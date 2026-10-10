#ifndef PW_RUFFLE_EVAL_BRIDGE_H
#define PW_RUFFLE_EVAL_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Evaluation ABI version 1. Independent of Tamarin, STL, Rust, and renderer pointers. */
typedef uint64_t PwRuffleHost;

/** Owned UTF-8 bytes, not NUL terminated. Free exactly once with pw_ruffle_buffer_free.
 * Do not copy ownership, modify data/len, or pass an already-owned buffer as output.
 */
typedef struct PwRuffleBuffer {
	uint8_t* data;
	size_t len;
} PwRuffleBuffer;

/** Owned top-down straight-alpha RGBA8, tightly packed (stride == width * 4).
 * Free rgba with pw_ruffle_buffer_free on all return paths. No GPU objects escape.
 */
typedef struct PwRuffleFrame {
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	PwRuffleBuffer rgba;
} PwRuffleFrame;

enum PwRuffleStatus {
	PW_RUFFLE_OK = 0,
	PW_RUFFLE_INVALID_ARGUMENT = 1,
	PW_RUFFLE_INVALID_HOST = 2,
	PW_RUFFLE_ERROR = 3,
	PW_RUFFLE_BUSY = 4,
	PW_RUFFLE_PANIC = 5
};

/** Return the exact ABI version. All functions are local, native calls. */
uint32_t pw_ruffle_abi_version(void);

/** Open a host from UTF-8 JSON {"data":"...", "movie":"..."}; maximum 64 KiB.
 * All pointers must denote valid memory of the specified length. Outputs must be
 * writable and non-aliasing. Host stays on its creating thread, including close.
 * output is zero on failure; diagnostic is owned even for a recoverable error.
 */
int32_t pw_ruffle_open(const uint8_t* config, size_t len, PwRuffleHost* output, PwRuffleBuffer* diagnostic);

/** Execute one JSON request exactly once; return a fresh owned JSON response.
 * Requests support invoke/get/set, rooted object handles, release/clear, step,
 * callback polling, stats, and offscreen PNG capture. IDs in JSON are strings.
 * No user callback is called here. Reentrancy is rejected with BUSY. After PANIC,
 * close the host; do not continue using potentially poisoned runtime state.
 */
int32_t pw_ruffle_request(PwRuffleHost host, const uint8_t* request, size_t len, PwRuffleBuffer* response);

/** Render and copy one frame without advancing time. Outputs must be valid,
 * initially unowned and non-aliasing. Diagnostic uses normal buffer ownership.
 * Additive ABI v1 export; older v1 libraries may not provide this symbol.
 */
int32_t pw_ruffle_render(PwRuffleHost host, PwRuffleFrame* frame, PwRuffleBuffer* diagnostic);

/** Replace a complete rooted BitmapData with tightly packed straight-alpha RGBA8.
 * Dimensions must match the existing bitmap and be 1..2048. Data is borrowed
 * only during the call; len must equal width*height*4. Disposed, stale, foreign,
 * or wrong-type handles fail. Existing display users retain the same identity.
 * Additive ABI v1 export; no engine/GPU/Tamarin pointers cross the boundary.
 */
int32_t pw_ruffle_bitmap_upload(PwRuffleHost host, uint64_t bitmap, uint32_t width, uint32_t height,
	const uint8_t* data, size_t len, PwRuffleBuffer* diagnostic);

/** Release all roots/runtime state. Unknown, stale, or wrong-thread IDs fail. */
int32_t pw_ruffle_close(PwRuffleHost host);

/** Free a buffer from this library, then clear both fields. A cleared buffer is OK.
 * The pointer must be valid; freeing forged/modified/copied ownership is undefined.
 */
void pw_ruffle_buffer_free(PwRuffleBuffer* buffer);

#ifdef __cplusplus
}
#endif
#endif
