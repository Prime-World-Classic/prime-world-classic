/** Terrain fixtures exercise actual shipped bytecode, not a replacement GLSL shader. */
std::vector<ProgramRef> SceneTickets(const ScenePrograms& p) {
	const auto statics=StaticTickets(p.statics);
	std::vector<ProgramRef> result(statics.begin(),statics.end());
	result.insert(result.end(),p.terrain.begin(),p.terrain.end());
	return result;
}
bool SceneEmpty(const ScenePrograms& p) {
	const auto tickets=SceneTickets(p);
	return std::all_of(tickets.begin(),tickets.end(),[](const ProgramRef& t) { return t.Empty(); });
}

/** COLOR2 stores layer weights in raw B,G,R,A byte order, unlike decoded lighting. */
struct TerrainColors : BakedColors {
	Bytes weights=Bytes(73,0xd3);
	void Weights(Pixel value) {
		for (size_t i=1; i<=4; ++i) std::memcpy(weights.data()+5+i*13,value.data(),4);
	}
	void Bind(IndexedDraw& draw) const {
		BakedColors::Bind(draw);
		draw.terrainWeights={{weights.data(),weights.size()},5,13};
	}
};
void SolidTexture(GLuint texture, Pixel color) {
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,color.data());
}
/** Only the seven reflected VS and two PS registers; WORLD is deliberately absent. */
void TerrainRegisters(ShaderBackend& backend, float night=0, float tiling=.1f,
	Float4 nature={0,1,0,0}, float overburn=0, Float4 mapping={1,-1,0,1},
	const std::array<Float4,4>& view=Identity) {
	const Float4 fraction{night,0,0,0}, tile{tiling,23,29,31}, burn{overburn,37,41,43};
	for (auto status : {backend.SetFloat4(Stage::Vertex,8,{view.data(),4}),
		backend.SetFloat4(Stage::Vertex,20,{&tile,1}),backend.SetFloat4(Stage::Vertex,21,{&mapping,1}),
		backend.SetFloat4(Stage::Vertex,234,{&fraction,1}),backend.SetFloat4(Stage::Pixel,20,{&burn,1}),
		backend.SetFloat4(Stage::Pixel,21,{&nature,1})})
		if (!status) throw std::runtime_error("terrain registers: "+status.message);
}
/** Independent byte-domain oracle preserves the original unweighted first-layer alpha. */
Pixel TerrainPixel(size_t layers, const std::array<Pixel,4>& textures, Pixel weights,
	Pixel day, Pixel night, float fraction, Pixel nature, Float4 selection, float burn) {
	const auto light=BakedLight(day,night,fraction);
	double natureWeight=0;
	for (size_t c=0; c<4; ++c) natureWeight+=nature[c]/255.*selection[c];
	Pixel result{};
	for (size_t c=0; c<4; ++c) {
		double value=textures[0][c]/255.*(c==3?1.:weights[0]/255.);
		for (size_t i=1; i<layers; ++i) value+=textures[i][c]/255.*weights[i]/255.;
		if (c<3) value*=light[c]*natureWeight*(1+burn*nature[3]/255.);
		result[c]=static_cast<unsigned char>(std::lround(std::clamp(value,0.,1.)*255));
	}
	return result;
}

/** Include every touched unit and borrowed object's parameters, including aliased names. */
std::vector<int64_t> TerrainSnapshot(const IndexedDraw& d) {
	auto result=Snapshot(d.terrainTextures[0]);
	GLint active=0; glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
	for (size_t unit=0; unit<5; ++unit) {
		glActiveTexture(GL_TEXTURE0+GLenum(unit));
		GLint binding=0,sampler=0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding); glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
		result.push_back(binding); result.push_back(sampler);
		const GLuint texture=unit==4?d.natureTexture:d.terrainTextures[unit];
		glBindTexture(GL_TEXTURE_2D,texture);
		for (GLenum parameter : {GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_MIN_FILTER,
			GL_TEXTURE_MAG_FILTER,GL_TEXTURE_BASE_LEVEL,GL_TEXTURE_MAX_LEVEL}) {
			GLint value=0; glGetTexParameteriv(GL_TEXTURE_2D,parameter,&value); result.push_back(value);
		}
		glBindTexture(GL_TEXTURE_2D,GLuint(binding));
	}
	glActiveTexture(GLenum(active));
	return result;
}

