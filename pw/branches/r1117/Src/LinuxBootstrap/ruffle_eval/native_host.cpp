#include "native_host.h"
#include "bridge.h"
#include "gl_compositor.h"
#include <EGL/egl.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace
{
/** Ruffle owns an EGL context; never let its dispatch replace a current engine GLX context. */
class ContextScope final
{
public:
	ContextScope()
		: display_(glXGetCurrentDisplay()), context_(glXGetCurrentContext()),
		  draw_(glXGetCurrentDrawable()), read_(glXGetCurrentReadDrawable()), api_(eglQueryAPI())
	{
		if (eglGetCurrentContext() != EGL_NO_CONTEXT)
			throw std::runtime_error("Native Ruffle inspection requires GLX, not a caller EGL context");
		if (context_ && !glXMakeContextCurrent(display_, None, None, nullptr))
			throw std::runtime_error("Cannot detach the engine GLX context");
	}

	~ContextScope() { Restore(); }
	bool Restore()
	{
		if (restored_)
			return success_;
		restored_ = true;
		if (eglGetCurrentContext() != EGL_NO_CONTEXT)
			success_ = eglMakeCurrent(eglGetCurrentDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE;
		if (eglQueryAPI() != api_)
			success_ = (eglBindAPI(api_) == EGL_TRUE) && success_;
		if (context_)
			success_ = (glXMakeContextCurrent(display_, draw_, read_, context_) == True) && success_;
		return success_;
	}
private:
	Display* display_;
	GLXContext context_;
	GLXDrawable draw_, read_;
	EGLenum api_;
	bool restored_ = false;
	bool success_ = true;
};

template<class T> T Symbol(void* library, const char* name)
{
	dlerror();
	void* address = dlsym(library, name);
	const char* error = dlerror();
	if (error || !address)
		throw std::runtime_error(std::string("Missing native Ruffle export: ") + name);
	return reinterpret_cast<T>(address);
}
}

struct PwRuffleNativeHost::Impl
{
	void* library = nullptr;
	PwRuffleHost host = 0;
	decltype(&pw_ruffle_open) open = nullptr;
	decltype(&pw_ruffle_request) request = nullptr;
	decltype(&pw_ruffle_render) render = nullptr;
	decltype(&pw_ruffle_close) close = nullptr;
	decltype(&pw_ruffle_buffer_free) free = nullptr;
	PwRuffleGlCompositor compositor;
	unsigned width = 0, height = 0;
	bool poisoned = false;
	bool unloadUnsafe = false;

	/** All foreign buffers are released even if copying a diagnostic throws. */
	struct Buffer
	{
		PwRuffleBuffer bytes{};
		decltype(&pw_ruffle_buffer_free) free;
		explicit Buffer(decltype(free) release) : free(release) {}
		~Buffer() { free(&bytes); }
		std::string Text() const
		{
			return bytes.len ? std::string(reinterpret_cast<const char*>(bytes.data), bytes.len) : std::string();
		}
	};
};

PwRuffleNativeHost::PwRuffleNativeHost() : impl_(new Impl) {}
PwRuffleNativeHost::~PwRuffleNativeHost() { Reset(); }
bool PwRuffleNativeHost::IsReady() const { return impl_->host != 0 && !impl_->poisoned; }

bool PwRuffleNativeHost::Reset() noexcept
{
	impl_->compositor.Reset();
	if (impl_->unloadUnsafe) return false;
	if (impl_->host)
	{
		impl_->poisoned = true;
		try
		{
			ContextScope context;
			const int status = impl_->close(impl_->host);
			if (status == PW_RUFFLE_OK) impl_->host = 0;
			if (!context.Restore()) impl_->unloadUnsafe = true;
			if (status != PW_RUFFLE_OK || impl_->unloadUnsafe) return false;
		}
		catch (...) { return false; }
	}
	if (impl_->library)
		dlclose(impl_->library);
	impl_->library = nullptr;
	impl_->width = impl_->height = 0;
	impl_->poisoned = false;
	return true;
}

bool PwRuffleNativeHost::Open(const std::string& library, const std::string& data,
	const std::string& movie, std::string& error)
{
	if (!Reset()) { error = "Previous Ruffle teardown failed; library retained"; return false; }
	try
	{
		if (library.empty() || library.front() != '/')
			throw std::runtime_error("Ruffle library path must be absolute");
		impl_->library = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!impl_->library)
			throw std::runtime_error(dlerror());
		if (Symbol<decltype(&pw_ruffle_abi_version)>(impl_->library, "pw_ruffle_abi_version")() != 1)
			throw std::runtime_error("Unsupported Ruffle ABI version");
		impl_->open = Symbol<decltype(impl_->open)>(impl_->library, "pw_ruffle_open");
		impl_->request = Symbol<decltype(impl_->request)>(impl_->library, "pw_ruffle_request");
		impl_->render = Symbol<decltype(impl_->render)>(impl_->library, "pw_ruffle_render");
		impl_->close = Symbol<decltype(impl_->close)>(impl_->library, "pw_ruffle_close");
		impl_->free = Symbol<decltype(impl_->free)>(impl_->library, "pw_ruffle_buffer_free");
		const std::string config = nlohmann::json{{"data", data}, {"movie", movie}}.dump();
		Impl::Buffer diagnostic(impl_->free);
		ContextScope context;
		const int status = impl_->open(reinterpret_cast<const uint8_t*>(config.data()), config.size(), &impl_->host, &diagnostic.bytes);
		if (!context.Restore())
			throw std::runtime_error("Cannot restore engine GLX context after Ruffle open");
		if (status != PW_RUFFLE_OK)
			throw std::runtime_error("Ruffle open: " + diagnostic.Text());
		error.clear();
		return true;
	}
	catch (const std::exception& exception) { error = exception.what(); Reset(); return false; }
}

