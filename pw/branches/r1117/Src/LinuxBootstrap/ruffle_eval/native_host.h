#ifndef PW_RUFFLE_EVAL_NATIVE_HOST_H
#define PW_RUFFLE_EVAL_NATIVE_HOST_H

#include <memory>
#include <cstddef>
#include <cstdint>
#include <string>

/** Optional Linux inspection host. Loads an explicit native DSO, never Wine.
 * All methods and destruction belong to the constructing thread. Draw requires
 * one current desktop compatibility GLX context; keep that same context current
 * through Reset/destruction. No engine or AVM2 pointers cross the DSO boundary.
 * Requests and frame capture temporarily detach GLX while Ruffle uses EGL, then
 * restore the caller's GLX context/drawables and EGL API selection.
 */
class PwRuffleNativeHost final
{
public:
	PwRuffleNativeHost();
	~PwRuffleNativeHost();
	PwRuffleNativeHost(const PwRuffleNativeHost&) = delete;
	PwRuffleNativeHost& operator=(const PwRuffleNativeHost&) = delete;

	/** Open an absolute DSO path and the specified SWF; failure leaves no live host. */
	bool Open(const std::string& library, const std::string& data, const std::string& movie, std::string& error);
	/** Execute a JSON request once. Response is copied before its foreign buffer is freed. */
	bool Request(const std::string& request, std::string& response, std::string& error);
	/** Replace rooted BitmapData pixels without JSON/base64 or shared GPU objects.
	 * The input is borrowed for this call; the native ABI validates shape and handle.
	 * Preserves the caller's GLX context on both success and rejection.
	 */
	bool UploadBitmap(uint64_t bitmap, unsigned width, unsigned height,
		const uint8_t* pixels, size_t length, std::string& error);
	/** Resize transparently, advance 0..250 ms, and composite into the current framebuffer.
	 * Uses bounded CPU readback; this is an inspection path, not a performance backend.
	 * Does not swap buffers, route game input, or interpret queued FSCommands.
	 */
	bool Draw(unsigned width, unsigned height, double deltaMs, std::string& error);
	/** Close before destroying GLX. On failed teardown, retain the DSO instead of
	 * unloading code potentially still in use; destruction deliberately leaks that
	 * loader reference. Returns false when teardown cannot be confirmed.
	 */
	bool Reset() noexcept;
	bool IsReady() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

#endif
