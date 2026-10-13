// Included in the isolated GL probe, reusing its host, allocation and pixel fixtures.
std::array<ProgramRef,4> StaticTickets(const StaticPrograms& programs) {
	return {programs.basicFx.simple,programs.basicFx.additiveOpacity,
		programs.basicFx.blendOpacity,programs.bakedBasicMaterial};
}
bool StaticEmpty(const StaticPrograms& programs) {
	return FamilyEmpty(programs.basicFx) && programs.bakedBasicMaterial.Empty();
}

/** Independent, separately padded BGRA streams; vertex zero is intentionally unused. */
struct BakedColors {
	Bytes day = Bytes(43,0xa7), night = Bytes(59,0xb9);
	void Put(size_t vertex, Pixel d, Pixel n) {
		const size_t swizzle[] = {2,1,0,3};
		for (size_t c = 0; c < 4; ++c) {
			day[3+vertex*8+c] = d[swizzle[c]];
			night[7+vertex*10+c] = n[swizzle[c]];
		}
	}
	void Fill(Pixel d, Pixel n) { for (size_t i = 1; i <= 4; ++i) Put(i,d,n); }
	void Bind(IndexedDraw& draw) const {
		draw.dayColors = {{day.data(),day.size()},3,8};
		draw.nightColors = {{night.data(),night.size()},7,10};
	}
};

/** Upload only reflected baked registers, excluding shader-owned c3, UV c20 and PS c6. */
void BakedRegisters(ShaderBackend& backend, float night = 0, Float4 mul = {1,1,1,1},
	Float4 add = {}, const std::array<Float4,4>& world = Identity, const std::array<Float4,4>& view = Identity) {
	const Float4 fraction{night,17,19,23};
	for (auto status : {backend.SetFloat4(Stage::Vertex,0,{world.data(),3}),
		backend.SetFloat4(Stage::Vertex,8,{view.data(),4}), backend.SetFloat4(Stage::Vertex,234,{&fraction,1}),
		backend.SetFloat4(Stage::Pixel,20,{&mul,1}),backend.SetFloat4(Stage::Pixel,21,{&add,1})})
		if (!status) throw std::runtime_error("baked register upload: " + status.message);
}

/** Byte-domain oracle: decode per vertex, interpolate, then texture RGB * light * mul + add. */
Float4 BakedLight(Pixel day, Pixel night, double fraction) {
	Float4 result{};
	for (size_t c = 0; c < 3; ++c)
		result[c] = float((double(day[c])*day[3]*(1-fraction) + double(night[c])*night[3]*fraction)/255.);
	return result;
}
Pixel BakedPixel(Float4 light, Float4 mul, Float4 add, Pixel texture = {255,255,255,255}) {
	Pixel result = texture;
	for (size_t c = 0; c < 3; ++c)
		result[c] = static_cast<unsigned char>(std::lround(std::clamp(double(light[c])*texture[c]/255.*mul[c]+add[c],0.,1.)*255));
	return result;
}

