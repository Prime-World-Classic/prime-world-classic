/** Shared native camera construction; drawing and input use identical bounds and angles. */
static LinuxBootstrap::MapCameraFrame ResolveLinuxMapCameraFrame(
	const LinuxClientLaunchSettings& settings, const LinuxSelectedMapPreview& map,
	const LinuxBootstrapScreenRuntime& runtime)
{
	if (!map.tactical.ready || settings.width > INT_MAX || settings.height > INT_MAX) return {};
	const auto& bounds = map.tactical;
	return {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY, static_cast<int>(settings.width), static_cast<int>(settings.height),
		runtime.mapPreviewZoom, runtime.mapPreviewRenderedPitchDegrees, runtime.mapPreviewRenderedYawDegrees,
		runtime.mapPreviewPanX, runtime.mapPreviewPanZ, runtime.localMapCamera, runtime.localMapFocusHeight};
}

float SampleLinuxMapTerrainHeightAtWorld(const LinuxMapTerrainHeightmapPreview&, float, float);

/** Conservative visible XY bounds include terrain relief and tall authored props.
 * Project corner rays at both heights, rather than treating viewport pixels as map XY.
 */
static LinuxBootstrap::SceneWindowRequest ResolveLinuxMapSceneRequest(
	const LinuxClientLaunchSettings& settings, const LinuxSelectedMapPreview& map,
	const LinuxBootstrapScreenRuntime& runtime)
{
	LinuxBootstrap::SceneWindowRequest request;
	const auto camera = ResolveLinuxMapCameraFrame(settings, map, runtime);
	if (!camera.valid) return request;
	request.focusX = camera.centerX - camera.panX / camera.scale;
	request.focusY = camera.centerY - camera.panZ / camera.scale;
	request.bounds = {DBL_MAX, DBL_MAX, -DBL_MAX, -DBL_MAX};
	const bool surfaceReady=map.terrainHeightmap.ready || map.terrainHeightmap.bakedSurface;
	const double minHeight = surfaceReady ? map.terrainHeightmap.minHeight : 0;
	const double maxHeight = (surfaceReady ? map.terrainHeightmap.maxHeight : 0) + 32;
	for (const double x : {0., double(camera.width)})
	for (const double y : {0., double(camera.height)}) {
		LinuxBootstrap::MapCameraFrame::Point origin, direction;
		if (!camera.Ray(x, y, origin, direction) || std::abs(direction.z) < 1e-12) return {};
		for (const double height : {minHeight, maxHeight}) {
			const double t = (height - origin.z) / direction.z;
			const double wx = origin.x + t * direction.x, wy = origin.y + t * direction.y;
			request.bounds.minX = std::min(request.bounds.minX, wx);
			request.bounds.minY = std::min(request.bounds.minY, wy);
			request.bounds.maxX = std::max(request.bounds.maxX, wx);
			request.bounds.maxY = std::max(request.bounds.maxY, wy);
		}
	}
	return request;
}

/** Follow only the scheduler's own playing human. Losing ownership freezes the camera,
 * never retargeting a bot. Manual pan/minimap navigation detach follow until Home/reset.
 */
