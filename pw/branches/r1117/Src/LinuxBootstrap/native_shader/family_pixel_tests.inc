// Included after the existing fixtures; every borrowed view remains live through its call.
unsigned familyCompiles = 0, familyLinks = 0, failFamilyCompile = 0, failFamilyLink = 0;
void APIENTRY FamilyCompile(GLuint shader) {
	if (++familyCompiles != failFamilyCompile) glCompileShader(shader);
}
void APIENTRY FamilyLink(GLuint program) {
	if (++familyLinks != failFamilyLink) glLinkProgram(program);
}
void APIENTRY FamilyCompileArb(GLhandleARB shader) {
	if (++familyCompiles != failFamilyCompile)
		reinterpret_cast<PFNGLCOMPILESHADERARBPROC>(glXGetProcAddressARB(
			reinterpret_cast<const GLubyte*>("glCompileShaderARB")))(shader);
}
void APIENTRY FamilyLinkArb(GLhandleARB program) {
	if (++familyLinks != failFamilyLink)
		reinterpret_cast<PFNGLLINKPROGRAMARBPROC>(glXGetProcAddressARB(
			reinterpret_cast<const GLubyte*>("glLinkProgramARB")))(program);
}
/** Instrument the existing host lookup, without a production capability/failure flag. */
void* FamilyLookup(const char* name, void* user) {
	if (!std::strcmp(name, "glCompileShader")) return reinterpret_cast<void*>(FamilyCompile);
	if (!std::strcmp(name, "glLinkProgram")) return reinterpret_cast<void*>(FamilyLink);
	if (!std::strcmp(name, "glCompileShaderARB")) return reinterpret_cast<void*>(FamilyCompileArb);
	if (!std::strcmp(name, "glLinkProgramARB")) return reinterpret_cast<void*>(FamilyLinkArb);
	return Host::Lookup(name, user);
}
std::array<ProgramRef,3> FamilyTickets(const BasicFxOpacityPrograms& family) {
	return {family.simple, family.additiveOpacity, family.blendOpacity};
}
bool FamilyEmpty(const BasicFxOpacityPrograms& family) {
	return family.simple.Empty() && family.additiveOpacity.Empty() && family.blendOpacity.Empty();
}
void Opacity(Checks& check, ShaderBackend& backend, float value) {
	const Float4 opacity{value,0,0,0};
	check.Good(backend.SetFloat4(Stage::Pixel,6,{&opacity,1}), "family PS c6 upload");
}
Pixel OpacityPixel(Float4 value, float opacity, size_t slot) {
	Pixel result{};
	for (size_t i = 0; i < 4; ++i) {
		const double scaled = double(value[i]) * ((slot == 1 && i < 3) || (slot == 2 && i == 3) ? opacity : 1.f);
		result[i] = static_cast<unsigned char>(std::lround(std::clamp(scaled,0.,1.) * 255));
	}
	return result;
}

