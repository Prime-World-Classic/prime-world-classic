/** Original baked triangle centres are independent reference points for live ground picking. */
static bool RunLinuxBakedPickingEngineProbe(const LinuxSelectedMapPreview& map)
{
	unsigned checks=0, failures=0;
	const auto check=[&](bool good,const char* label) {
		++checks; if (!good) { ++failures; fprintf(stderr,"Baked picking: %s\n",label); }
	};
	check(map.bakedTerrain && map.terrainHeightmap.ready,"original baked geometry and sampled legacy surface available");
	if (!map.bakedTerrain || !map.terrainHeightmap.ready) return false;
	check(map.terrainHeightmap.bakedSurface==map.bakedTerrain,"rendering, placement and picking retain one immutable source");
	for (size_t tileIndex : {1187u,818u,666u,845u}) {
		const auto* tile=map.bakedTerrain->GetTile(tileIndex);
		if (!tile) { check(false,"authored reference tile available"); continue; }
		CVec3 vertices[3]; bool found=false;
		for (size_t triangle=0; triangle<tile->triangleCount; ++triangle) {
			for (size_t corner=0; corner<3; ++corner) {
				std::uint32_t index=0;
				std::memcpy(&index,map.bakedTerrain->IndexData(tileIndex)+(triangle*3+corner)*4,4);
				std::memcpy(&vertices[corner],map.bakedTerrain->VertexData(tileIndex)+size_t(index)*44,12);
			}
			const double area=(double(vertices[1].x)-vertices[0].x)*(double(vertices[2].y)-vertices[0].y)-
				(double(vertices[1].y)-vertices[0].y)*(double(vertices[2].x)-vertices[0].x);
			if (std::abs(area)>.1) { found=true; break; }
		}
		check(found,"reference triangle has a nondegenerate ground footprint");
		if (!found) continue;
		const double x=(double(vertices[0].x)+vertices[1].x+vertices[2].x)/3;
		const double y=(double(vertices[0].y)+vertices[1].y+vertices[2].y)/3;
		const double z=(double(vertices[0].z)+vertices[1].z+vertices[2].z)/3;
		const double sampled=SampleLinuxMapTerrainHeightAtWorld(map.terrainHeightmap,float(x),float(y));
		fprintf(stdout,"Baked picking reference: tile=%zu point=%.6f,%.6f,%.6f sampled=%.6f\n",tileIndex,x,y,z,sampled);
		check(std::abs(sampled-z)<.001,"placement samples the authored triangle instead of the reduced heightfield");
		for (const auto size : {std::array<unsigned,2>{1280,720},{720,1280}})
		for (float yaw : {0.f,42.f,158.f,270.f}) for (float pitch : {35.f,56.f,72.f}) {
			LinuxClientLaunchSettings settings; settings.width=size[0]; settings.height=size[1];
			LinuxBootstrapScreenRuntime runtime; runtime.localMapCamera=true; runtime.localMapFocusHeight=float(z);
			runtime.mapPreviewZoom=8; runtime.mapPreviewRenderedYawDegrees=yaw; runtime.mapPreviewRenderedPitchDegrees=pitch;
			runtime.mapPreviewPanX=float((205-x)*(92./410.)); runtime.mapPreviewPanZ=float((205-y)*(92./410.));
			const auto camera=ResolveLinuxMapCameraFrame(settings,map,runtime);
			double sx=0,sy=0; const bool projected=camera.Project({x,y,z},sx,sy);
			check(projected,"actual rendered triangle centre projects inside local view");
			LinuxBootstrap::MapCameraFrame::Point hit;
			check(projected && TraceLinuxMapCameraTerrain(camera,map.terrainHeightmap,sx,sy,hit) &&
				std::abs(hit.x-x)<.001 && std::abs(hit.y-y)<.001 && std::abs(hit.z-z)<.001,
				"live ray reaches the same baked triangle at every rotation and aspect");
			int px=0,py=0;
			check(ProjectLinuxMapPreviewWorldToScreen(settings,map,runtime,CVec2(float(x),float(y)),&px,&py) &&
				std::abs(px-sx)<=.501 && std::abs(py-sy)<=.501,"ground target projection uses the same baked surface");
		}
	}
	{
		LinuxMapTerrainHeightmapPreview fallback=map.terrainHeightmap;
		fallback.ready=true; fallback.sampledWidth=fallback.sampledHeight=2;
		fallback.worldWidth=fallback.worldHeight=1000; fallback.sampledHeights={0,0,0,0};
		LinuxBootstrap::MapCameraFrame camera(0,0,410,410,1280,720,8,56,0,(205-800)*(92./410.),(205-800)*(92./410.),true,0);
		double x=0,y=0; LinuxBootstrap::MapCameraFrame::Point hit{1,2,3};
		check(camera.Project({800,800,0},x,y) && !TraceLinuxMapCameraTerrain(camera,fallback,x,y,hit) &&
			hit.x==1 && hit.y==2 && hit.z==3,"baked miss never invents a hit on the hidden legacy surface");
		fallback.bakedSurface.reset();
		check(TraceLinuxMapCameraTerrain(camera,fallback,x,y,hit),"maps without baked geometry retain legacy sampled picking");
	}
	{
		LinuxSelectedMapPreview fallback=map; fallback.terrainHeightmap.ready=false;
		LinuxClientLaunchSettings settings; settings.width=1280; settings.height=720;
		LinuxBootstrapScreenRuntime runtime; runtime.localMapCamera=true; runtime.mapPreviewZoom=8;
		runtime.mapPreviewRenderedPitchDegrees=56; runtime.mapPreviewRenderedYawDegrees=42;
		const auto a=ResolveLinuxMapSceneRequest(settings,map,runtime),b=ResolveLinuxMapSceneRequest(settings,fallback,runtime);
		check(a.Valid() && b.Valid() && a.bounds.minX==b.bounds.minX && a.bounds.maxX==b.bounds.maxX &&
			a.bounds.minY==b.bounds.minY && a.bounds.maxY==b.bounds.maxY,"baked visibility height bounds do not depend on a legacy heightmap");
	}
	fprintf(stdout,"Baked picking summary: checks=%u failures=%u\n",checks,failures);
	return failures==0;
}