static void UpdateLinuxLocalMapCamera(const LinuxClientLaunchSettings& settings,
	const LinuxSelectedMapPreview& map, LinuxBootstrapScreenRuntime* runtime)
{
	if (!runtime) return;
	if (!IsLinuxWorldPresentationActive(runtime) || settings.bootstrapWorldDebug || !map.tactical.ready) {
		if (runtime->localMapCamera) {
			runtime->localMapCamera = false;
			runtime->localMapCameraWorld = nullptr;
			ResetLinuxMapPreviewCamera(runtime, "leave-local-view");
		}
		return;
	}
	auto* world = GetLinuxBootstrapRuntimeWorld(runtime);
	auto* player = !settings.spectator && !runtime->replayFileInputActive && world && runtime->localScheduler
		? world->GetPlayerByUID(runtime->localScheduler->GetMyClientID()) : nullptr;
	auto* hero = player && !player->IsBot() && player->IsPlaying() ? player->GetHero() : nullptr;
	if (!hero || !std::isfinite(hero->GetPosition().x) || !std::isfinite(hero->GetPosition().y)) return;
	const auto& bounds = map.tactical;
	const float range = std::max(1.f, std::max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY));
	const float scale = 92.f / range;
	if (!runtime->localMapCamera || runtime->localMapCameraWorld != world ||
		runtime->localMapCameraDescriptor != map.descriptor) {
		runtime->localMapCamera = true;
		runtime->localMapCameraWorld = world;
		runtime->localMapCameraDescriptor = map.descriptor;
		runtime->localMapFollow = true;
		runtime->mapPreviewZoom = (54.f * 1.38f) / (37.f * scale);
		runtime->mapPreviewYawDegrees = kLinuxMapPreviewDefaultYawDegrees;
		runtime->mapPreviewPitchDegrees = kLinuxMapPreviewDefaultPitchDegrees;
		runtime->mapPreviewLastAction = "local-hero-camera";
	}
	if (runtime->localMapFollow) {
		runtime->mapPreviewPanX = ((bounds.minX + bounds.maxX) * .5f - hero->GetPosition().x) * scale;
		runtime->mapPreviewPanZ = ((bounds.minY + bounds.maxY) * .5f - hero->GetPosition().y) * scale;
		++runtime->localMapFocusUpdates;
	}
	ClampLinuxMapPreviewCamera(runtime);
	runtime->mapPreviewPanX = (std::clamp)(runtime->mapPreviewPanX,
		-(bounds.maxX - bounds.minX) * .5f * scale, (bounds.maxX - bounds.minX) * .5f * scale);
	runtime->mapPreviewPanZ = (std::clamp)(runtime->mapPreviewPanZ,
		-(bounds.maxY - bounds.minY) * .5f * scale, (bounds.maxY - bounds.minY) * .5f * scale);
	const float focusX = (bounds.minX + bounds.maxX) * .5f - runtime->mapPreviewPanX / scale;
	const float focusY = (bounds.minY + bounds.maxY) * .5f - runtime->mapPreviewPanZ / scale;
	runtime->localMapFocusHeight = SampleLinuxMapTerrainHeightAtWorld(map.terrainHeightmap, focusX, focusY);
	runtime->mapPreviewRenderedPitchDegrees = runtime->mapPreviewPitchDegrees;
	runtime->mapPreviewRenderedYawDegrees = NormalizeLinuxPreviewYawValue(runtime->mapPreviewYawDegrees);
}

/** Nearest intersection with authored baked geometry, or the legacy sampled fallback.
 * Both are bounded, two-sided surface queries, not alpha/prop/road collision or GL readback.
 */
static bool TraceLinuxMapCameraTerrain(const LinuxBootstrap::MapCameraFrame& camera,
	const LinuxMapTerrainHeightmapPreview& terrain, double sx, double sy,
	LinuxBootstrap::MapCameraFrame::Point& hit)
{
	using Point = LinuxBootstrap::MapCameraFrame::Point;
	Point origin, direction;
	if (!camera.Ray(sx, sy, origin, direction) || std::abs(direction.z) < 1e-12) return false;
	if (terrain.bakedSurface) {
		LinuxBootstrap::BakedTerrain::SurfaceHit surface;
		if (!terrain.bakedSurface->Trace({origin.x,origin.y,origin.z},{direction.x,direction.y,direction.z},440,surface))
			return false;
		hit={surface.position[0],surface.position[1],surface.position[2]}; return true;
	}
	if (!terrain.ready) {
		const double t = -origin.z / direction.z;
		if (t < 0 || t > 440.) return false;
		hit = {origin.x + t * direction.x, origin.y + t * direction.y, 0};
		return true;
	}
	const size_t w = terrain.sampledWidth, h = terrain.sampledHeight;
	if (w < 2 || h < 2 || w > 256 || h > 256 || terrain.sampledHeights.size() != w * h) return false;
	const double worldWidth = terrain.worldWidth > 0 ? terrain.worldWidth : terrain.sourceWidth;
	const double worldHeight = terrain.worldHeight > 0 ? terrain.worldHeight : terrain.sourceHeight;
	if (!std::isfinite(worldWidth) || !std::isfinite(worldHeight) || worldWidth <= 0 || worldHeight <= 0) return false;
	const double dx = worldWidth / (w - 1), dy = worldHeight / (h - 1);
	double nearest = 441.;
	for (size_t y = 0; y + 1 < h; ++y)
	for (size_t x = 0; x + 1 < w; ++x) {
		const double h00 = terrain.sampledHeights[y * w + x] + .04 / camera.scale;
		const double h10 = terrain.sampledHeights[y * w + x + 1] + .04 / camera.scale;
		const double h01 = terrain.sampledHeights[(y + 1) * w + x] + .04 / camera.scale;
		const double h11 = terrain.sampledHeights[(y + 1) * w + x + 1] + .04 / camera.scale;
		if (!std::isfinite(h00) || !std::isfinite(h10) || !std::isfinite(h01) || !std::isfinite(h11)) return false;
		for (bool upper : {false, true}) {
			const double a = (upper ? h11 - h01 : h10 - h00) / dx;
			const double b = (upper ? h11 - h10 : h01 - h00) / dy;
			const double c = upper ? h11 - a * ((x + 1) * dx) - b * ((y + 1) * dy)
				: h00 - a * (x * dx) - b * (y * dy);
			const double denominator = direction.z - a * direction.x - b * direction.y;
			if (std::abs(denominator) < 1e-12) continue;
			const double t = (a * origin.x + b * origin.y + c - origin.z) / denominator;
			if (!std::isfinite(t) || t < 0 || t > 440. || t >= nearest) continue;
			const double tx = (origin.x + t * direction.x) / dx - x;
			const double ty = (origin.y + t * direction.y) / dy - y;
			const double epsilon = 1e-7;
			if (tx < -epsilon || tx > 1 + epsilon || ty < -epsilon || ty > 1 + epsilon ||
				(upper ? tx + ty < 1 - epsilon : tx + ty > 1 + epsilon)) continue;
			nearest = t;
		}
	}
	if (nearest > 440.) return false;
	hit = {origin.x + nearest * direction.x, origin.y + nearest * direction.y, origin.z + nearest * direction.z};
	return true;
}