/** Fixed program identity, final-color semantics, reload atomicity and context ownership. */
void FamilyTests(Checks& check, Host& host, const Bytes& archive, BasicFxOpacityPrograms& late) {
	Target target; Texture white(true), colored; Mesh mesh;
	Result status;
	const ContextBinding binding{&host,Host::Current,FamilyLookup};
	auto backend = ShaderBackend::Create(binding,{},status);
	check.Good(status,"family backend creation");
	if (!backend) return;
	BasicFxOpacityPrograms family;
	const size_t createCalls = backend->GetDiagnostics().allocationCalls;
	const unsigned compilesBefore = familyCompiles, linksBefore = familyLinks;
	const auto loadState = Snapshot(white.name);
	status = backend->LoadBasicFxOpacityFamily({archive.data(),archive.size()},family);
	check.Good(status,"load fixed BasicFX opacity family");
	check.Test(!family.simple.Empty() && !family.additiveOpacity.Empty() && !family.blendOpacity.Empty(),
		"successful family load publishes three CPU tickets");
	if (!status) { check.Good(backend->Shutdown(),"unimplemented/failed family baseline shutdown"); return; }
	check.Test(Snapshot(white.name) == loadState,"family load preserves host state");
	check.Test(familyCompiles - compilesBefore == 6 && familyLinks - linksBefore == 3,
		"family owns six load-time shaders and three linked programs");
	const size_t loadCalls = backend->GetDiagnostics().allocationCalls;
	std::cout << "FAMILY create_calls=" << createCalls << " load_calls=" << loadCalls << '\n';
	auto tickets = FamilyTickets(family);
	auto draw = mesh.Draw(white.name);
	Registers(*backend);
	target.Clear();
	check.Good(backend->Draw(tickets[0],draw),"family pair zero still needs no PS c6");
	for (size_t slot : {size_t(1),size_t(2)}) {
		check.Test(backend->Draw(tickets[slot],draw).code == Code::UninitializedRegisters,"opacity program requires PS c6");
	}
	const unsigned loadedCompiles = familyCompiles, loadedLinks = familyLinks;
	const Float4 color{.2f,.4f,.6f,.8f};
	for (float opacity : {0.f,.25f,1.f,1.1f,-.5f}) for (size_t slot : {size_t(0),size_t(1),size_t(2),size_t(0)}) {
		Registers(*backend,Identity,Identity,{}, {.1f,.25f,.5f,.5f}, {.1f,.15f,.1f,.3f});
		Opacity(check,*backend,opacity); target.Clear();
		check.Good(backend->Draw(tickets[slot],draw),"alternating family draw with authored-range opacity");
		PixelIs(check,32,32,OpacityPixel(color,opacity,slot),"pair0 ignores k, pair12 scales RGB, pair24 scales alpha after mul/add");
		DepthIs(check,.25f,"family Off retains original depth mapping/write");
	}
	check.Test(familyCompiles == loadedCompiles && familyLinks == loadedLinks,"ticket switching performs no shader compile or link");
	// The same numeric register in different stages must not alias when programs switch.
	for (size_t slot = 0; slot < 3; ++slot) {
		Registers(*backend,Identity,Identity,{.5f,0,0,0},{.2f,.4f,.6f,.8f});
		Opacity(check,*backend,.5f); target.Clear();
		check.Good(backend->Draw(tickets[slot],mesh.Draw(colored.name)),"family independent VS/PS c20 draw");
		PixelIs(check,32,32,OpacityPixel({0,.4f,0,.8f},.5f,slot),"UV-selected green texel retains independent pixel constants");
	}
	const std::array<Float4,4> world{{{.8f,.2f,0,.15f},{-.1f,.6f,0,-.05f},{0,0,1,.1f},{0,0,0,1}}};
	const std::array<Float4,4> view{{{.9f,-.15f,0,-.1f},{.25f,1.1f,0,.2f},{0,0,1,0},{.1f,0,0,1}}};
	for (const auto& ticket : tickets) {
		Registers(*backend,world,view); Opacity(check,*backend,1); target.Clear();
		check.Good(backend->Draw(ticket,mesh.Draw(colored.name)),"family nonsymmetric perspective draw");
		check.Test(Coverage(mesh,world,view),"family keeps independent transformed-coverage oracle");
	}
	for (size_t slot = 0; slot < 3; ++slot) for (int alpha : {126,127,128}) {
		Registers(*backend,Identity,Identity,{}, {}, {.2f,.4f,.6f,(2*alpha)/255.f});
		Opacity(check,*backend,.5f); target.Clear();
		draw.alphaTest = AlphaTest::GreaterEqual; draw.alphaReference = 127;
		check.Good(backend->Draw(tickets[slot],draw),"family final-alpha boundary draw");
		const bool visible = slot != 2 || alpha >= 127;
		PixelIs(check,32,32,visible ? OpacityPixel({.2f,.4f,.6f,(2*alpha)/255.f},.5f,slot) : ClearPixel,
			"only BlendOpacity scales alpha before GEQUAL");
		DepthIs(check,visible ? .25f : 1.f,"discarded opacity fragments do not write depth");
	}
	draw = mesh.Draw(white.name);
	for (size_t slot = 0; slot < 3; ++slot) for (CullMode cull : {CullMode::Back,CullMode::Disabled}) {
		auto mirrored = Identity; mirrored[0][0] = -1;
		Registers(*backend,mirrored,Identity,{}, {}, color); Opacity(check,*backend,.5f); target.Clear();
		draw.cullMode = cull;
		check.Good(backend->Draw(tickets[slot],draw),"family mirrored culling draw");
		PixelIs(check,32,32,cull == CullMode::Disabled ? OpacityPixel(color,.5f,slot) : ClearPixel,
			"family preserves explicit two-sided/culling policy");
	}
	draw = mesh.Draw(white.name);
	if (IndexedBlendAvailable()) {
		const Pixel destination{47,89,131,73};
		Foreign foreign(archive,mesh);
		for (size_t slot : {size_t(1),size_t(2)}) for (BlendMode blend : {BlendMode::LerpByAlpha,BlendMode::AddColor}) {
			Registers(*backend,Identity,Identity,{}, {}, color); Opacity(check,*backend,.5f);
			draw.blendMode = blend; ClearBlendTarget(target,destination);
			glUseProgram(foreign.program); glBindVertexArray(foreign.vao);
			glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_NEVER,.7f);
			glColorMaski(1,0,1,0,1); glEnablei(GL_BLEND,1);
			const auto before = Snapshot(white.name);
			check.Good(backend->Draw(tickets[slot],draw),"opacity pin independent of fixed-function blend mode");
			PixelIs(check,32,32,Blended(blend,OpacityPixel(color,.5f,slot),destination),"combined opacity/blend preserves destination alpha");
			DepthIs(check,1,"opacity transparency leaves depth read-only");
			check.Test(Snapshot(white.name) == before,"family draw restores foreign program and indexed state");
			ClearBlendTarget(target,destination,.1);
			check.Good(backend->Draw(tickets[slot],draw),"opacity layer behind existing depth");
			PixelIs(check,32,32,destination,"opaque depth occludes opacity layer");
			DepthIs(check,.1f,"occlusion leaves existing depth unchanged");
		}
	}
	CleanRaster(); draw = mesh.Draw(white.name);
	const Float4 bad{std::numeric_limits<float>::quiet_NaN(),0,0,0};
	check.Test(!backend->SetFloat4(Stage::Pixel,6,{&bad,1}),"invalid family opacity register rejected");
	for (const auto& ticket : tickets) check.Test(backend->Draw(ticket,draw).code == Code::UninitializedRegisters,
		"invalid register write invalidates every family slot");
	Registers(*backend);
	check.Good(backend->Draw(tickets[0],draw),"pair zero recovers without c6 after invalid write");
	check.Test(backend->Draw(tickets[2],draw).code == Code::UninitializedRegisters,"opacity recovery still requires reuploading c6");
	Opacity(check,*backend,1);
	// A rejected thread/context call clears its caller output without invalidating the loaded set.
	BasicFxOpacityPrograms rejected = family;
	std::thread worker([&] { status = backend->LoadBasicFxOpacityFamily({archive.data(),archive.size()},rejected); }); worker.join();
	check.Test(status.code == Code::WrongThread && FamilyEmpty(rejected),"wrong-thread family load clears only caller output");
	host.ReleaseCurrent(); rejected = family;
	status = backend->LoadBasicFxOpacityFamily({archive.data(),archive.size()},rejected);
	check.Test(status.code == Code::WrongContext && FamilyEmpty(rejected),"missing-context family load clears only caller output");
	host.MakeCurrent(); target.Clear();
	check.Good(backend->Draw(tickets[2],draw),"rejected thread/context load preserves usable family and registers");
	const auto reload = [&] {
		check.Good(backend->LoadBasicFxOpacityFamily({archive.data(),archive.size()},family),"reload valid complete family");
		Registers(*backend); Opacity(check,*backend,1); tickets = FamilyTickets(family);
	};
	const auto failed = [&](ByteView bytes, const char* name) {
		const auto previous = tickets; family = {tickets[0],tickets[1],tickets[2]};
		target.Clear(); const auto before = Snapshot(white.name); const auto count = backend->GetDiagnostics().draws;
		check.Test(!backend->LoadBasicFxOpacityFamily(bytes,family) && FamilyEmpty(family),name);
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code == Code::InvalidReference,
			"failed family reload invalidates every prior slot");
		check.Test(backend->GetDiagnostics().draws == count && Snapshot(white.name) == before,"failed family load preserves draw count and host state");
		PixelIs(check,32,32,ClearPixel,"failed family reload emits no pixels");
		reload();
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code == Code::InvalidReference,
			"successful retry never resurrects old family serial");
	};
	failed({},"empty family reload fails all");
	for (size_t pair : {size_t(12),size_t(24)}) {
		auto corrupt = archive;
		for (size_t i = 0; i < 4; ++i) corrupt[4+pair*8+4+i] = archive[8+i];
		failed({corrupt.data(),corrupt.size()},"wrong reflected opacity PS rejects whole family");
		const auto previous = tickets; ProgramRef legacy;
		check.Good(backend->LoadBasicFxPair0({corrupt.data(),corrupt.size()},legacy),"legacy loader ignores unused opacity reflection");
		Registers(*backend); target.Clear();
		check.Good(backend->Draw(legacy,draw),"legacy loader retains pair-zero register contract");
		PixelIs(check,32,32,{255,255,255,255},"legacy pair-zero pixel after family replacement");
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code == Code::InvalidReference,"pair-zero reload invalidates all family tickets");
		reload();
		check.Test(backend->Draw(legacy,draw).code == Code::InvalidReference,"family reload invalidates old pair-zero ticket");
	}
	for (unsigned member = 1; member <= 3; ++member) {
		failFamilyLink = familyLinks + member;
		failed({archive.data(),archive.size()},"injected member link failure publishes no family");
		failFamilyLink = 0;
	}
	for (unsigned stage : {1u,3u,6u}) {
		failFamilyCompile = familyCompiles + stage;
		failed({archive.data(),archive.size()},"injected stage compile failure publishes no family");
		failFamilyCompile = 0;
	}
	{
		Host other(31); other.Open(); Result otherStatus;
		auto second = ShaderBackend::Create(other.Binding(),{},otherStatus);
		check.Good(otherStatus,"foreign family context backend");
		if (second) {
			BasicFxOpacityPrograms secondFamily;
			check.Good(second->LoadBasicFxOpacityFamily({archive.data(),archive.size()},secondFamily),"foreign context family load");
			for (const auto& ticket : tickets) check.Test(second->Draw(ticket,draw).code == Code::InvalidReference,"foreign backend rejects each family slot before texture access");
			rejected = family;
			check.Test(backend->LoadBasicFxOpacityFamily({archive.data(),archive.size()},rejected).code == Code::WrongContext && FamilyEmpty(rejected),
				"other current context cannot reload family");
			check.Good(second->Shutdown(),"foreign family shutdown");
			check.Test(second->GetDiagnostics().liveBytes == 0,"foreign family releases all stages");
		}
	}
	host.MakeCurrent(); late = family;
	check.Good(backend->Draw(tickets[1],draw),"original family survives foreign-context attempts");
	check.Good(backend->Shutdown(),"family owning-context shutdown");
	check.Test(backend->GetDiagnostics().liveBytes == 0,"all six owned shaders and three programs released");
	rejected = family;
	check.Test(backend->LoadBasicFxOpacityFamily({archive.data(),archive.size()},rejected).code == Code::Closed && FamilyEmpty(rejected),"closed family load clears output");
	check.Good(backend->Shutdown(),"family shutdown idempotent");
	// Fail every upstream allocation during first family load, including each member's link.
	for (size_t calls = createCalls; calls < loadCalls; ++calls) {
		Limits limits; limits.allocationCalls = calls;
		auto trial = ShaderBackend::Create(binding,limits,status);
		check.Good(status,"family allocation sweep creates context");
		if (!trial) continue;
		BasicFxOpacityPrograms output = late;
		check.Test(!trial->LoadBasicFxOpacityFamily({archive.data(),archive.size()},output) && FamilyEmpty(output),"family allocation refusal publishes no partial tickets");
		check.Test(trial->GetDiagnostics().refusedAllocations > 0,"family allocation sweep reaches refusal");
		check.Good(trial->Shutdown(),"partial family allocation shutdown");
		check.Test(trial->GetDiagnostics().liveBytes == 0,"partial family allocation releases every owned stage");
	}
	// Exhaust during reload with an existing set still allocated; no fallback remains drawable.
	Limits limits; limits.allocationCalls = loadCalls + (loadCalls-createCalls) - 1;
	auto trial = ShaderBackend::Create(binding,limits,status);
	check.Good(status,"family reload allocation context");
	if (trial) {
		BasicFxOpacityPrograms first, next;
		check.Good(trial->LoadBasicFxOpacityFamily({archive.data(),archive.size()},first),"family reload allocation first set");
		check.Test(!trial->LoadBasicFxOpacityFamily({archive.data(),archive.size()},next) && FamilyEmpty(next),"last reload allocation failure is all-or-none");
		for (const auto& ticket : FamilyTickets(first)) check.Test(trial->Draw(ticket,draw).code == Code::InvalidReference,"reload allocation failure disables previous tickets");
		check.Test(trial->GetDiagnostics().refusedAllocations > 0,"reload actually reaches allocation refusal");
		check.Good(trial->Shutdown(),"reload allocation failure shutdown");
		check.Test(trial->GetDiagnostics().liveBytes == 0,"reload allocation failure releases old and candidate stages");
	}
	// Family loading itself and opaque opacity draws do not require indexed blending.
	host.hiddenBlendFunctions = 3;
	auto opaque = ShaderBackend::Create(host.Binding(),{},status);
	check.Good(status,"family context without optional blending functions");
	if (opaque) {
		BasicFxOpacityPrograms programs;
		check.Good(opaque->LoadBasicFxOpacityFamily({archive.data(),archive.size()},programs),"family loads without indexed blending capability");
		Registers(*opaque,Identity,Identity,{}, {}, color); Opacity(check,*opaque,.5f);
		const auto all = FamilyTickets(programs);
		for (size_t slot = 0; slot < 3; ++slot) {
			target.Clear();
			check.Good(opaque->Draw(all[slot],draw),"opaque family draw without optional blend functions");
			PixelIs(check,32,32,OpacityPixel(color,.5f,slot),"opaque opacity family expected pixel");
			auto blended = draw; blended.blendMode = BlendMode::LerpByAlpha;
			const auto before = Snapshot(white.name); const auto count = opaque->GetDiagnostics().draws;
			check.Test(opaque->Draw(all[slot],blended).code == Code::Unsupported,"non-Off family draw still requires indexed blending");
			check.Test(Snapshot(white.name) == before && opaque->GetDiagnostics().draws == count,"missing family blend capability is state-atomic");
		}
		check.Good(opaque->Shutdown(),"opaque-only family shutdown");
		check.Test(opaque->GetDiagnostics().liveBytes == 0,"opaque-only family allocation balance");
	}
	host.hiddenBlendFunctions = 0; CleanRaster();
	check.Test(glGetError() == GL_NO_ERROR,"family final clean GL state");
}