void TerrainTests(Checks& check, Host& host, const Bytes& fx, const Bytes& baked,
	const Bytes& terrain, ScenePrograms& late) {
	Target target; Mesh mesh; TerrainColors colors;
	Texture t0(true),t1(true),t2(true),t3(true),natureTexture(true),colored;
	const std::array<Pixel,4> texels{{{160,30,50,40},{20,180,60,50},{40,70,200,60},{140,110,30,70}}};
	const Pixel day{100,150,200,2}, night{200,90,30,3}, weights{20,40,80,115}, nature{80,140,210,60};
	const std::array<GLuint,4> textures{{t0.name,t1.name,t2.name,t3.name}};
	for (size_t i=0; i<4; ++i) SolidTexture(textures[i],texels[i]);
	SolidTexture(natureTexture.name,nature); colors.Fill(day,night); colors.Weights(weights);
	auto draw=mesh.Draw(0); colors.Bind(draw); draw.terrainTextures=textures; draw.natureTexture=natureTexture.name;
	const ByteView f{fx.data(),fx.size()},b{baked.data(),baked.size()},t{terrain.data(),terrain.size()};
	Result status; auto backend=ShaderBackend::Create({&host,Host::Current,FamilyLookup},{},status);
	check.Good(status,"terrain backend creation"); if (!backend) return;
	ScenePrograms programs;
	const unsigned compiles=familyCompiles,links=familyLinks;
	const auto initial=TerrainSnapshot(draw);
	status=backend->LoadSceneFamily(f,b,t,programs);
	check.Good(status,"atomic static plus eight ordinary terrain programs");
	if (!status) { check.Good(backend->Shutdown(),"failed terrain baseline shutdown"); return; }
	check.Test(familyCompiles-compiles==24 && familyLinks-links==12,"exact twelve programs compiled once");
	check.Test(TerrainSnapshot(draw)==initial,"scene load restores GL host state");
	for (const auto& ticket : SceneTickets(programs)) check.Test(!ticket.Empty(),"all twelve CPU tickets published");
	const unsigned loadedCompiles=familyCompiles,loadedLinks=familyLinks;
	TerrainRegisters(*backend);
	check.Test(backend->Draw(programs.statics.basicFx.simple,draw).code==Code::UninitializedRegisters,
		"terrain uploads do not initialize BasicFX WORLD");
	for (size_t slot=0; slot<8; ++slot) for (float fraction : {0.f,.4f,1.f}) {
		const Float4 selection{.2f,.5f,.3f,0};
		TerrainRegisters(*backend,fraction,.1f,selection,.7f); target.Clear();
		check.Good(backend->Draw(programs.terrain[slot],draw),"original terrain layer/day-night draw");
		PixelIs(check,32,32,TerrainPixel(slot/2+1,texels,weights,day,night,fraction,nature,selection,.7f),
			"layer order, BGRA weights, HDR decode, nature and multiplicative overburn");
		DepthIs(check,.25f,"terrain world-space D3D depth mapping");
	}
	check.Test(familyCompiles==loadedCompiles && familyLinks==loadedLinks,"terrain switching performs no compilation");
	// Each layer, including the fourth, receives its own sampler and original weight channel.
	for (size_t layer=0; layer<4; ++layer) {
		Pixel isolated{}; isolated[layer]=255; colors.Weights(isolated);
		TerrainRegisters(*backend); target.Clear();
		check.Good(backend->Draw(programs.terrain[6],draw),"isolated original terrain layer");
		PixelIs(check,32,32,TerrainPixel(4,texels,isolated,day,night,0,nature,{0,1,0,0},0),"independent terrain sampler and mask channel");
	}
	// Global zero tiling samples the repeating 2x2 seam; only the first local layer uses UV=.25.
	colors.Fill({255,255,255,1},{255,255,255,1}); colors.Weights({128,127,0,0});
	auto uvDraw=draw; uvDraw.terrainTextures.fill(colored.name); SolidTexture(natureTexture.name,{0,255,0,0});
	for (size_t slot : {size_t(0),size_t(1),size_t(2),size_t(3)}) {
		TerrainRegisters(*backend,0,0); target.Clear();
		check.Good(backend->Draw(programs.terrain[slot],uvDraw),"local versus global UV terrain draw");
		const auto value=ReadPixel(32,32);
		const int base=slot>=2?64:0;
		check.Test(std::abs(int(value[0])-(slot%2?128+base:64+base))<=2 &&
			std::abs(int(value[1])-(slot%2?base:64+base))<=2 &&
			std::abs(int(value[2])-(slot%2?base:64+base))<=2,"local UV affects layer zero only; global tiling is scalar c20.x");
	}
	colors.Fill(day,night); colors.Weights(weights); SolidTexture(natureTexture.name,nature);
	// Missing initialization is tracked per stage/register, and bad uploads invalidate all.
	for (unsigned missing : {8u,9u,10u,11u,20u,21u,234u}) {
		check.Test(!backend->SetFloat4(Stage::Vertex,256,{Identity.data(),1}),"invalid write resets terrain initialization");
		const Float4 value{1,1,1,1};
		for (unsigned reg : {8u,9u,10u,11u,20u,21u,234u}) if (reg!=missing)
			check.Good(backend->SetFloat4(Stage::Vertex,reg,{&value,1}),"partial terrain VS upload");
		check.Good(backend->SetFloat4(Stage::Pixel,20,{Identity.data(),2}),"partial terrain PS upload");
		check.Test(backend->Draw(programs.terrain[0],draw).code==Code::UninitializedRegisters,"each reflected terrain VS register required");
	}
	TerrainRegisters(*backend);
	// Global variants never read local UVs, including their layout and nonfinite data.
	{
		auto absent=draw; absent.uvOffset=SIZE_MAX;
		target.Clear(); check.Good(backend->Draw(programs.terrain[0],absent),"global terrain ignores absent UV layout");
		check.Test(backend->Draw(programs.terrain[1],absent).code==Code::InvalidInput,"local terrain requires valid UV layout");
		for (size_t i=0; i<4; ++i) { auto v=mesh.Get(i); v.uv[0]=NAN; mesh.Put(i,v); }
		target.Clear(); check.Good(backend->Draw(programs.terrain[0],draw),"global terrain ignores nonfinite unused UV");
		check.Test(backend->Draw(programs.terrain[1],draw).code==Code::InvalidInput,"local terrain rejects nonfinite UV");
		for (size_t i=0; i<4; ++i) { auto v=mesh.Get(i); v.uv[0]=.25f; mesh.Put(i,v); }
	}
	// Nature UV mapping and original N/A/B channel masks remain distinct from layer weights.
	{
		auto mapped=draw; mapped.natureTexture=colored.name;
		for (const Float4 selection : {Float4{1,0,0,0},Float4{0,1,0,0},Float4{0,0,1,0}}) {
			TerrainRegisters(*backend,0,.1f,selection,0,{0,0,.25f,.25f}); target.Clear();
			check.Good(backend->Draw(programs.terrain[7],mapped),"mapped nature sampler draw");
			PixelIs(check,32,32,TerrainPixel(4,texels,weights,day,night,0,{255,0,0,255},selection,0),"nature mapping and faction channel selection");
		}
	}
	// Shader-owned c0 and first-layer alpha must not be replaced by a WORLD or weight value.
	{
		const Float4 poison{91,93,95,97};
		check.Good(backend->SetFloat4(Stage::Vertex,0,{&poison,1}),"unused WORLD upload");
		for (int alpha : {39,40,41}) {
			TerrainRegisters(*backend); auto cut=draw;
			cut.alphaTest=AlphaTest::GreaterEqual; cut.alphaReference=alpha;
			target.Clear(); check.Good(backend->Draw(programs.terrain[0],cut),"terrain first-layer alpha test");
			PixelIs(check,32,32,alpha>40?ClearPixel:TerrainPixel(1,texels,weights,day,night,0,nature,{0,1,0,0},0),
				"terrain alpha is unweighted layer-zero alpha and c0 is shader-owned");
			DepthIs(check,alpha>40?1.f:.25f,"terrain alpha discard preserves depth");
		}
	}
	// Interleave every sibling program in the same owner, sharing register numbers but not semantics.
	for (const auto& ticket : StaticTickets(programs.statics)) {
		auto sibling=draw; sibling.texture=t0.name;
		Registers(*backend); BakedRegisters(*backend); Opacity(check,*backend,.5f);
		target.Clear(); check.Good(backend->Draw(ticket,sibling),"scene static sibling after terrain attributes");
		TerrainRegisters(*backend); target.Clear();
		check.Good(backend->Draw(programs.terrain[7],draw),"terrain after sibling register and attribute state");
		PixelIs(check,32,32,TerrainPixel(4,texels,weights,day,night,0,nature,{0,1,0,0},0),"terrain constants do not inherit static meanings");
	}
	// Host samplers, texture objects and active unit must survive both aliases and failures.
	{
		Foreign foreign(fx,mesh);
		for (size_t unit=0; unit<5; ++unit) {
			glActiveTexture(GL_TEXTURE0+GLenum(unit)); glBindTexture(GL_TEXTURE_2D,colored.name);
			glBindSampler(GLuint(unit),foreign.sampler);
		}
		glActiveTexture(GL_TEXTURE7); glUseProgram(foreign.program); glBindVertexArray(foreign.vao);
		glClampColor(GL_CLAMP_VERTEX_COLOR,GL_TRUE); glClampColor(GL_CLAMP_FRAGMENT_COLOR,GL_TRUE);
		glEnable(GL_VERTEX_PROGRAM_TWO_SIDE);
		for (bool aliased : {false,true}) {
			auto borrowed=draw;
			if (aliased) { borrowed.terrainTextures.fill(t0.name); borrowed.natureTexture=t0.name; }
			TerrainRegisters(*backend); target.Clear(); const auto before=TerrainSnapshot(borrowed);
			check.Good(backend->Draw(programs.terrain[7],borrowed),"terrain hostile host and aliased texture draw");
			check.Test(TerrainSnapshot(borrowed)==before,"all five texture units, aliases, samplers and HDR clamps restored");
		}
	}
	glDisable(GL_VERTEX_PROGRAM_TWO_SIDE); glClampColor(GL_CLAMP_VERTEX_COLOR,GL_FIXED_ONLY);
	glClampColor(GL_CLAMP_FRAGMENT_COLOR,GL_FIXED_ONLY); CleanRaster();
	for (size_t unit=0; unit<5; ++unit) glBindSampler(GLuint(unit),0);
	glActiveTexture(GL_TEXTURE0);
	// A later faction pass adds RGB while retaining the first pass's alpha and depth.
	TerrainRegisters(*backend); target.Clear();
	check.Good(backend->Draw(programs.terrain[7],draw),"terrain first nature pass");
	const Pixel first=ReadPixel(32,32); auto additive=draw; additive.blendMode=BlendMode::AddColor;
	const auto additiveStatus=backend->Draw(programs.terrain[7],additive);
	if (IndexedBlendAvailable()) {
		check.Good(additiveStatus,"terrain additive nature pass");
		Pixel sum=first; for (size_t c=0; c<3; ++c) sum[c]=static_cast<unsigned char>(std::min(255,int(first[c])*2));
		check.Test(Near(ReadPixel(32,32),sum,2),"terrain additive RGB preserves destination alpha");
		DepthIs(check,.25f,"terrain later pass preserves original depth");
	} else check.Test(additiveStatus.code==Code::Unsupported,"terrain additive pass requires indexed blending");
	// Malformed views fail before any draw, with untouched state and pixels.
	for (int kind=0; kind<13; ++kind) {
		auto bad=draw;
		if (kind==0) bad.terrainWeights.bytes={};
		if (kind==1) bad.terrainWeights.stride=3;
		if (kind==2) bad.terrainWeights.offset=SIZE_MAX;
		if (kind==3) bad.terrainWeights.bytes.size=15;
		if (kind==4) bad.dayColors.bytes={};
		if (kind==5) bad.nightColors.bytes={};
		if (kind>=6 && kind<=9) bad.terrainTextures[kind-6]=0;
		if (kind==10) bad.natureTexture=0;
		if (kind==11) bad.blendMode=BlendMode::LerpByAlpha;
		if (kind==12) bad.baseVertex=INT32_MAX;
		const auto before=TerrainSnapshot(draw); const auto draws=backend->GetDiagnostics().draws;
		const auto result=backend->Draw(programs.terrain[7],bad);
		check.Test(!result,"malformed terrain input rejected");
		check.Test(TerrainSnapshot(draw)==before && backend->GetDiagnostics().draws==draws,"terrain rejection is state-atomic");
	}
	{
		GLuint empty=0; glGenTextures(1,&empty); glBindTexture(GL_TEXTURE_2D,empty);
		auto bad=draw; bad.natureTexture=empty;
		const auto before=TerrainSnapshot(draw);
		check.Test(backend->Draw(programs.terrain[7],bad).code==Code::InvalidInput,"late sampler4 missing storage rejected");
		check.Test(TerrainSnapshot(draw)==before,"late sampler failure restores previously modified diffuse textures");
		glDeleteTextures(1,&empty);
	}
	{
		GLuint cube=0; glGenTextures(1,&cube); glBindTexture(GL_TEXTURE_CUBE_MAP,cube);
		auto bad=draw; bad.natureTexture=cube;
		const auto before=TerrainSnapshot(draw);
		check.Test(backend->Draw(programs.terrain[7],bad).code==Code::InvalidInput,"late wrong-target texture rejected");
		check.Test(glGetError()==GL_NO_ERROR,"wrong-target rejection consumes its own GL error");
		check.Test(TerrainSnapshot(draw)==before,"wrong-target failure restores all borrowed units");
		glDeleteTextures(1,&cube);
	}
	for (size_t slot : {size_t(0),size_t(1),size_t(2),size_t(3)}) {
		auto unused=draw; for (size_t i=slot/2+1; i<4; ++i) unused.terrainTextures[i]=UINT32_MAX;
		target.Clear(); check.Good(backend->Draw(programs.terrain[slot],unused),"unreferenced terrain samplers ignored");
	}
	late=programs;
	ScenePrograms wrong=programs;
	host.ReleaseCurrent();
	check.Test(backend->LoadSceneFamily(f,b,t,wrong).code==Code::WrongContext && SceneEmpty(wrong),"wrong-context scene reload only clears output");
	host.MakeCurrent();
	target.Clear(); check.Good(backend->Draw(programs.terrain[7],draw),"wrong-context attempt preserves active scene");
	check.Good(backend->Shutdown(),"scene shutdown");
	check.Test(backend->GetDiagnostics().liveBytes==0,"all twelve programs freed");
	backend.reset();
	// The aggregate archive cap includes all three sources, without overflowing on SIZE_MAX.
	{
		Limits limits; limits.archiveBytes=fx.size()+baked.size()+terrain.size()-1;
		backend=ShaderBackend::Create(host.Binding(),limits,status);
		check.Good(status,"scene aggregate limit fixture");
		if (backend) {
			check.Test(backend->LoadSceneFamily(f,b,t,programs).code==Code::InvalidInput && SceneEmpty(programs),
				"sum of three archives respects configured cap");
			check.Good(backend->Shutdown(),"scene aggregate limit shutdown"); backend.reset();
		}
	}
	// Each failure has a fresh budget; successful recovery may not resurrect earlier tickets.
	for (int kind=0; kind<6; ++kind) {
		backend=ShaderBackend::Create({&host,Host::Current,FamilyLookup},{},status);
		check.Good(status,"scene failure fixture creation"); if (!backend) break;
		check.Good(backend->LoadSceneFamily(f,b,t,programs),"scene before injected failure");
		const auto previous=SceneTickets(programs);
		ByteView broken=t;
		if (kind==0) broken={};
		if (kind==1) broken.size=SIZE_MAX;
		if (kind==2) failFamilyCompile=familyCompiles+9;
		if (kind==3) failFamilyCompile=familyCompiles+24;
		if (kind==4) failFamilyLink=familyLinks+5;
		if (kind==5) failFamilyLink=familyLinks+12;
		const auto before=TerrainSnapshot(draw);
		check.Test(!backend->LoadSceneFamily(f,b,broken,programs) && SceneEmpty(programs),"failed scene load publishes no partial family");
		failFamilyCompile=failFamilyLink=0;
		check.Test(TerrainSnapshot(draw)==before,"failed scene compilation restores host state");
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code==Code::InvalidReference,"failed scene load invalidates every old slot");
		check.Good(backend->LoadSceneFamily(f,b,t,programs),"scene recovery after failure");
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code==Code::InvalidReference,"recovery never resurrects old scene serial");
		TerrainRegisters(*backend); target.Clear(); check.Good(backend->Draw(programs.terrain[7],draw),"recovered scene draws");
		check.Good(backend->Shutdown(),"failed scene fixture shutdown");
		check.Test(backend->GetDiagnostics().liveBytes==0,"failed/recovered shader allocations released");
		backend.reset();
	}
}