/** Ground picks share the displayed local camera and selected terrain surface. */
bool ProjectLinuxMapPreviewScreenToWorld(const LinuxClientLaunchSettings& settings,
	const LinuxSelectedMapPreview& map, const LinuxBootstrapScreenRuntime& runtime,
	int mouseX, int mouseY, float* worldX, float* worldY, bool clampToMap = true)
{
	if (worldX) *worldX = 0;
	if (worldY) *worldY = 0;
	const auto camera = ResolveLinuxMapCameraFrame(settings, map, runtime);
	LinuxBootstrap::MapCameraFrame::Point hit;
	if (!camera.valid || (runtime.localMapCamera && (mouseX < 0 || mouseY < 0 ||
		mouseX >= camera.width || mouseY >= camera.height))) return false;
	// Keep the legacy overview's planar selection contract; live play uses rendered terrain.
	LinuxMapTerrainHeightmapPreview flat;
	if (!TraceLinuxMapCameraTerrain(camera, runtime.localMapCamera ? map.terrainHeightmap : flat, mouseX, mouseY, hit))
		return false;
	const auto& bounds = map.tactical;
	if (!clampToMap && (hit.x < bounds.minX || hit.x >= bounds.maxX || hit.y < bounds.minY || hit.y >= bounds.maxY)) return false;
	if (worldX) *worldX = static_cast<float>((std::clamp)(hit.x, double(bounds.minX), double(bounds.maxX)));
	if (worldY) *worldY = static_cast<float>((std::clamp)(hit.y, double(bounds.minY), double(bounds.maxY)));
	return true;
}

/** Direct forward projection rejects offscreen targets instead of snapping to an edge. */
bool ProjectLinuxMapPreviewWorldToScreen(const LinuxClientLaunchSettings& settings,
	const LinuxSelectedMapPreview& map, const LinuxBootstrapScreenRuntime& runtime,
	const CVec2& position, int* screenX, int* screenY)
{
	if (screenX) *screenX = 0;
	if (screenY) *screenY = 0;
	const auto camera = ResolveLinuxMapCameraFrame(settings, map, runtime);
	const double height = runtime.localMapCamera && (map.terrainHeightmap.ready || map.terrainHeightmap.bakedSurface)
		? SampleLinuxMapTerrainHeightAtWorld(map.terrainHeightmap, position.x, position.y) +
			(map.terrainHeightmap.bakedSurface ? 0 : .04 / camera.scale) : 0.;
	double x = 0, y = 0;
	if (!camera.Project({position.x, position.y, height}, x, y)) return false;
	const auto ix = static_cast<int>(std::lround(x)), iy = static_cast<int>(std::lround(y));
	if (ix < 0 || ix >= settings.width || iy < 0 || iy >= settings.height) return false;
	if (screenX) *screenX = ix;
	if (screenY) *screenY = iy;
	return true;
}
