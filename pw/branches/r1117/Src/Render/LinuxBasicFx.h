#pragma once

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include "../LinuxBootstrap/native_shader/shader_backend.hpp"
#include "../LinuxBootstrap/native_shader/shader_transforms.hpp"

struct Matrix43;
namespace NDb { struct Material; }
namespace Render {
class Texture;
namespace LinuxBasicFx {

struct Statistics {
	uint64_t selections = 0, unsupported = 0, draws = 0, rejected = 0, loads = 0;
};

/** Host lifecycle: call once after GLX creation, and shut down before destroying it. */
bool BeginContext();
bool Shutdown();
/** Batch selection is cleared even for unsupported/null materials. No pointer is retained.
 * Pair-zero materials support byte GEQUAL alpha testing and optional backface culling.
 * Non-Off blend modes require the retained transparent pass and preserve alpha/depth.
 * Malformed admitted states remain selected but cannot draw or fall back to the old preview.
 */
void ClearSelection();
void Select(const NDb::Material* material, const Texture* texture);
bool IsSelected();
/** Classify the same BasicFX subset as Select, without loading textures or GL.
 * Malformed non-Off modes also require deferral so Draw can reject them without opaque fallback.
 */
bool IsTransparentMaterial(const NDb::Material* material);
/** Enable only during the retained, depth-sorted per-view transparent pass; clears selection. */
void SetTransparentDrawingEnabled(bool enabled);
/** Draw raw engine buffers. A selected failure must not fall back to unshaded geometry. */
bool Draw(pwshader::IndexedDraw draw, const Matrix43* world, const pwshader::PreviewTransform& preview);
Statistics GetStatistics();

}
}
#endif
