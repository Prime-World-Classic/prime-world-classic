#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include "gl_compositor.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	/** Keep checks active under NDEBUG and report a bounded, useful failure. */
	void Check(bool condition, const std::string& message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	/** Standalone native window/context; destroyed after all context-owned objects. */
	struct GlxWindow
	{
		Display* display = nullptr;
		Window window = 0;
		Colormap colormap = 0;
		GLXContext context = nullptr;

		/** Create a small double-buffered RGBA compatibility drawable, without a loop. */
		void Open()
		{
			display = XOpenDisplay(nullptr);
			Check(display != nullptr, "Cannot open DISPLAY");
			int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
				GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, None};
			XVisualInfo* visual = glXChooseVisual(display, DefaultScreen(display), attributes);
			Check(visual != nullptr, "No RGBA8/depth/stencil GLX visual");
			colormap = XCreateColormap(display, RootWindow(display, visual->screen), visual->visual, AllocNone);
			XSetWindowAttributes settings{};
			settings.colormap = colormap;
			settings.override_redirect = True;
			window = XCreateWindow(display, RootWindow(display, visual->screen), 0, 0, 96, 80, 0,
				visual->depth, InputOutput, visual->visual, CWColormap | CWOverrideRedirect, &settings);
			context = glXCreateContext(display, visual, nullptr, True);
			XFree(visual);
			Check(window != 0 && context != nullptr, "Cannot create GLX window/context");
			XStoreName(display, window, "Ruffle compositor mock probe");
			XMapWindow(display, window);
			XSync(display, False);
			Check(glXMakeCurrent(display, window, context) == True, "Cannot make GLX context current");
			glDrawBuffer(GL_BACK);
			glReadBuffer(GL_BACK);
		}

		/** Resize only this probe's drawable; no host or game window is involved. */
		void Resize()
		{
			XResizeWindow(display, window, 112, 96);
			XSync(display, False);
		}

		~GlxWindow()
		{
			if (display && context)
			{
				glXMakeCurrent(display, None, nullptr);
				glXDestroyContext(display, context);
			}
			if (display && window) XDestroyWindow(display, window);
			if (display && colormap) XFreeColormap(display, colormap);
			if (display) XCloseDisplay(display);
		}
	};

	/** Probe optional modern bindings without requiring more than desktop GL 2.1. */
	struct Features
	{
		bool rectangle, samplers, pipelines, srgb, discard;
		GLint units = 0;

		Features()
		{
			int major = 0, minor = 0;
			const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
			Check(version && std::sscanf(version, "%d.%d", &major, &minor) == 2, "Invalid desktop GL version");
			Check(major > 2 || (major == 2 && minor >= 1), "OpenGL 2.1 required");
			const char* raw = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
			const std::string extensions = std::string(" ") + (raw ? raw : "") + " ";
			const auto has = [&](const char* name) { return extensions.find(std::string(" ") + name + " ") != std::string::npos; };
			rectangle = major > 3 || (major == 3 && minor >= 1) || has("GL_ARB_texture_rectangle");
			samplers = major > 3 || (major == 3 && minor >= 3) || has("GL_ARB_sampler_objects");
			pipelines = major > 4 || (major == 4 && minor >= 1) || has("GL_ARB_separate_shader_objects");
			srgb = major >= 3 || has("GL_EXT_framebuffer_sRGB") || has("GL_ARB_framebuffer_sRGB");
			discard = major >= 3 || has("GL_EXT_transform_feedback");
			glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
			Check(units >= 2, "Probe needs two fixed-function texture units");
		}
	};

	/** Independent state snapshot, including each fixed-function unit's matrix/bindings. */
	struct Snapshot
	{
		std::vector<double> values;

		/** Capture scalar/vector integer state without assuming contiguous enum values. */
		void Integer(GLenum name, size_t count = 1)
		{
			GLint data[4]{};
			glGetIntegerv(name, data);
			for (size_t i = 0; i < count; ++i) values.push_back(data[i]);
		}

		/** Capture a matrix, color, or other floating-point state exactly as returned. */
		void Real(GLenum name, size_t count)
		{
			GLdouble data[16]{};
			glGetDoublev(name, data);
			values.insert(values.end(), data, data + count);
		}

		explicit Snapshot(const Features& features)
		{
			for (GLenum name : {GL_CURRENT_PROGRAM, GL_ACTIVE_TEXTURE, GL_CLIENT_ACTIVE_TEXTURE, GL_MATRIX_MODE,
				GL_ATTRIB_STACK_DEPTH, GL_CLIENT_ATTRIB_STACK_DEPTH, GL_MODELVIEW_STACK_DEPTH, GL_PROJECTION_STACK_DEPTH,
				GL_DRAW_BUFFER, GL_READ_BUFFER, GL_PIXEL_UNPACK_BUFFER_BINDING, GL_PIXEL_PACK_BUFFER_BINDING,
				GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_PIXELS,
				GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST,
				GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_PIXELS,
				GL_PACK_SKIP_ROWS, GL_PACK_SKIP_IMAGES, GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST,
				GL_DEPTH_WRITEMASK, GL_DEPTH_FUNC, GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA,
				GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA,
				GL_STENCIL_FUNC, GL_STENCIL_REF, GL_STENCIL_VALUE_MASK, GL_STENCIL_WRITEMASK,
				GL_STENCIL_FAIL, GL_STENCIL_PASS_DEPTH_FAIL, GL_STENCIL_PASS_DEPTH_PASS,
				GL_STENCIL_BACK_FUNC, GL_STENCIL_BACK_REF, GL_STENCIL_BACK_VALUE_MASK, GL_STENCIL_BACK_WRITEMASK,
				GL_STENCIL_BACK_FAIL, GL_STENCIL_BACK_PASS_DEPTH_FAIL, GL_STENCIL_BACK_PASS_DEPTH_PASS,
				GL_CULL_FACE_MODE, GL_FRONT_FACE, GL_SHADE_MODEL, GL_ALPHA_TEST_FUNC, GL_LOGIC_OP_MODE, GL_MAP_COLOR}) Integer(name);
			Integer(GL_VIEWPORT, 4);
			Integer(GL_SCISSOR_BOX, 4);
			Integer(GL_COLOR_WRITEMASK, 4);
			Integer(GL_POLYGON_MODE, 2);
			Real(GL_MODELVIEW_MATRIX, 16);
			Real(GL_PROJECTION_MATRIX, 16);
			Real(GL_DEPTH_RANGE, 2);
			Real(GL_CURRENT_COLOR, 4);
			Real(GL_BLEND_COLOR, 4);
			Real(GL_COLOR_CLEAR_VALUE, 4);
			for (GLenum name : {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE,
				GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS, GL_ALPHA_TEST_REF}) Real(name, 1);
			for (GLenum cap : {GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_BLEND, GL_CULL_FACE,
				GL_LIGHTING, GL_FOG, GL_ALPHA_TEST, GL_COLOR_LOGIC_OP, GL_COLOR_SUM, GL_DITHER,
				GL_POLYGON_STIPPLE, GL_POLYGON_SMOOTH, GL_POLYGON_OFFSET_FILL, GL_MULTISAMPLE,
				GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_ALPHA_TO_ONE, GL_SAMPLE_COVERAGE}) values.push_back(glIsEnabled(cap));
			GLint planes = 0, active = 0;
			glGetIntegerv(GL_MAX_CLIP_PLANES, &planes);
			glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
			for (GLint i = 0; i < planes; ++i) values.push_back(glIsEnabled(GL_CLIP_PLANE0 + i));
			if (features.pipelines) Integer(GL_PROGRAM_PIPELINE_BINDING);
			if (features.srgb) values.push_back(glIsEnabled(GL_FRAMEBUFFER_SRGB));
			if (features.discard) values.push_back(glIsEnabled(GL_RASTERIZER_DISCARD));
			for (GLint unit = 0; unit < features.units; ++unit)
			{
				glActiveTexture(GL_TEXTURE0 + unit);
				for (GLenum name : {GL_TEXTURE_BINDING_1D, GL_TEXTURE_BINDING_2D, GL_TEXTURE_BINDING_3D,
					GL_TEXTURE_BINDING_CUBE_MAP, GL_TEXTURE_STACK_DEPTH}) Integer(name);
				if (features.rectangle) Integer(GL_TEXTURE_BINDING_RECTANGLE);
				if (features.samplers) Integer(GL_SAMPLER_BINDING);
				for (GLenum cap : {GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP,
					GL_TEXTURE_GEN_S, GL_TEXTURE_GEN_T, GL_TEXTURE_GEN_R, GL_TEXTURE_GEN_Q}) values.push_back(glIsEnabled(cap));
				if (features.rectangle) values.push_back(glIsEnabled(GL_TEXTURE_RECTANGLE));
				GLint environment = 0, filter = 0;
				glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &environment);
				glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
				values.push_back(environment);
				values.push_back(filter);
				Real(GL_TEXTURE_MATRIX, 16);
				Real(GL_CURRENT_TEXTURE_COORDS, 4);
			}
			glActiveTexture(static_cast<GLenum>(active));
			Check(glGetError() == GL_NO_ERROR, "Snapshot generated a GL error");
		}

		/** Compare without tolerances: untouched GL state should round-trip exactly. */
		void RequireEqual(const Snapshot& other) const
		{
			Check(values.size() == other.values.size(), "Snapshot shape changed");
			for (size_t i = 0; i < values.size(); ++i)
				Check(values[i] == other.values[i], "GL state scalar " + std::to_string(i) + " changed from " +
					std::to_string(values[i]) + " to " + std::to_string(other.values[i]));
		}
	};

	/** Compile a test-only hostile host shader; the compositor has no shaders. */
	GLuint MakeProgram()
	{
		const GLuint program = glCreateProgram();
		for (const auto& stage : {std::make_pair(GL_VERTEX_SHADER, "#version 120\nvoid main(){gl_Position=ftransform();}"),
			std::make_pair(GL_FRAGMENT_SHADER, "#version 120\nvoid main(){gl_FragColor=vec4(1.0,0.0,1.0,1.0);}")})
		{
			const GLuint shader = glCreateShader(stage.first);
			const char* source = stage.second;
			glShaderSource(shader, 1, &source, nullptr);
			glCompileShader(shader);
			GLint compiled = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
			Check(compiled == GL_TRUE, "Host-state fixture shader compilation failed");
			glAttachShader(program, shader);
			glDeleteShader(shader);
		}
		glLinkProgram(program);
		GLint linked = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);
		Check(linked == GL_TRUE, "Host-state fixture program link failed");
		return program;
	}

	/** Own deliberately hostile host resources, independent of the compositor texture. */
	struct HostState
	{
		GLuint program = 0, unpack = 0, pack = 0, sampler = 0, pipeline = 0;
		std::vector<GLuint> textures;

		/** Seed nondefault shader, texture, matrix, pixel, clipping and write state. */
		void Configure(const Features& features)
		{
			std::vector<GLenum> targets{GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP};
			if (features.rectangle) targets.push_back(GL_TEXTURE_RECTANGLE);
			textures.resize(features.units * targets.size());
			glGenTextures(static_cast<GLsizei>(textures.size()), textures.data());
			for (GLint unit = 0; unit < features.units; ++unit)
			{
				glActiveTexture(GL_TEXTURE0 + unit);
				for (size_t i = 0; i < targets.size(); ++i) glBindTexture(targets[i], textures[unit * targets.size() + i]);
				const uint8_t green[] = {0, 255, 0, 255};
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, green);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glEnable(GL_TEXTURE_2D);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glMatrixMode(GL_TEXTURE);
				glLoadIdentity();
				glTranslated(unit + 0.25, 0.5, 0.75);
				glMultiTexCoord4f(GL_TEXTURE0 + unit, 0.25f, 0.5f, 0.75f, 2);
			}
			glActiveTexture(GL_TEXTURE0);
			for (GLenum cap : {GL_TEXTURE_1D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP,
				GL_TEXTURE_GEN_S, GL_TEXTURE_GEN_T, GL_TEXTURE_GEN_R, GL_TEXTURE_GEN_Q}) glEnable(cap);
			if (features.rectangle) glEnable(GL_TEXTURE_RECTANGLE);
			if (features.samplers)
			{
				glGenSamplers(1, &sampler);
				glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
				glBindSampler(0, sampler);
			}
			if (features.pipelines)
			{
				glGenProgramPipelines(1, &pipeline);
				glBindProgramPipeline(pipeline);
			}
			program = MakeProgram();
			glUseProgram(program);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glTranslated(3, 4, 5);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glScaled(2, 3, 4);
			glViewport(3, 5, 23, 29);
			glScissor(0, 0, 1, 1);
			glDepthFunc(GL_NEVER);
			glDepthMask(GL_TRUE);
			glDepthRange(0.2, 0.8);
			glStencilFuncSeparate(GL_FRONT, GL_NEVER, 3, 0x35);
			glStencilFuncSeparate(GL_BACK, GL_EQUAL, 5, 0x73);
			glStencilMaskSeparate(GL_FRONT, 0x55);
			glStencilMaskSeparate(GL_BACK, 0xaa);
			glStencilOpSeparate(GL_FRONT, GL_INCR, GL_DECR, GL_REPLACE);
			glStencilOpSeparate(GL_BACK, GL_ZERO, GL_INVERT, GL_KEEP);
			glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
			glColor4f(0.25f, 0.5f, 0.75f, 0.25f);
			glBlendColor(0.25f, 0.5f, 0.75f, 0.5f);
			glBlendFuncSeparate(GL_DST_COLOR, GL_ONE, GL_ZERO, GL_DST_ALPHA);
			glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
			glDisable(GL_BLEND);
			glCullFace(GL_FRONT_AND_BACK);
			glFrontFace(GL_CW);
			glShadeModel(GL_FLAT);
			glPolygonMode(GL_FRONT, GL_LINE);
			glPolygonMode(GL_BACK, GL_POINT);
			glAlphaFunc(GL_NEVER, 0.5f);
			glLogicOp(GL_XOR);
			for (GLenum cap : {GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_ALPHA_TEST, GL_CULL_FACE,
				GL_LIGHTING, GL_FOG, GL_COLOR_LOGIC_OP, GL_COLOR_SUM, GL_POLYGON_STIPPLE, GL_POLYGON_SMOOTH,
				GL_POLYGON_OFFSET_FILL, GL_DITHER, GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE,
				GL_SAMPLE_ALPHA_TO_ONE, GL_SAMPLE_COVERAGE}) glEnable(cap);
			const GLdouble plane[] = {0, 0, 0, -1};
			glClipPlane(GL_CLIP_PLANE0, plane);
			glEnable(GL_CLIP_PLANE0);
			if (features.srgb) glEnable(GL_FRAMEBUFFER_SRGB);
			if (features.discard) glEnable(GL_RASTERIZER_DISCARD);
			glGenBuffers(1, &unpack);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
			glBufferData(GL_PIXEL_UNPACK_BUFFER, 4096, nullptr, GL_STATIC_DRAW);
			glGenBuffers(1, &pack);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, pack);
			glBufferData(GL_PIXEL_PACK_BUFFER, 4096, nullptr, GL_STATIC_READ);
			for (GLenum name : {GL_UNPACK_ALIGNMENT, GL_PACK_ALIGNMENT}) glPixelStorei(name, 8);
			for (GLenum name : {GL_UNPACK_ROW_LENGTH, GL_PACK_ROW_LENGTH}) glPixelStorei(name, 17);
			for (GLenum name : {GL_UNPACK_IMAGE_HEIGHT, GL_PACK_IMAGE_HEIGHT}) glPixelStorei(name, 19);
			for (GLenum name : {GL_UNPACK_SKIP_ROWS, GL_PACK_SKIP_ROWS}) glPixelStorei(name, 2);
			for (GLenum name : {GL_UNPACK_SKIP_PIXELS, GL_PACK_SKIP_PIXELS}) glPixelStorei(name, 3);
			for (GLenum name : {GL_UNPACK_SKIP_IMAGES, GL_PACK_SKIP_IMAGES}) glPixelStorei(name, 1);
			for (GLenum name : {GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST, GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST}) glPixelStorei(name, GL_TRUE);
			glPixelTransferi(GL_MAP_COLOR, GL_TRUE);
			for (GLenum name : {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE}) glPixelTransferf(name, 0.5f);
			for (GLenum name : {GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS}) glPixelTransferf(name, 0.25f);
			glActiveTexture(GL_TEXTURE0 + features.units - 1);
			glClientActiveTexture(GL_TEXTURE1);
			glMatrixMode(GL_TEXTURE);
			Check(glGetError() == GL_NO_ERROR, "Cannot configure hostile host state");
		}

		~HostState()
		{
			glUseProgram(0);
			if (program && glIsProgram(program)) glDeleteProgram(program);
			if (pipeline) { glBindProgramPipeline(0); glDeleteProgramPipelines(1, &pipeline); }
			if (sampler) glDeleteSamplers(1, &sampler);
			glDeleteBuffers(1, &unpack);
			glDeleteBuffers(1, &pack);
			glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
		}
	};

	using Pixel = std::array<uint8_t, 4>;
	constexpr Pixel Background{32, 64, 96, 128};

	/** Clear the drawable without letting the hostile write/clip state affect it. */
	void Clear(const Features& features)
	{
		glPushAttrib(GL_ALL_ATTRIB_BITS);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DITHER);
		if (features.srgb) glDisable(GL_FRAMEBUFFER_SRGB);
		if (features.discard) glDisable(GL_RASTERIZER_DISCARD);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);
		glStencilMask(~0u);
		glClearColor(Background[0] / 255.f, Background[1] / 255.f, Background[2] / 255.f, Background[3] / 255.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		glPopAttrib();
		Check(glGetError() == GL_NO_ERROR, "Clear failed");
	}

	/** Read one raw RGBA pixel, temporarily neutralizing pack PBO/store/transfer state. */
	Pixel ReadPixel(int x, int y)
	{
		GLint pack = 0, unpack = 0;
		glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
		glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
		glPushAttrib(GL_PIXEL_MODE_BIT);
		glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		for (GLenum name : {GL_PACK_ROW_LENGTH, GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS,
			GL_PACK_SKIP_IMAGES, GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST}) glPixelStorei(name, 0);
		glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
		for (GLenum name : {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE}) glPixelTransferf(name, 1);
		for (GLenum name : {GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS}) glPixelTransferf(name, 0);
		Pixel pixel{};
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
		glPopClientAttrib();
		glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(pack));
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack));
		glPopAttrib();
		Check(glGetError() == GL_NO_ERROR, "Pixel readback failed");
		return pixel;
	}

	/** Compare all four channels, allowing only one byte of implementation rounding. */
	void ExpectPixel(int x, int y, const Pixel& expected)
	{
		const Pixel actual = ReadPixel(x, y);
		for (size_t channel = 0; channel < actual.size(); ++channel)
			Check(std::abs(int(actual[channel]) - int(expected[channel])) <= 1,
				"Pixel (" + std::to_string(x) + "," + std::to_string(y) + ") channel " + std::to_string(channel) +
				": got " + std::to_string(actual[channel]) + ", expected " + std::to_string(expected[channel]));
	}

	/** Compute independent source-over expectations, including destination alpha. */
	Pixel Over(const Pixel& source)
	{
		Pixel result{};
		for (size_t i = 0; i < 3; ++i)
			result[i] = static_cast<uint8_t>((source[i] * source[3] + Background[i] * (255 - source[3]) + 127) / 255);
		result[3] = static_cast<uint8_t>(source[3] + (Background[3] * (255 - source[3]) + 127) / 255);
		return result;
	}

	/** Sequence the draw before reading its diagnostic; argument order is unspecified. */
	void Draw(PwRuffleGlCompositor& compositor, const PwRuffleFrame& frame,
		int x, int y, int width, int height)
	{
		std::string error = "stale diagnostic";
		const bool drawn = compositor.Draw(frame, x, y, width, height, error);
		Check(drawn, "Draw failed: " + error);
		Check(error.empty(), "Success did not clear the diagnostic");
	}

	/** Verify rejection performs no GL calls, by preserving a pending GL error too. */
	void Reject(PwRuffleGlCompositor& compositor, const Features& features, const PwRuffleFrame& frame,
		int x = 11, int y = 9, int width = 40, int height = 36)
	{
		const Snapshot before(features);
		glEnable(static_cast<GLenum>(0xffffffffu));
		std::string error;
		Check(!compositor.Draw(frame, x, y, width, height, error) && !error.empty(), "Malformed input accepted");
		Check(glGetError() == GL_INVALID_ENUM, "Malformed input touched the GL error queue");
		before.RequireEqual(Snapshot(features));
	}

	/** Exercise exact ABI layout bounds with fake pointers that must never be read. */
	void Malformed(PwRuffleGlCompositor& compositor, const Features& features, const PwRuffleFrame& valid)
	{
		for (const PwRuffleFrame& invalid : std::vector<PwRuffleFrame>{
			{}, {0, 2, 0, valid.rgba}, {2, 0, 8, valid.rgba}, {2, 2, 7, valid.rgba}, {2, 2, 12, valid.rgba},
			{2, 2, 8, {nullptr, 16}}, {2, 2, 8, {valid.rgba.data, 15}}, {2, 2, 8, {valid.rgba.data, 17}},
			{8193, 1, 8193 * 4, {reinterpret_cast<uint8_t*>(uintptr_t(1)), 8193 * 4}},
			{1, 8193, 4, {reinterpret_cast<uint8_t*>(uintptr_t(1)), 8193 * 4}},
			{8192, 2049, 32768, {reinterpret_cast<uint8_t*>(uintptr_t(1)), size_t(32768) * 2049}},
			{std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), 0, valid.rgba},
			{2, 2, 8, {valid.rgba.data, std::numeric_limits<size_t>::max()}}}) Reject(compositor, features, invalid);
		for (const auto& rectangle : std::vector<std::array<int, 4>>{{0, 0, 0, 1}, {0, 0, 1, 0}, {0, 0, -1, 1},
			{0, 0, 1, -1}, {0, 0, 8193, 1}, {0, 0, 1, 8193}, {1000001, 0, 1, 1}, {-1000001, 0, 1, 1},
			{0, 1000001, 1, 1}, {0, -1000001, 1, 1}})
			Reject(compositor, features, valid, rectangle[0], rectangle[1], rectangle[2], rectangle[3]);
	}

	/** Full attribute stacks must reject without corrupting the caller's stack. */
	void FullStacks(PwRuffleGlCompositor& compositor, const Features& features, const PwRuffleFrame& frame)
	{
		for (bool client : {false, true})
		{
			GLint depth = 0, maximum = 0;
			glGetIntegerv(client ? GL_CLIENT_ATTRIB_STACK_DEPTH : GL_ATTRIB_STACK_DEPTH, &depth);
			glGetIntegerv(client ? GL_MAX_CLIENT_ATTRIB_STACK_DEPTH : GL_MAX_ATTRIB_STACK_DEPTH, &maximum);
			for (GLint i = depth; i < maximum; ++i)
				if (client) glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT); else glPushAttrib(GL_VIEWPORT_BIT);
			const Snapshot before(features);
			std::string error;
			const bool rejected = !compositor.Draw(frame, 11, 9, 40, 36, error) && !error.empty();
			before.RequireEqual(Snapshot(features));
			for (GLint i = depth; i < maximum; ++i)
				if (client) glPopClientAttrib(); else glPopAttrib();
			Check(rejected, "Full attribute stack was not rejected");
		}
	}
}