/** Four atomic programs, original HDR/light math, independent streams and strict failure paths. */
void BakedTests(Checks& check, Host& host, const Bytes& fx, const Bytes& baked, StaticPrograms& late) {
	Target target; Texture white(true), colored; Mesh mesh; BakedColors colors;
	const Pixel day{170,85,51,3}, night{34,136,204,5};
	const Float4 mul{.2f,.15f,.1f,0}, add{.01f,.03f,.05f,.9f};
	colors.Fill(day,night);
	auto draw = mesh.Draw(white.name); colors.Bind(draw);
	const ByteView fxBytes{fx.data(),fx.size()}, bakedBytes{baked.data(),baked.size()};
	const ContextBinding binding{&host,Host::Current,FamilyLookup};
	Result status; auto backend = ShaderBackend::Create(binding,{},status);
	check.Good(status,"static create one backend owner");
	if (!backend) return;
	StaticPrograms programs;
	const size_t createCalls = backend->GetDiagnostics().allocationCalls;
	const unsigned compiled = familyCompiles, linked = familyLinks;
	const auto loadState = Snapshot(white.name);
	status = backend->LoadStaticFamily(fxBytes,bakedBytes,programs);
	check.Good(status,"atomic BasicFX0/12/24 plus baked BasicMaterial24 load");
	check.Test(!StaticEmpty(programs),"combined family publishes tickets");
	if (!status) { check.Good(backend->Shutdown(),"failed static baseline shutdown"); return; }
	auto tickets = StaticTickets(programs);
	for (const auto& ticket : tickets) check.Test(!ticket.Empty(),"each combined ticket published");
	check.Test(Snapshot(white.name) == loadState,"combined load restores complete host state");
	check.Test(familyCompiles-compiled == 8 && familyLinks-linked == 4,"exactly eight shaders/four programs in one owner");
	const size_t loadCalls = backend->GetDiagnostics().allocationCalls;
	std::cout << "STATIC create_calls=" << createCalls << " load_calls=" << loadCalls << '\n';
	const unsigned loadedCompiles = familyCompiles, loadedLinks = familyLinks;
	BakedRegisters(*backend);
	check.Test(backend->Draw(tickets[0],draw).code == Code::UninitializedRegisters,"baked uploads do not falsely initialize BasicFX registers");
	for (float fraction : {0.f,.25f,.5f,1.f,-.25f,1.25f}) {
		BakedRegisters(*backend,fraction,mul,add); target.Clear();
		check.Good(backend->Draw(tickets[3],draw),"baked HDR day/night draw with finite extrapolation");
		PixelIs(check,32,32,BakedPixel(BakedLight(day,night,fraction),mul,add),"BGRA decode, alpha-as-HDR-scale, original night lerp and RGB constants");
		DepthIs(check,.25f,"baked D3D depth mapping");
	}
	// Alternate four tickets and two texture sources; colors may be invalid for BasicFX.
	for (size_t slot : {size_t(3),size_t(0),size_t(3),size_t(1),size_t(2),size_t(3)}) {
		Registers(*backend,Identity,Identity,{.5f,0,0,0},mul,add);
		Opacity(check,*backend,.5f); BakedRegisters(*backend,.25f,mul,add);
		auto alternating = draw; alternating.texture = colored.name;
		if (slot != 3) { alternating.dayColors = {{nullptr,SIZE_MAX},SIZE_MAX,0}; alternating.nightColors = {}; }
		target.Clear(); check.Good(backend->Draw(tickets[slot],alternating),"interleaved FX and baked stream/texture draw");
		Float4 fxValue = add; fxValue[1] += mul[1]; fxValue[3] += mul[3];
		PixelIs(check,32,32,slot == 3 ? BakedPixel(BakedLight(day,night,.25),mul,add,{255,0,0,255}) :
			OpacityPixel(fxValue,.5f,slot),"baked ignores UV offset while FX ignores color descriptors");
	}
	check.Test(familyCompiles == loadedCompiles && familyLinks == loadedLinks,"per-draw switching compiles/links nothing");
	// Host state must neither clamp the vertex HDR light nor select undefined gl_BackColor.
	{
		Foreign foreign(fx,mesh);
		for (GLenum clamp : {GL_TRUE,GL_FALSE,GL_FIXED_ONLY}) for (bool mirrored : {false,true}) {
			auto world = Identity; if (mirrored) world[0][0] = -1;
			BakedRegisters(*backend,.5f,mul,add,world); target.Clear();
			glClampColor(GL_CLAMP_VERTEX_COLOR,clamp); glClampColor(GL_CLAMP_FRAGMENT_COLOR,clamp);
			glEnable(GL_VERTEX_PROGRAM_TWO_SIDE); glUseProgram(foreign.program); glBindVertexArray(foreign.vao);
			glBindBuffer(GL_ARRAY_BUFFER,foreign.vbo); glBindSampler(0,foreign.sampler);
			glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,colored.name);
			glColorMaski(1,0,1,0,1); glEnablei(GL_BLEND,1);
			draw.cullMode = CullMode::Disabled;
			const auto before = Snapshot(white.name);
			check.Good(backend->Draw(tickets[3],draw),"baked hostile host clamp/two-side/foreign arrays draw");
			PixelIs(check,32,32,BakedPixel(BakedLight(day,night,.5),mul,add),"HDR survives vertex clamp and mirrored back face");
			check.Test(Snapshot(white.name) == before,"baked restores clamping, two-side, foreign program, arrays and indexed masks");
		}
	}
	glDisable(GL_VERTEX_PROGRAM_TWO_SIDE); glClampColor(GL_CLAMP_VERTEX_COLOR,GL_TRUE);
	glClampColor(GL_CLAMP_FRAGMENT_COLOR,GL_FIXED_ONLY); glActiveTexture(GL_TEXTURE0); CleanRaster();
	draw.cullMode = CullMode::Back;
	// Float framebuffer catches fragment clamping independently of the vertex HDR test.
	glBindTexture(GL_TEXTURE_2D,target.color);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,64,64,0,GL_RGBA,GL_FLOAT,nullptr);
	check.Test(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,"HDR float target complete");
	BakedRegisters(*backend,0); target.Clear(); glClampColor(GL_CLAMP_FRAGMENT_COLOR,GL_TRUE);
	const auto floatState = Snapshot(white.name);
	check.Good(backend->Draw(tickets[3],draw),"unclamped floating output draw");
	glClampColor(GL_CLAMP_READ_COLOR,GL_FALSE);
	Float4 actual{}; glReadPixels(32,32,1,1,GL_RGBA,GL_FLOAT,actual.data());
	const auto expected = BakedLight(day,night,0);
	check.Test(std::abs(actual[0]-expected[0]) < .0001f && std::abs(actual[1]-expected[1]) < .0001f &&
		std::abs(actual[2]-expected[2]) < .0001f && std::abs(actual[3]-1) < .0001f,"floating HDR RGB above one remains original shader output");
	check.Test(Snapshot(white.name) == floatState,"fragment clamp restored after float draw");
	glClampColor(GL_CLAMP_READ_COLOR,GL_FIXED_ONLY); glClampColor(GL_CLAMP_FRAGMENT_COLOR,GL_FIXED_ONLY);
	glBindTexture(GL_TEXTURE_2D,target.color);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
	// Authored texture alpha, not color scale or PS constant alpha, drives GEQUAL.
	for (unsigned char alpha : {109,110,111}) {
		Texture cutout(true,alpha); auto cut = draw; cut.texture = cutout.name;
		cut.alphaTest = AlphaTest::GreaterEqual; cut.alphaReference = 110;
		BakedRegisters(*backend,0,mul,add); target.Clear();
		check.Good(backend->Draw(tickets[3],cut),"baked authored alpha110 threshold");
		PixelIs(check,32,32,alpha < 110 ? ClearPixel : BakedPixel(BakedLight(day,night,0),mul,add,{255,255,255,alpha}),
			"baked alpha is texture alpha, ignores PS constant alpha and HDR scale");
		DepthIs(check,alpha < 110 ? 1.f : .25f,"baked cutout discarded depth");
	}
	// Interpolate differing per-vertex HDR decode, not encoded RGB/alpha multiplied per pixel.
	const Pixel ds[] = {{200,80,10,1},{100,50,20,5},{25,70,150,3},{90,20,140,2}};
	const Pixel ns[] = {{30,90,100,4},{20,50,80,2},{140,80,20,1},{100,60,10,3}};
	for (size_t i = 0; i < 4; ++i) colors.Put(i+1,ds[i],ns[i]);
	BakedRegisters(*backend,.3f,mul,add); target.Clear();
	check.Good(backend->Draw(tickets[3],draw),"per-vertex independent color stream draw");
	for (auto xy : {std::array<int,2>{38,24},std::array<int,2>{24,38},std::array<int,2>{40,30}}) {
		const double u = (xy[0]+.5-16)/32., v = (xy[1]+.5-16)/32.;
		const double weights[] = {1-std::max(u,v),std::max(u-v,0.),std::min(u,v),std::max(v-u,0.)};
		Float4 light{};
		for (size_t i = 0; i < 4; ++i) for (size_t c = 0; c < 3; ++c) light[c] += float(weights[i]*BakedLight(ds[i],ns[i],.3)[c]);
		PixelIs(check,xy[0],xy[1],BakedPixel(light,mul,add),"independent triangle interpolation oracle");
	}
	colors.Fill(day,night); BakedRegisters(*backend,.25f,mul,add);
	// UInt32 and negative base resolve to exactly the same physical geometry/color indices.
	const uint32_t largeIndices[] = {99,99,2,3,4,2,4,5};
	auto wide = draw; wide.indexType = IndexType::UInt32; wide.baseVertex = -1;
	wide.indexData = {largeIndices,sizeof(largeIndices)}; target.Clear();
	check.Good(backend->Draw(tickets[3],wide),"baked UInt32 indices with valid negative base");
	PixelIs(check,32,32,BakedPixel(BakedLight(day,night,.25),mul,add),"color addressing uses resolved index, not stream0 offset");
	const auto rejectedDraw = [&](const IndexedDraw& invalid, Code code, const char* name) {
		target.Clear(); const auto before = Snapshot(white.name); const auto count = backend->GetDiagnostics().draws;
		check.Test(backend->Draw(tickets[3],invalid).code == code,name);
		check.Test(before == Snapshot(white.name) && count == backend->GetDiagnostics().draws,"rejected baked draw preserves state and draw count");
		PixelIs(check,32,32,ClearPixel,"rejected baked draw emits no pixels");
	};
	for (bool isNight : {false,true}) for (unsigned variant = 0; variant < 7; ++variant) {
		auto invalid = draw; auto& stream = isNight ? invalid.nightColors : invalid.dayColors;
		switch (variant) {
		case 0: stream.bytes.data = nullptr; break;
		case 1: stream.bytes.size = 0; break;
		case 2: stream.bytes.size = stream.offset+4*stream.stride+3; break;
		case 3: stream.stride = 3; break;
		case 4: stream.offset = SIZE_MAX; break;
		case 5: stream.stride = SIZE_MAX; break;
		case 6: stream.bytes.size = SIZE_MAX; break;
		}
		rejectedDraw(invalid,Code::InvalidInput,"invalid bounded day/night descriptor");
	}
	for (auto blend : BlendModes) { auto invalid = draw; invalid.blendMode = blend; rejectedDraw(invalid,Code::Unsupported,"baked slice rejects unsupported blending"); }
	const Float4 bad{std::numeric_limits<float>::quiet_NaN(),0,0,0};
	check.Test(!backend->SetFloat4(Stage::Vertex,234,{&bad,1}),"NaN night fraction rejected");
	for (const auto& ticket : tickets) check.Test(backend->Draw(ticket,draw).code == Code::UninitializedRegisters,"bad write invalidates all four register contracts");
	BakedRegisters(*backend,.25f,mul,add); target.Clear();
	check.Good(backend->Draw(tickets[3],draw),"baked recovers without shader-owned c3 or FX-only registers");
	const auto reload = [&] {
		check.Good(backend->LoadStaticFamily(fxBytes,bakedBytes,programs),"reload complete static family");
		tickets = StaticTickets(programs); Registers(*backend); Opacity(check,*backend,1); BakedRegisters(*backend,0,mul,add);
	};
	// Every reflected application register is required independently, no extra register is.
	for (unsigned missing : {0u,1u,2u,8u,9u,10u,11u,234u,276u,277u}) {
		check.Good(backend->LoadStaticFamily(fxBytes,bakedBytes,programs),"reset static register initialization");
		tickets = StaticTickets(programs);
		for (unsigned reg : {0u,1u,2u,8u,9u,10u,11u,234u,276u,277u}) if (reg != missing) {
			const Float4 value = reg < 3 ? Identity[reg] : reg >= 8 && reg <= 11 ? Identity[reg-8] : Float4{};
			check.Good(backend->SetFloat4(reg >= 256 ? Stage::Pixel : Stage::Vertex,reg >= 256 ? reg-256 : reg,{&value,1}),"required baked register upload");
		}
		rejectedDraw(draw,Code::UninitializedRegisters,"one missing reflected register blocks baked draw");
	}
	reload();
	StaticPrograms refused = programs;
	std::thread worker([&] { status = backend->LoadStaticFamily(fxBytes,bakedBytes,refused); }); worker.join();
	check.Test(status.code == Code::WrongThread && StaticEmpty(refused),"wrong-thread combined load clears caller output only");
	host.ReleaseCurrent(); refused = programs;
	status = backend->LoadStaticFamily(fxBytes,bakedBytes,refused);
	check.Test(status.code == Code::WrongContext && StaticEmpty(refused),"no-context combined load clears caller output only");
	host.MakeCurrent(); target.Clear(); check.Good(backend->Draw(tickets[3],draw),"wrong-thread/context leaves static family usable");
	const auto failedLoad = [&](ByteView f, ByteView b, const char* name) {
		const auto previous = tickets; refused = programs; target.Clear();
		const auto before = Snapshot(white.name); const auto count = backend->GetDiagnostics().draws;
		check.Test(!backend->LoadStaticFamily(f,b,refused) && StaticEmpty(refused),name);
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code == Code::InvalidReference,"failed combined load invalidates every old ticket");
		check.Test(Snapshot(white.name) == before && count == backend->GetDiagnostics().draws,"failed combined load leaves GL/draw count unchanged");
		PixelIs(check,32,32,ClearPixel,"failed combined load emits no pixels");
		reload();
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code == Code::InvalidReference,"retry never resurrects failed-load tickets");
	};
	failedLoad({},bakedBytes,"empty BasicFX archive rejects combined load");
	failedLoad(fxBytes,{},"empty BasicMaterial archive rejects combined load");
	failedLoad(fxBytes,{baked.data(),294915},"truncated BasicMaterial directory rejects combined load");
	for (bool basic : {false,true}) {
		auto corrupt = basic ? baked : fx;
		// A valid envelope with a wrong selected reflection must fail the whole family.
		std::copy_n(corrupt.data()+8,4,corrupt.data()+4+24*8+4);
		if (basic) failedLoad(fxBytes,{corrupt.data(),corrupt.size()},"wrong baked PS reflection rejects combined family");
		else failedLoad({corrupt.data(),corrupt.size()},bakedBytes,"wrong BasicFX opacity reflection rejects combined family");
	}
	for (unsigned stage = 1; stage <= 8; ++stage) {
		failFamilyCompile = familyCompiles+stage;
		failedLoad(fxBytes,bakedBytes,"injected static member compile failure publishes nothing"); failFamilyCompile = 0;
	}
	for (unsigned member = 1; member <= 4; ++member) {
		failFamilyLink = familyLinks+member;
		failedLoad(fxBytes,bakedBytes,"injected static member link failure publishes nothing"); failFamilyLink = 0;
	}
	for (bool family : {false,true}) {
		const auto previous = tickets; ProgramRef simple; BasicFxOpacityPrograms legacy;
		check.Good(family ? backend->LoadBasicFxOpacityFamily(fxBytes,legacy) : backend->LoadBasicFxPair0(fxBytes,simple),"legacy API replaces complete combined family");
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code == Code::InvalidReference,"legacy reload invalidates all four static tickets");
		Registers(*backend); target.Clear(); check.Good(backend->Draw(family ? legacy.simple : simple,draw),"legacy API remains drawable after combined replacement");
		reload(); check.Test(backend->Draw(family ? legacy.simple : simple,draw).code == Code::InvalidReference,"combined reload invalidates legacy ticket");
	}
	{
		Host other(47); other.Open(); Result otherStatus;
		auto second = ShaderBackend::Create(other.Binding(),{},otherStatus);
		check.Good(otherStatus,"foreign static context backend");
		if (second) {
			StaticPrograms foreign; check.Good(second->LoadStaticFamily(fxBytes,bakedBytes,foreign),"foreign context loads own static set");
			for (const auto& ticket : tickets) check.Test(second->Draw(ticket,draw).code == Code::InvalidReference,"foreign owner rejects all static tickets");
			refused = programs;
			check.Test(backend->LoadStaticFamily(fxBytes,bakedBytes,refused).code == Code::WrongContext && StaticEmpty(refused),"foreign current context cannot reload original static owner");
			check.Good(second->Shutdown(),"foreign static owner shutdown");
			check.Test(second->GetDiagnostics().liveBytes == 0,"foreign static allocation balance");
		}
	}
	host.MakeCurrent(); target.Clear(); check.Good(backend->Draw(tickets[3],draw),"original static set survives foreign context");
	late = programs; check.Good(backend->Shutdown(),"static owner shutdown");
	check.Test(backend->GetDiagnostics().liveBytes == 0,"all eight shaders/four programs released");
	refused = programs;
	check.Test(backend->LoadStaticFamily(fxBytes,bakedBytes,refused).code == Code::Closed && StaticEmpty(refused),"closed combined load clears output");
	check.Good(backend->Shutdown(),"static shutdown idempotent");
	// Refuse each pinned MojoShader allocation during the full eight-stage/four-link load.
	for (size_t calls = createCalls; calls < loadCalls; ++calls) {
		Limits limits; limits.allocationCalls = calls;
		auto trial = ShaderBackend::Create(binding,limits,status);
		check.Good(status,"static allocation sweep context"); if (!trial) continue;
		StaticPrograms output = late;
		check.Test(!trial->LoadStaticFamily(fxBytes,bakedBytes,output) && StaticEmpty(output),"static OOM publishes no partial tickets");
		check.Test(trial->GetDiagnostics().refusedAllocations > 0,"static OOM reaches requested allocator boundary");
		check.Good(trial->Shutdown(),"static partial allocation shutdown");
		check.Test(trial->GetDiagnostics().liveBytes == 0,"static partial allocation releases all stages");
	}
	Limits cap; cap.allocationCalls = loadCalls+(loadCalls-createCalls)-1;
	auto trial = ShaderBackend::Create(binding,cap,status);
	check.Good(status,"static exhausted reload context");
	if (trial) {
		StaticPrograms first,next; check.Good(trial->LoadStaticFamily(fxBytes,bakedBytes,first),"static OOM first complete load");
		check.Test(!trial->LoadStaticFamily(fxBytes,bakedBytes,next) && StaticEmpty(next),"static last reload allocation fails atomically");
		for (const auto& ticket : StaticTickets(first)) check.Test(trial->Draw(ticket,draw).code == Code::InvalidReference,"reload OOM invalidates all old tickets");
		check.Test(trial->GetDiagnostics().refusedAllocations > 0,"reload OOM reaches actual allocation refusal");
		check.Good(trial->Shutdown(),"static reload OOM shutdown"); check.Test(trial->GetDiagnostics().liveBytes == 0,"reload OOM balances old/candidate allocations");
	}
	for (size_t bytes : {fx.size()+baked.size()-1,fx.size()+baked.size()}) {
		Limits limits; limits.archiveBytes = bytes;
		auto bounded = ShaderBackend::Create(host.Binding(),limits,status); check.Good(status,"bounded combined archive context");
		if (!bounded) continue;
		StaticPrograms output; status = bounded->LoadStaticFamily(fxBytes,bakedBytes,output);
		check.Test(bytes == fx.size()+baked.size() ? bool(status) && !StaticEmpty(output) : !status && StaticEmpty(output),"sum-of-archives cap exact boundary");
		check.Good(bounded->Shutdown(),"bounded archive shutdown");
	}
	host.hiddenBlendFunctions = 3;
	auto opaque = ShaderBackend::Create(host.Binding(),{},status); check.Good(status,"static no-optional-blend context");
	if (opaque) {
		StaticPrograms output; check.Good(opaque->LoadStaticFamily(fxBytes,bakedBytes,output),"static load does not require optional indexed blend");
		BakedRegisters(*opaque,0,mul,add); target.Clear();
		check.Good(opaque->Draw(output.bakedBasicMaterial,draw),"baked draw without optional blend entrypoints");
		PixelIs(check,32,32,BakedPixel(BakedLight(day,night,0),mul,add),"baked opaque fallback capability pixel");
		check.Good(opaque->Shutdown(),"static no-optional-blend shutdown");
		check.Test(opaque->GetDiagnostics().liveBytes == 0,"static no-optional-blend allocation balance");
	}
	host.hiddenBlendFunctions = 0; CleanRaster(); check.Test(glGetError() == GL_NO_ERROR,"static final clean GL state");
}