bool PwRuffleNativeHost::Request(const std::string& request, std::string& response, std::string& error)
{
	response.clear();
	try
	{
		if (!IsReady())
			throw std::runtime_error("Native Ruffle host is closed");
		Impl::Buffer buffer(impl_->free);
		ContextScope context;
		const int status = impl_->request(impl_->host, reinterpret_cast<const uint8_t*>(request.data()), request.size(), &buffer.bytes);
		if (status == PW_RUFFLE_PANIC) impl_->poisoned = true;
		if (!context.Restore())
			throw std::runtime_error("Cannot restore engine GLX context after Ruffle request");
		response = buffer.Text();
		if (status != PW_RUFFLE_OK)
			throw std::runtime_error("Ruffle request: " + response);
		const auto parsed = nlohmann::json::parse(request);
		if (parsed.value("action", std::string()) == "surface")
			impl_->width = impl_->height = 0;
		error.clear();
		return true;
	}
	catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool PwRuffleNativeHost::Draw(unsigned width, unsigned height, double deltaMs, std::string& error)
{
	try
	{
		if (!IsReady() || !glXGetCurrentContext())
			throw std::runtime_error("Draw requires an open host and a current GLX context");
		if (!width || !height || width > 4096 || height > 4096 || uint64_t(width) * height > 8 * 1024 * 1024)
			throw std::runtime_error("Invalid native Ruffle viewport");
		if (!std::isfinite(deltaMs) || deltaMs < 0 || deltaMs > 250)
			throw std::runtime_error("Invalid native Ruffle delta (0..250 ms)");
		std::string response;
		if (impl_->width != width || impl_->height != height)
		{
			if (!Request(nlohmann::json{{"action", "surface"}, {"width", width}, {"height", height}, {"transparent", true}}.dump(), response, error))
				return false;
			impl_->width = width;
			impl_->height = height;
		}
		if (!Request(nlohmann::json{{"action", "tick"}, {"delta_ms", deltaMs}}.dump(), response, error))
			return false;
		Impl::Buffer diagnostic(impl_->free), pixels(impl_->free);
		PwRuffleFrame frame{};
		ContextScope context;
		const int status = impl_->render(impl_->host, &frame, &diagnostic.bytes);
		if (status == PW_RUFFLE_PANIC) impl_->poisoned = true;
		pixels.bytes = frame.rgba;
		if (!context.Restore())
			throw std::runtime_error("Cannot restore engine GLX context after Ruffle render");
		if (status != PW_RUFFLE_OK)
			throw std::runtime_error("Ruffle render: " + diagnostic.Text());
		return impl_->compositor.Draw(frame, 0, 0, static_cast<int>(width), static_cast<int>(height), error);
	}
	catch (const std::exception& exception) { error = exception.what(); return false; }
}