/** Native mock-only gate: no Rust host, SWF, game process, or DSO is loaded. */
int main()
{
	try
	{
		GlxWindow window;
		window.Open();
		const Features features;
		std::cout << "GL renderer: " << glGetString(GL_RENDERER) << '\n';
		HostState host;
		PwRuffleGlCompositor compositor;
		std::array<Pixel, 4> pixels{{{255, 0, 0, 255}, {0, 255, 0, 0}, {0, 0, 255, 255}, {200, 100, 50, 128}}};
		PwRuffleFrame frame{2, 2, 8, {reinterpret_cast<uint8_t*>(pixels.data()), sizeof(pixels)}};
		std::string error;
		Clear(features);
		const Snapshot defaults(features);
		Draw(compositor, frame, 11, 9, 40, 36);
		defaults.RequireEqual(Snapshot(features));
		ExpectPixel(21, 36, Over(pixels[0]));
		std::cout << "Default-state draw passed\n";
		host.Configure(features);
		for (int repeat = 0; repeat < 3; ++repeat)
		{
			Clear(features);
			const Snapshot before(features);
			Draw(compositor, frame, 11, 9, 40, 36);
			before.RequireEqual(Snapshot(features));
			if (features.pipelines)
			{
				glUseProgram(0);
				GLint pipeline = 0;
				glGetIntegerv(GL_PROGRAM_PIPELINE_BINDING, &pipeline);
				Check(static_cast<GLuint>(pipeline) == host.pipeline, "Pipeline hidden by the host program was lost");
				glUseProgram(host.program);
			}
			ExpectPixel(21, 36, Over(pixels[0]));
			ExpectPixel(41, 36, Background);
			ExpectPixel(21, 18, Over(pixels[2]));
			ExpectPixel(41, 18, Over(pixels[3]));
			ExpectPixel(10, 18, Background);
			ExpectPixel(51, 18, Background);
			ExpectPixel(21, 8, Background);
			ExpectPixel(21, 45, Background);
			before.RequireEqual(Snapshot(features));
			pixels[0] = {255, uint8_t(40 + repeat), 0, 255};
		}
		Malformed(compositor, features, frame);
		FullStacks(compositor, features, frame);
		window.Resize();
		std::array<Pixel, 3> row{{{19, 37, 71, 255}, {101, 53, 17, 255}, {13, 149, 83, 255}}};
		PwRuffleFrame resized{3, 1, 12, {reinterpret_cast<uint8_t*>(row.data()), sizeof(row)}};
		Clear(features);
		const Snapshot beforeResize(features);
		Draw(compositor, resized, 7, 11, 90, 72);
		beforeResize.RequireEqual(Snapshot(features));
		for (int i = 0; i < 3; ++i) ExpectPixel(22 + i * 30, 47, row[i]);
		compositor.Reset();
		compositor.Reset();
		beforeResize.RequireEqual(Snapshot(features));
		Clear(features);
		Draw(compositor, frame, 11, 9, 40, 36);
		beforeResize.RequireEqual(Snapshot(features));
		ExpectPixel(21, 36, Over(pixels[0]));
		ExpectPixel(41, 18, Over(pixels[3]));
		glDeleteProgram(host.program);
		const Snapshot pendingDelete(features);
		Check(!compositor.Draw(frame, 11, 9, 40, 36, error), "Pending-delete program was not protected");
		pendingDelete.RequireEqual(Snapshot(features));
		Check(glIsProgram(host.program) == GL_TRUE, "Current pending-delete program was destroyed");
		compositor.Reset();
		Check(glGetError() == GL_NO_ERROR, "Final GL error");
		std::cout << "PASS: orientation, straight-alpha RGBA, repeat/update, resize/reset, malformed input, "
			"full stacks and host state (program, " << features.units << " texture units, matrices, PBO/pixel store)\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
