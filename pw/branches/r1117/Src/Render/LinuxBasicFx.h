#pragma once

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include "../LinuxBootstrap/native_shader/shader_backend.hpp"
#include "../LinuxBootstrap/native_shader/shader_transforms.hpp"

struct Matrix43;
namespace NDb { struct Material; struct SkyMaterial; }
namespace Render {
class Texture;
class MeshVertexColors;
namespace LinuxBasicFx {

struct Statistics {
	/** Aggregate bridge counts; bakedDraws is the successful baked subset of draws. */
	uint64_t selections = 0, unsupported = 0, draws = 0, rejected = 0, loads = 0;
	uint64_t bakedSelections = 0, bakedUnsupported = 0, bakedDraws = 0, bakedRejected = 0;
	uint64_t terrainDraws = 0, terrainRejected = 0;
	uint64_t skyDraws = 0, skyRejected = 0;
};

/** Host lifecycle: call once after GLX creation, and shut down before destroying it. */
bool BeginContext();
bool Shutdown();
/** Batch selection is cleared even for unsupported/null materials. No pointer is retained.
 * Constant Simple/AdditiveOpacity/BlendOpacity materials retain authored blend/alpha state.
 * Simple ignores opacity; the other programs consume finite opacity*masterOpacity in PS c6.
 * No animated constants, emissive, skin, vertex-color, distortion or runtime fade overrides.
 * Family materials support byte GEQUAL alpha testing and optional backface culling.
 * Non-Off blend modes require the retained transparent pass and preserve alpha/depth.
 * Malformed admitted states remain selected but cannot draw or fall back to the old preview.
 */
void ClearSelection();
void Select(const NDb::Material* material, const Texture* texture);
/** Select original opaque BasicMaterial24 after mesh/material preparation.
 * Non-BasicMaterial calls leave a sibling BasicFX selection unchanged. Null colours or
 * unsupported material features clear a BasicMaterial selection and retain preview fallback.
 * Supplied malformed colours/state remain selected but fail closed at Draw. Both raw BGRA
 * buffers are borrowed through the following Draw only; the mesh must retain their owner.
 * Night fraction is captured from the current SceneConstants, not cached with the material.
 */
void SelectBaked(const NDb::Material* material, const Texture* texture,
	const MeshVertexColors* colors, float nightFraction);
/** Explicit post-opaque sky selection; texture is already admitted/uploaded by the map.
 * Original curve lookup/clamp/wrap is reused at the supplied presentation time. Invalid
 * material/curve state stays selected and fails closed, never drawing opaque fallback.
 */
void SelectSky(const NDb::SkyMaterial& material, uint32_t texture, unsigned mipLevels, float elapsed);
bool IsSelected();
/** Classify the same BasicFX subset as Select, without loading textures or GL.
 * Malformed non-Off modes also require deferral so Draw can reject them without opaque fallback.
 */
bool IsTransparentMaterial(const NDb::Material* material);
/** Enable only during the retained, depth-sorted per-view transparent pass; clears selection. */
void SetTransparentDrawingEnabled(bool enabled);
/** Draw raw engine buffers. A selected failure must not fall back to unshaded geometry. */
bool Draw(pwshader::IndexedDraw draw, const Matrix43* world, const pwshader::PreviewTransform& preview);

/** Original ordinary terrain constants. World-space vertices need no WORLD matrix. */
struct TerrainConstants {
	unsigned layers = 0;
	bool localUV = false;
	float tiling = .1f / 1.7f, nightFraction = 0, overburn = 0;
	pwshader::Float4 natureMapping{}, natureWeight{0,1,0,0};
};
/** Borrowed terrain draw in the same shader/context owner as static materials.
 * Clears material selection; never falls back to the preview on a failed admitted draw.
 * The caller supplies budget-admitted textures, all three color streams and nature-pass
 * raster state. Finite constants, layer count, streams and textures are checked before submission.
 */
bool DrawTerrain(pwshader::IndexedDraw draw, const TerrainConstants& constants,
	const pwshader::PreviewTransform& preview);
/** Preflight a mixed-nature tile before its first pass; no geometry is submitted. */
bool SupportsTerrainAdditive();
Statistics GetStatistics();

}
}
#endif
