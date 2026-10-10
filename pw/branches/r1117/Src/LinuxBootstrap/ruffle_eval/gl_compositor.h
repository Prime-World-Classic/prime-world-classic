#ifndef PW_RUFFLE_EVAL_GL_COMPOSITOR_H
#define PW_RUFFLE_EVAL_GL_COMPOSITOR_H

#include "bridge.h"
#include <string>

/** Shader-free RGBA overlay for a current desktop OpenGL 2.1+ compatibility context.
 *
 * Construction makes no GL calls. Draw lazily creates one private texture and
 * reuses it, reallocating storage when frame dimensions change. Keep the same
 * context current for Draw, Reset, and destruction after the first Draw. Reset
 * before destroying/replacing the context; an empty destructor makes no GL calls.
 * This object is neither copyable nor thread-safe and never owns the frame bytes.
 * No GLX, Rust, DSO, buffer-free, swap, or frame-advance calls are made here.
 *
 * Call outside glBegin/glEnd, display-list compilation, queries, and transform
 * feedback, with a complete single-color-target framebuffer and GL_RENDER mode.
 * The caller supplies valid readable frame memory and a clean GL error queue.
 * GL errors are consumed and reported, not restored. One free server and client
 * attribute-stack entry is required; matrix-stack space is not required.
 * Core/ES contexts are unsupported. Extension state unrelated to this legacy
 * draw path is not managed (for example conditional rendering and clip control).
 */
class PwRuffleGlCompositor final
{
public:
	PwRuffleGlCompositor() = default;
	PwRuffleGlCompositor(const PwRuffleGlCompositor&) = delete;
	PwRuffleGlCompositor& operator=(const PwRuffleGlCompositor&) = delete;
	/** Release the private texture; its creating context must still be current. */
	~PwRuffleGlCompositor();

	/** Composite the entire frame into a bottom-left-origin destination rectangle.
	 * Source rows are top-down straight-alpha RGBA8. Sampling is nearest-neighbor;
	 * no color-space conversion is applied. RGB uses SRC_ALPHA/ONE_MINUS_SRC_ALPHA;
	 * alpha uses ONE/ONE_MINUS_SRC_ALPHA (source-over, not squared source alpha).
	 *
	 * Source dimensions must be 1..8192, stride exactly width*4, length exactly
	 * stride*height and at most 64 MiB. Destination dimensions must be 1..8192,
	 * with x/y within +/-1,000,000. Driver texture/viewport limits also apply.
	 * Scissor/depth/stencil tests and host texture/shader effects are bypassed.
	 * Viewport, masks, blending, program, fixed-function texture units, matrices,
	 * unpack PBO, pixel-store and pixel-transfer state are restored on return.
	 * The framebuffer binding and draw/read buffers are left as the caller set them.
	 *
	 * @return True on success (error cleared), false with a diagnostic otherwise.
	 * Malformed frames/rectangles are rejected before any GL call. GPU failure
	 * may leave destination pixels partially drawn; host state is still restored.
	 */
	bool Draw(const PwRuffleFrame& frame, int x, int y, int width, int height, std::string& error);

	/** Delete the private texture in its current context; idempotent and reusable. */
	void Reset() noexcept;

private:
	unsigned int texture_ = 0;
	uint32_t width_ = 0;
	uint32_t height_ = 0;
};

#endif
