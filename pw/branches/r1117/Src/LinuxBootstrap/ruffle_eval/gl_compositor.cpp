#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#include "gl_compositor.h"
#include <cstdio>
#include <cstring>

namespace
{
	constexpr uint32_t MaxDimension = 8192;
	constexpr uint64_t MaxBytes = 64ull * 1024 * 1024;

	/** Test a whole token in the compatibility-context extension list. */
	bool HasExtension(const char* extensions, const char* name)
	{
		if (!extensions) return false;
		const size_t length = std::strlen(name);
		for (const char* found = std::strstr(extensions, name); found; found = std::strstr(found + length, name))
			if ((found == extensions || found[-1] == ' ') && (found[length] == ' ' || found[length] == '\0'))
				return true;
		return false;
	}

	/** Optional compatibility features which can override an otherwise plain quad. */
	struct Features
	{
		bool rectangle;
		bool samplers;
		bool pipelines;
		bool srgb;
		bool discard;
		bool vertexProgram;
		bool fragmentProgram;
		bool imaging;

		Features(int major, int minor, const char* extensions)
			: rectangle(major > 3 || (major == 3 && minor >= 1) || HasExtension(extensions, "GL_ARB_texture_rectangle")),
			  samplers(major > 3 || (major == 3 && minor >= 3) || HasExtension(extensions, "GL_ARB_sampler_objects")),
			  pipelines(major > 4 || (major == 4 && minor >= 1) || HasExtension(extensions, "GL_ARB_separate_shader_objects")),
			  srgb(major >= 3 || HasExtension(extensions, "GL_EXT_framebuffer_sRGB") || HasExtension(extensions, "GL_ARB_framebuffer_sRGB")),
			  discard(major >= 3 || HasExtension(extensions, "GL_EXT_transform_feedback")),
			  vertexProgram(HasExtension(extensions, "GL_ARB_vertex_program")),
			  fragmentProgram(HasExtension(extensions, "GL_ARB_fragment_program")),
			  imaging(HasExtension(extensions, "GL_ARB_imaging"))
		{}
	};

