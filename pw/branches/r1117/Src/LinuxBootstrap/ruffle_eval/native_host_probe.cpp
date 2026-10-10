#include "native_host.h"
#include <EGL/egl.h>
#include <GL/glx.h>
#include <nlohmann/json.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Check(bool condition, const std::string& error)
{
	if (!condition) throw std::runtime_error(error);
}

/** Own a real GLX drawable before the host, so its texture is destroyed first. */
struct WindowContext
{
	Display* display = nullptr;
	Window window = 0;
	Colormap colormap = 0;
	GLXContext context = nullptr;
	void Open()
	{
		display = XOpenDisplay(nullptr);
		Check(display != nullptr, "DISPLAY unavailable");
		int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
		XVisualInfo* visual = glXChooseVisual(display, DefaultScreen(display), attributes);
		Check(visual != nullptr, "No GLX visual");
		colormap = XCreateColormap(display, RootWindow(display, visual->screen), visual->visual, AllocNone);
		XSetWindowAttributes settings{};
		settings.colormap = colormap;
		settings.override_redirect = True;
		window = XCreateWindow(display, RootWindow(display, visual->screen), 0, 0, 1280, 720, 0,
			visual->depth, InputOutput, visual->visual, CWColormap | CWOverrideRedirect, &settings);
		context = glXCreateContext(display, visual, nullptr, True);
		XFree(visual);
		Check(window && context, "Cannot create GLX context");
		XStoreName(display, window, "Prime World native Ruffle context probe");
		XMapWindow(display, window);
		XSync(display, False);
		Check(glXMakeCurrent(display, window, context), "Cannot activate GLX context");
		glDrawBuffer(GL_BACK);
		glReadBuffer(GL_BACK);
	}
	~WindowContext()
	{
		if (context) { glXMakeCurrent(display, None, nullptr); glXDestroyContext(display, context); }
		if (window) XDestroyWindow(display, window);
		if (colormap) XFreeColormap(display, colormap);
		if (display) XCloseDisplay(display);
	}
	void CheckCurrent() const
	{
		Check(glXGetCurrentContext() == context && glXGetCurrentDisplay() == display &&
			glXGetCurrentDrawable() == window && glXGetCurrentReadDrawable() == window,
			"GLX context or draw/read drawable was not restored");
		Check(eglGetCurrentContext() == EGL_NO_CONTEXT, "Ruffle left an EGL context current");
		Check(glGetString(GL_VERSION) != nullptr, "GL dispatch was not restored");
		Check(glGetError() == GL_NO_ERROR, "Unexpected host GL error");
	}
};
}

/** Exercise real SWF -> EGL readback -> GLX composition, failure recovery, and repeated teardown. */
int main(int argc, char** argv)
{
	try
	{
		Check(argc == 4, "Usage: PrimeWorldRuffleNativeHostProbe LIBRARY DATA COMBAT_SWF");
		WindowContext window;
		window.Open();
		PwRuffleNativeHost host;
		std::string error, response;
		Check(!host.Open("relative.so", argv[2], argv[3], error), "Accepted relative library");
		Check(!host.Open("/nonexistent/pw-ruffle.so", argv[2], argv[3], error), "Accepted missing library");
		Check(!host.Request("{}", response, error), "Accepted request while closed");
		window.CheckCurrent();
		for (int cycle = 0; cycle < 2; ++cycle)
		{
			Check(host.Open(argv[1], argv[2], argv[3], error), error);
			window.CheckCurrent();
			Check(host.Request(R"({"path":"LocalizationResources","method":"LocalizationComplete","args":[]})", response, error), error);
			Check(host.Request(R"({"path":"mainInterface","method":"HideAllWindows","args":[]})", response, error), error);
			Check(host.Request(R"({"action":"step","frames":3})", response, error), error);
			Check(!host.Request("invalid JSON", response, error), "Accepted malformed JSON");
			window.CheckCurrent();
			Check(!host.Draw(0, 720, 16, error), "Accepted invalid viewport");
			Check(!host.Draw(1280, 720, -1, error), "Accepted negative delta");
			for (const auto size : {std::array<unsigned, 2>{640, 480}, {257, 193}, {1280, 720}})
			{
				glDisable(GL_SCISSOR_TEST);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glClearColor(17 / 255.f, 203 / 255.f, 71 / 255.f, 1);
				glClear(GL_COLOR_BUFFER_BIT);
				glViewport(3, 7, 109, 113);
				glEnable(GL_SCISSOR_TEST);
				glScissor(1, 2, 11, 13);
				const EGLenum api = eglQueryAPI();
				Check(host.Draw(size[0], size[1], 16, error), error);
				window.CheckCurrent();
				Check(eglQueryAPI() == api, "EGL client API changed");
				GLint viewport[4];
				glGetIntegerv(GL_VIEWPORT, viewport);
				Check(viewport[0] == 3 && viewport[1] == 7 && viewport[2] == 109 && viewport[3] == 113 && glIsEnabled(GL_SCISSOR_TEST), "Host GL state changed");
				std::vector<uint8_t> pixels(size[0] * size[1] * 4);
				glReadPixels(0, 0, size[0], size[1], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
				size_t background = 0;
				for (size_t i = 0; i < pixels.size(); i += 4)
					if (pixels[i] == 17 && pixels[i + 1] == 203 && pixels[i + 2] == 71) ++background;
				std::cout << "cycle=" << cycle << " size=" << size[0] << 'x' << size[1] << " background=" << background << '\n';
				Check(background > 100 && background < size[0] * size[1] - 100, "Missing transparent background or rendered SWF pixels");
			}
			Check(host.Request(R"({"action":"stats"})", response, error), error);
			Check(nlohmann::json::parse(response).at("runtime_errors") == 0, "SWF runtime errors");
			Check(host.Reset(), "Host close failed");
			Check(host.Reset(), "Repeated close failed");
			Check(!host.IsReady(), "Host remained open");
			window.CheckCurrent();
		}
		std::cout << "Native Ruffle/GLX context probe PASS\n";
		return 0;
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
