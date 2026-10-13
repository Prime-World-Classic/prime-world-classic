/** Shipped sky bytecode, original RGB arithmetic and post-opaque depth ownership. */
void SkyRegisters(ShaderBackend& backend, Float4 uv={0,0,0,0}, Float4 mul={1,1,1,1},
	Float4 add={0,0,0,0}, const std::array<Float4,4>& world=Identity) {
	for (auto status : {backend.SetFloat4(Stage::Vertex,0,{world.data(),3}),
		backend.SetFloat4(Stage::Vertex,8,{Identity.data(),4}), backend.SetFloat4(Stage::Vertex,20,{&uv,1}),
		backend.SetFloat4(Stage::Pixel,20,{&mul,1}), backend.SetFloat4(Stage::Pixel,21,{&add,1})})
		if (!status) throw std::runtime_error("sky registers: "+status.message);
}

void SkyTests(Checks& check, Host& host, const Bytes& fx, const Bytes& baked, const Bytes& terrain,
	const Bytes& sky, ProgramRef& late) {
	Target target; Mesh mesh; Texture texture, solid(true);
	const ByteView f{fx.data(),fx.size()}, b{baked.data(),baked.size()}, t{terrain.data(),terrain.size()}, s{sky.data(),sky.size()};
	Result status; auto backend=ShaderBackend::Create({&host,Host::Current,FamilyLookup},{},status);
	check.Good(status,"sky backend creation"); if (!backend) return;
	ScenePrograms programs;
	const unsigned compiles=familyCompiles, links=familyLinks;
	const auto initial=Snapshot(texture.name);
	status=backend->LoadSceneFamily(f,b,t,s,programs);
	check.Good(status,"atomic thirteen-program scene with original sky");
	if (!status) { check.Good(backend->Shutdown(),"sky baseline shutdown"); return; }
	check.Test(familyCompiles-compiles==26 && familyLinks-links==13,"exact thirteen programs compiled once");
	check.Test(!programs.sky.Empty() && !SceneEmpty(programs),"sky and existing scene tickets published");
	check.Test(Snapshot(texture.name)==initial,"sky family load restores host state");
	auto draw=mesh.Draw(texture.name);
	check.Test(backend->Draw(programs.sky,draw).code==Code::UninitializedRegisters,"sky requires original constants");
	for (unsigned missing : {0u,1u,2u,8u,9u,10u,11u,20u}) {
		backend->SetFloat4(Stage::Vertex,256,{Identity.data(),1});
		for (unsigned reg : {0u,1u,2u,8u,9u,10u,11u,20u}) if (reg!=missing)
			check.Good(backend->SetFloat4(Stage::Vertex,reg,{Identity.data(),1}),"partial sky VS upload");
		check.Good(backend->SetFloat4(Stage::Pixel,20,{Identity.data(),2}),"partial sky PS upload");
		check.Test(backend->Draw(programs.sky,draw).code==Code::UninitializedRegisters,"each reflected sky VS register required");
	}
	SkyRegisters(*backend); target.Clear();
	check.Good(backend->Draw(programs.sky,draw),"original sky without caller c3");
	PixelIs(check,32,32,{255,0,0,255},"sky original UV without BasicFX V addition");
	DepthIs(check,1,"sky never writes scene depth");
	SkyRegisters(*backend,{.5f,0,57,61}); target.Clear();
	check.Good(backend->Draw(programs.sky,draw),"sky horizontal curve sample");
	PixelIs(check,32,32,{0,255,0,255},"sky first UV pair controls original sampler");
	SkyRegisters(*backend,{0,.5f,0,0}); target.Clear();
	check.Good(backend->Draw(programs.sky,draw),"sky vertical curve sample");
	PixelIs(check,32,32,{0,0,255,255},"sky V offset not inverted");
	SolidTexture(solid.name,{80,100,120,70}); draw.texture=solid.name;
	SkyRegisters(*backend,{0,0,0,0},{.5f,1,1.5f,0},{.1f,0,.2f,1}); target.Clear();
	check.Good(backend->Draw(programs.sky,draw),"sky RGB multiply/add");
	PixelIs(check,32,32,{66,100,231,70},"sky preserves diffuse alpha regardless of output constant alpha");
	DepthIs(check,1,"colored sky still read-only");
	// A nearer opaque quad survives the sky; a later opaque draw still sees original depth.
	Registers(*backend); target.Clear(); mesh.Z(.1f,.1f); draw=mesh.Draw(texture.name);
	check.Good(backend->Draw(programs.statics.basicFx.simple,draw),"opaque foreground before sky");
	SkyRegisters(*backend,{.5f,0,0,0}); mesh.Z(.5f,.5f); draw=mesh.Draw(texture.name);
	check.Good(backend->Draw(programs.sky,draw),"sky after opaque foreground");
	PixelIs(check,32,32,{255,0,0,255},"opaque foreground occludes farther sky");
	DepthIs(check,.1f,"sky retains opaque depth");
	target.Clear(); mesh.Z(.1f,.1f); draw=mesh.Draw(texture.name);
	check.Good(backend->Draw(programs.sky,draw),"sky before later object");
	Registers(*backend,Identity,Identity,{0,.5f,0,0}); mesh.Z(.5f,.5f); draw=mesh.Draw(texture.name);
	check.Good(backend->Draw(programs.statics.basicFx.simple,draw),"later object sees no sky depth");
	PixelIs(check,32,32,{0,0,255,255},"sky cannot occlude later object");
	DepthIs(check,.5f,"later object writes its own depth");
	// Preview cameras may be outside the sky enclosure: reserve it for clear depth only.
	draw.skyAtFarDepth=true; mesh.Z(0,0); draw.vertices={mesh.vertices.data(),mesh.vertices.size()};
	SkyRegisters(*backend,{.5f,0,0,0}); const auto backgroundState=Snapshot(texture.name);
	check.Good(backend->Draw(programs.sky,draw),"explicit preview far-depth sky");
	PixelIs(check,32,32,{0,0,255,255},"near sky geometry cannot cover existing opaque pixels in preview mode");
	DepthIs(check,.5f,"preview far-depth retains opaque depth");
	check.Test(Snapshot(texture.name)==backgroundState,"preview sky restores the original depth range");
	target.Clear(); check.Good(backend->Draw(programs.sky,draw),"preview sky fills untouched background");
	PixelIs(check,32,32,{0,255,0,255},"preview far-depth retains original texture and UV arithmetic");
	DepthIs(check,1,"preview far-depth never writes depth");
	draw.skyAtFarDepth=false;
	SkyRegisters(*backend); mesh.Z(.25f,.25f); draw=mesh.Draw(texture.name);
	for (auto blend : {BlendMode::LerpByAlpha,BlendMode::AddColor,BlendMode::MulColor}) {
		draw.blendMode=blend;
		check.Test(backend->Draw(programs.sky,draw).code==Code::Unsupported,"sky rejects non-original blending");
	}
	draw.blendMode=BlendMode::Off;
	for (unsigned count : {0u,3u,14u}) {
		draw.textureMipLevels=count;
		check.Test(!backend->Draw(programs.sky,draw),"invalid or missing sky mips rejected");
	}
	draw.textureMipLevels=2;
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture.name);
	const Pixel mip{90,120,180,80};
	glTexImage2D(GL_TEXTURE_2D,1,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,mip.data());
	for (size_t i=0; i<4; ++i) { auto v=mesh.Get(i); v.uv[0]=v.position[0]*256; v.uv[1]=v.position[1]*256; mesh.Put(i,v); }
	target.Clear(); const auto before=Snapshot(texture.name);
	check.Good(backend->Draw(programs.sky,draw),"sky authored diffuse mip sampling");
	check.Test(Snapshot(texture.name)==before,"sky draw restores texture and depth state");
	PixelIs(check,32,32,mip,"sky selects authored terminal mip without regeneration");
	DepthIs(check,1,"mipmapped sky preserves depth");
	ScenePrograms wrong=programs;
	host.ReleaseCurrent();
	check.Test(backend->LoadSceneFamily(f,b,t,s,wrong).code==Code::WrongContext && SceneEmpty(wrong) && wrong.sky.Empty(),
		"wrong-context sky reload clears outputs only");
	host.MakeCurrent(); target.Clear();
	check.Good(backend->Draw(programs.sky,draw),"wrong-context reload preserves original active sky");
	Code threadCode=Code::Ok;
	std::thread worker([&] { ScenePrograms out; threadCode=backend->LoadSceneFamily(f,b,t,s,out).code; }); worker.join();
	check.Test(threadCode==Code::WrongThread,"sky family rejects wrong-thread reload");
	check.Test(familyCompiles-compiles==26 && familyLinks-links==13,"sky draws do not compile");
	late=programs.sky;
	ScenePrograms legacy;
	check.Good(backend->LoadSceneFamily(f,b,t,legacy),"legacy twelve-program load remains available");
	check.Test(legacy.sky.Empty(),"legacy scene has no sky ticket");
	check.Test(backend->Draw(late,draw).code==Code::InvalidReference,"legacy reload invalidates sky ticket");
	check.Good(backend->LoadSceneFamily(f,b,t,s,programs),"sky reload after legacy family");
	late=programs.sky;
	check.Test(!backend->LoadSceneFamily(f,b,t,{},programs),"missing sky archive fails atomically");
	check.Test(SceneEmpty(programs) && programs.sky.Empty(),"failed sky load clears all outputs");
	check.Test(backend->Draw(late,draw).code==Code::InvalidReference,"failed sky load invalidates tickets");
	check.Good(backend->Shutdown(),"sky backend shutdown");
	check.Test(backend->GetDiagnostics().liveBytes==0,"sky parser ownership balanced");
	backend.reset();
	Limits limits; limits.archiveBytes=fx.size()+baked.size()+terrain.size()+sky.size()-1;
	backend=ShaderBackend::Create(host.Binding(),limits,status);
	check.Good(status,"sky aggregate limit fixture");
	if (backend) {
		check.Test(backend->LoadSceneFamily(f,b,t,s,programs).code==Code::InvalidInput && SceneEmpty(programs) && programs.sky.Empty(),
			"all four archives share the configured size cap");
		check.Good(backend->Shutdown(),"sky aggregate limit shutdown"); backend.reset();
	}
	for (int kind=0; kind<4; ++kind) {
		backend=ShaderBackend::Create({&host,Host::Current,FamilyLookup},{},status);
		check.Good(status,"sky injected failure fixture"); if (!backend) break;
		check.Good(backend->LoadSceneFamily(f,b,t,s,programs),"sky family before injected failure");
		auto previous=SceneTickets(programs); previous.push_back(programs.sky);
		ByteView broken=s;
		if (kind==0) broken.size=SIZE_MAX;
		if (kind==1) failFamilyCompile=familyCompiles+25;
		if (kind==2) failFamilyCompile=familyCompiles+26;
		if (kind==3) failFamilyLink=familyLinks+13;
		const auto before=Snapshot(texture.name);
		check.Test(!backend->LoadSceneFamily(f,b,t,broken,programs) && SceneEmpty(programs) && programs.sky.Empty(),
			"late sky failure cannot publish a partial family");
		failFamilyCompile=failFamilyLink=0;
		check.Test(Snapshot(texture.name)==before,"failed sky load restores GL host state");
		for (const auto& ticket : previous) check.Test(backend->Draw(ticket,draw).code==Code::InvalidReference,
			"late sky failure invalidates all thirteen old tickets");
		check.Good(backend->LoadSceneFamily(f,b,t,s,programs),"sky recovery after failure");
		SkyRegisters(*backend); target.Clear(); check.Good(backend->Draw(programs.sky,draw),"recovered sky draws");
		check.Good(backend->Shutdown(),"failed sky fixture shutdown");
		check.Test(backend->GetDiagnostics().liveBytes==0,"late sky failure releases every shader allocation"); backend.reset();
	}
}