	/** Restore both stackable state and bindings/matrices excluded from GL stacks.
	 * Matrix tops are copied rather than pushed, so full host matrix stacks work.
	 */
	class SavedState final
	{
	public:
		explicit SavedState(const Features& features) : features_(features)
		{
			glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
			glGetIntegerv(GL_ACTIVE_TEXTURE, &active_);
			// Restore these explicitly as well: legacy attribute stacks are insufficient on Mesa.
			glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_);
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_);
			colorSum_ = glIsEnabled(GL_COLOR_SUM);
			glGetDoublev(GL_MODELVIEW_MATRIX, modelview_);
			glGetDoublev(GL_PROJECTION_MATRIX, projection_);
			glPushAttrib(GL_ALL_ATTRIB_BITS);
			glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
			glActiveTexture(GL_TEXTURE0);
			glGetDoublev(GL_TEXTURE_MATRIX, texture_);
			if (features_.samplers)
			{
				glGetIntegerv(GL_SAMPLER_BINDING, &sampler_);
				glBindSampler(0, 0);
			}
			if (features_.pipelines)
			{
				// Expose the pipeline hidden by glUseProgram before saving/unbinding it.
				glUseProgram(0);
				glGetIntegerv(GL_PROGRAM_PIPELINE_BINDING, &pipeline_);
				glBindProgramPipeline(0);
			}
			if (features_.srgb) srgb_ = glIsEnabled(GL_FRAMEBUFFER_SRGB);
			if (features_.discard) discard_ = glIsEnabled(GL_RASTERIZER_DISCARD);
			if (features_.imaging) glGetDoublev(GL_COLOR_MATRIX, color_);
		}

		SavedState(const SavedState&) = delete;
		SavedState& operator=(const SavedState&) = delete;

		~SavedState()
		{
			glActiveTexture(GL_TEXTURE0);
			glMatrixMode(GL_TEXTURE);
			glLoadMatrixd(texture_);
			glMatrixMode(GL_MODELVIEW);
			glLoadMatrixd(modelview_);
			glMatrixMode(GL_PROJECTION);
			glLoadMatrixd(projection_);
			if (features_.imaging)
			{
				glMatrixMode(GL_COLOR);
				glLoadMatrixd(color_);
			}
			glPopClientAttrib();
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_));
			glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(pack_));
			glPopAttrib();
			if (features_.samplers) glBindSampler(0, static_cast<GLuint>(sampler_));
			if (features_.pipelines) glBindProgramPipeline(static_cast<GLuint>(pipeline_));
			if (features_.srgb) SetEnabled(GL_FRAMEBUFFER_SRGB, srgb_);
			if (features_.discard) SetEnabled(GL_RASTERIZER_DISCARD, discard_);
			SetEnabled(GL_COLOR_SUM, colorSum_);
			glUseProgram(static_cast<GLuint>(program_));
			glActiveTexture(static_cast<GLenum>(active_));
		}

	private:
		/** Restore optional enables explicitly instead of relying on extension stacks. */
		static void SetEnabled(GLenum cap, GLboolean enabled)
		{
			if (enabled) glEnable(cap); else glDisable(cap);
		}
		Features features_;
		GLint program_ = 0, active_ = 0, sampler_ = 0, pipeline_ = 0;
		GLint unpack_ = 0, pack_ = 0;
		GLboolean srgb_ = GL_FALSE, discard_ = GL_FALSE, colorSum_ = GL_FALSE;
		GLdouble modelview_[16]{}, projection_[16]{}, texture_[16]{}, color_[16]{};
	};

	/** Upload client RGBA bytes independently of the host's unpack and transfer modes. */
	void PreparePixels(const Features& features)
	{
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		for (GLenum field : {GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS,
			GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST})
			glPixelStorei(field, 0);
		glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
		for (GLenum field : {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE}) glPixelTransferf(field, 1);
		for (GLenum field : {GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS}) glPixelTransferf(field, 0);
		if (features.imaging)
		{
			for (GLenum cap : {GL_COLOR_TABLE, GL_POST_CONVOLUTION_COLOR_TABLE, GL_POST_COLOR_MATRIX_COLOR_TABLE,
				GL_CONVOLUTION_1D, GL_CONVOLUTION_2D, GL_SEPARABLE_2D, GL_HISTOGRAM, GL_MINMAX}) glDisable(cap);
			for (GLenum field : {GL_POST_COLOR_MATRIX_RED_SCALE, GL_POST_COLOR_MATRIX_GREEN_SCALE,
				GL_POST_COLOR_MATRIX_BLUE_SCALE, GL_POST_COLOR_MATRIX_ALPHA_SCALE}) glPixelTransferf(field, 1);
			for (GLenum field : {GL_POST_COLOR_MATRIX_RED_BIAS, GL_POST_COLOR_MATRIX_GREEN_BIAS,
				GL_POST_COLOR_MATRIX_BLUE_BIAS, GL_POST_COLOR_MATRIX_ALPHA_BIAS}) glPixelTransferf(field, 0);
			glMatrixMode(GL_COLOR);
			glLoadIdentity();
		}
	}

	/** Neutralize host fixed-function effects, retaining only texture source-over. */
	void PrepareDraw(const Features& features, GLint units, GLint clipPlanes)
	{
		glUseProgram(0);
		if (features.vertexProgram) glDisable(GL_VERTEX_PROGRAM_ARB);
		if (features.fragmentProgram) glDisable(GL_FRAGMENT_PROGRAM_ARB);
		if (features.srgb) glDisable(GL_FRAMEBUFFER_SRGB);
		if (features.discard) glDisable(GL_RASTERIZER_DISCARD);
		for (GLenum cap : {GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_ALPHA_TEST, GL_CULL_FACE,
			GL_LIGHTING, GL_FOG, GL_COLOR_LOGIC_OP, GL_COLOR_SUM, GL_POLYGON_STIPPLE, GL_POLYGON_SMOOTH,
			GL_POLYGON_OFFSET_FILL, GL_DITHER, GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE,
			GL_SAMPLE_ALPHA_TO_ONE, GL_SAMPLE_COVERAGE}) glDisable(cap);
		for (GLint plane = 0; plane < clipPlanes; ++plane) glDisable(GL_CLIP_PLANE0 + plane);
		for (GLint unit = 0; unit < units; ++unit)
		{
			glActiveTexture(GL_TEXTURE0 + unit);
			for (GLenum cap : {GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP,
				GL_TEXTURE_GEN_S, GL_TEXTURE_GEN_T, GL_TEXTURE_GEN_R, GL_TEXTURE_GEN_Q}) glDisable(cap);
			if (features.rectangle) glDisable(GL_TEXTURE_RECTANGLE);
		}
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, 1, 1, 0, -1, 1);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glDepthMask(GL_FALSE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glEnable(GL_BLEND);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(1, 1, 1, 1);
	}

	/** Check stack space before pushing, so failure cannot pop the host's entry. */
	bool HasStackSpace(GLenum depthName, GLenum maxName)
	{
		GLint depth = 0, maximum = 0;
		glGetIntegerv(depthName, &depth);
		glGetIntegerv(maxName, &maximum);
		return depth < maximum;
	}

	/** Report one GL error without draining an unbounded external error source. */
	bool CheckGl(std::string& error)
	{
		const GLenum code = glGetError();
		if (code == GL_NO_ERROR) return true;
		error = "OpenGL error " + std::to_string(code);
		return false;
	}
}

PwRuffleGlCompositor::~PwRuffleGlCompositor()
{
	Reset();
}

void PwRuffleGlCompositor::Reset() noexcept
{
	if (texture_) glDeleteTextures(1, &texture_);
	texture_ = 0;
	width_ = height_ = 0;
}

bool PwRuffleGlCompositor::Draw(const PwRuffleFrame& frame, int x, int y, int width, int height, std::string& error)
{
	error.clear();
	const uint64_t stride = uint64_t(frame.width) * 4;
	const uint64_t length = uint64_t(frame.stride) * frame.height;
	if (!frame.rgba.data || !frame.width || !frame.height || frame.width > MaxDimension || frame.height > MaxDimension ||
		frame.stride != stride || length > MaxBytes || frame.rgba.len != length)
	{
		error = "Invalid frame: require packed RGBA8, dimensions 1..8192, exact length, at most 64 MiB";
		return false;
	}
	if (width <= 0 || height <= 0 || width > int(MaxDimension) || height > int(MaxDimension) ||
		x < -1000000 || x > 1000000 || y < -1000000 || y > 1000000)
	{
		error = "Invalid destination rectangle";
		return false;
	}
	const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	int major = 0, minor = 0;
	if (!version || std::sscanf(version, "%d.%d", &major, &minor) != 2 || major < 2 || (major == 2 && minor < 1))
	{
		error = "A current desktop OpenGL 2.1+ compatibility context is required";
		return false;
	}
	if (!CheckGl(error)) return false;
	if (major > 3 || (major == 3 && minor >= 2))
	{
		GLint profile = 0;
		glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
		if (!(profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT))
		{
			error = "Core-profile contexts are unsupported";
			return false;
		}
	}
	GLint maxTexture = 0, maxViewport[2]{}, mode = 0, program = 0, units = 0, clipPlanes = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewport);
	glGetIntegerv(GL_RENDER_MODE, &mode);
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
	glGetIntegerv(GL_MAX_CLIP_PLANES, &clipPlanes);
	if (frame.width > uint32_t(maxTexture) || frame.height > uint32_t(maxTexture) ||
		width > maxViewport[0] || height > maxViewport[1] || mode != GL_RENDER || units < 1)
	{
		error = "Unsupported GL dimensions or render mode";
		return false;
	}
	if (program)
	{
		GLint deleted = GL_FALSE;
		glGetProgramiv(static_cast<GLuint>(program), GL_DELETE_STATUS, &deleted);
		if (deleted)
		{
			error = "Cannot preserve a current program pending deletion";
			return false;
		}
	}
	if (!HasStackSpace(GL_ATTRIB_STACK_DEPTH, GL_MAX_ATTRIB_STACK_DEPTH) ||
		!HasStackSpace(GL_CLIENT_ATTRIB_STACK_DEPTH, GL_MAX_CLIENT_ATTRIB_STACK_DEPTH))
	{
		error = "Host GL attribute stack is full";
		return false;
	}
	const Features features(major, minor, reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)));
	if (!CheckGl(error)) return false;
	const SavedState saved(features);
	PreparePixels(features);
	PrepareDraw(features, units, clipPlanes);
	glViewport(x, y, width, height);
	if (!texture_) glGenTextures(1, &texture_);
	if (!texture_)
	{
		error = "Could not allocate a compositor texture";
		return false;
	}
	glBindTexture(GL_TEXTURE_2D, texture_);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (width_ != frame.width || height_ != frame.height)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data);
	else
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data);
	if (!CheckGl(error)) return false;
	width_ = frame.width;
	height_ = frame.height;
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(1, 0);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(0, 1);
	glEnd();
	return CheckGl(error);
}
