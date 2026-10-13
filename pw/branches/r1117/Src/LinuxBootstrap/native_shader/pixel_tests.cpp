#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "shader_backend.hpp"
#include "mojoshader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace pwshader;
namespace {
using Bytes = std::vector<unsigned char>;
using Pixel = std::array<unsigned char, 4>;
const Pixel ClearPixel{11,13,17,255};
struct Checks {
	unsigned count = 0, failed = 0;
	void Test(bool ok, const std::string& name) {
		++count; if (!ok) ++failed;
		std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
	}
	void Good(Result s, const std::string& name) {
		Test(bool(s), name + (s ? "" : " code=" + std::to_string(int(s.code)) + " " + s.message));
	}
	int Finish() {
		std::cout << "SUMMARY checks=" << count << " failures=" << failed << '\n';
		return failed ? 1 : 0;
	}
};

struct Host {
	Display* display = nullptr;
	XVisualInfo* visual = nullptr;
	Colormap colormap = 0;
	Window window = 0;
	GLXContext context = nullptr;
	uint64_t lifetime;
	unsigned hiddenBlendFunctions = 0, deniedBlendLookups = 0;
	explicit Host(uint64_t life) : lifetime(life) {}
	void Open() {
		display = XOpenDisplay(nullptr);
		if (!display) throw std::runtime_error("XOpenDisplay failed");
		int attributes[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, None};
		visual = glXChooseVisual(display, DefaultScreen(display), attributes);
		if (!visual) throw std::runtime_error("no GLX visual");
		colormap = XCreateColormap(display, RootWindow(display, visual->screen), visual->visual, AllocNone);
		XSetWindowAttributes a{}; a.colormap = colormap;
		window = XCreateWindow(display, RootWindow(display, visual->screen), 0, 0, 64, 64, 0,
			visual->depth, InputOutput, visual->visual, CWColormap, &a);
		context = glXCreateContext(display, visual, nullptr, True);
		MakeCurrent();
	}
	void MakeCurrent() {
		if (!context || !glXMakeCurrent(display, window, context)) throw std::runtime_error("GLX current failed");
	}
	void ReleaseCurrent() { glXMakeCurrent(display, None, nullptr); }
	static ContextKey Current(void* user) noexcept {
		const auto& h = *static_cast<Host*>(user);
		const auto c = glXGetCurrentContext();
		return {reinterpret_cast<uintptr_t>(c), c == h.context ? h.lifetime : 0};
	}
	static void* Lookup(const char* name, void* user) {
		auto& host = *static_cast<Host*>(user);
		const bool function = !std::strcmp(name, "glBlendFuncSeparatei") || !std::strcmp(name, "glBlendFuncSeparateiARB");
		const bool equation = !std::strcmp(name, "glBlendEquationSeparatei") || !std::strcmp(name, "glBlendEquationSeparateiARB");
		if ((function && (host.hiddenBlendFunctions & 1)) || (equation && (host.hiddenBlendFunctions & 2))) {
			++host.deniedBlendLookups;
			return nullptr;
		}
		return reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
	}
	ContextBinding Binding() { return {this, Current, Lookup}; }
	~Host() {
		if (display && context) { ReleaseCurrent(); glXDestroyContext(display, context); }
		if (display && window) XDestroyWindow(display, window);
		if (display && colormap) XFreeColormap(display, colormap);
		if (visual) XFree(visual);
		if (display) XCloseDisplay(display);
	}
};

/** Test capability discovery uses the actual context, including Mesa's 3.3 override runs. */
bool IndexedBlendAvailable() {
	GLint major = 0, extensions = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	if (major >= 4) return true;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
	for (GLint i = 0; i < extensions; ++i)
		if (!std::strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i))), "GL_ARB_draw_buffers_blend")) return true;
	return false;
}

const BlendMode BlendModes[] = {BlendMode::LerpByAlpha, BlendMode::PremultipliedLerp, BlendMode::AddColor,
	BlendMode::AddColorMulAlpha, BlendMode::MulColor, BlendMode::MulInvColor};

struct Target {
	GLuint fbo = 0, color = 0, depth = 0;
	Target() {
		glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
		glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 64, 64);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			throw std::runtime_error("offscreen target incomplete");
	}
	~Target() { glDeleteRenderbuffers(1, &depth); glDeleteTextures(1, &color); glDeleteFramebuffers(1, &fbo); }
	void Clear() {
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1); glDepthMask(1);
		glClearColor(ClearPixel[0]/255.f, ClearPixel[1]/255.f, ClearPixel[2]/255.f, 1);
		glClearDepth(1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
};

Pixel ReadPixel(int x, int y) {
	Pixel p{};
	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
	return p;
}
bool Near(Pixel actual, Pixel expected, int tolerance = 1) {
	for (unsigned i = 0; i < 4; ++i) if (std::abs(int(actual[i]) - int(expected[i])) > tolerance) return false;
	return true;
}
void PixelIs(Checks& check, int x, int y, Pixel expected, const char* name) {
	const auto p = ReadPixel(x, y);
	check.Test(Near(p, expected), std::string(name) + " got=" + std::to_string(p[0]) + "," +
		std::to_string(p[1]) + "," + std::to_string(p[2]) + "," + std::to_string(p[3]));
}

struct Texture {
	GLuint name = 0;
	explicit Texture(bool white = false, unsigned char alpha = 255) {
		const unsigned char pixels[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
		const unsigned char one[] = {255,255,255,alpha};
		glGenTextures(1, &name); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, name);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, white ? 1 : 2, white ? 1 : 2, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, white ? one : pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 7);
	}
	~Texture() { glDeleteTextures(1, &name); }
};

/** Deliberate padding, stream prefix, unused vertex, index prefix and base vertex. */
struct Mesh {
	struct Vertex { float prefix; float position[3]; float padding[2]; float uv[2]; uint32_t tail; };
	std::vector<unsigned char> vertices;
	std::vector<uint16_t> indices{31,31,0,1,2,0,2,3};
	Mesh() : vertices(12 + sizeof(Vertex) * 5) {
		const float xy[][2] = {{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}};
		for (size_t i = 0; i < 4; ++i) {
			Vertex v{}; v.prefix = 999; v.tail = 0xfeedbeef;
			v.position[0] = xy[i][0]; v.position[1] = xy[i][1]; v.position[2] = .25f;
			v.uv[0] = v.uv[1] = .25f;
			Put(i, v);
		}
	}
	Vertex Get(size_t i) const { Vertex v; std::memcpy(&v, vertices.data() + 12 + (i + 1) * sizeof(v), sizeof(v)); return v; }
	void Put(size_t i, const Vertex& v) { std::memcpy(vertices.data() + 12 + (i + 1) * sizeof(v), &v, sizeof(v)); }
	void Z(float left, float right) {
		for (size_t i = 0; i < 4; ++i) { auto v = Get(i); v.position[2] = v.position[0] < 0 ? left : right; Put(i, v); }
	}
	IndexedDraw Draw(GLuint texture) const {
		IndexedDraw d;
		d.vertices = {vertices.data(), vertices.size()}; d.indexData = {indices.data(), indices.size()*2};
		d.streamOffset = 12; d.stride = sizeof(Vertex); d.positionOffset = offsetof(Vertex, position);
		d.uvOffset = offsetof(Vertex, uv); d.firstIndex = 2; d.indexCount = 6; d.baseVertex = 1;
		d.texture = texture; d.viewportWidth = d.viewportHeight = 64;
		return d;
	}
};

const std::array<Float4,4> Identity{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
void Registers(ShaderBackend& backend, const std::array<Float4,4>& world = Identity,
	const std::array<Float4,4>& view = Identity, Float4 uv = {0,0,0,0},
	Float4 mul = {1,1,1,1}, Float4 add = {0,0,0,0}) {
	for (const auto& s : {backend.SetFloat4(Stage::Vertex, 0, {world.data(),4}),
		backend.SetFloat4(Stage::Vertex, 8, {view.data(),4}), backend.SetFloat4(Stage::Vertex, 20, {&uv,1}),
		backend.SetFloat4(Stage::Pixel, 20, {&mul,1}), backend.SetFloat4(Stage::Pixel, 21, {&add,1})})
		if (!s) throw std::runtime_error("register upload failed: " + s.message);
}

Bytes Read(const char* path) {
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	const auto size = f.tellg();
	if (!f || size <= 0 || size > 4*1024*1024) throw std::runtime_error("asset unavailable/oversize");
	Bytes bytes(static_cast<size_t>(size)); f.seekg(0);
	if (!f.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("short asset read");
	return bytes;
}

/** Independent CPU oracle for transformed quad coverage; excludes rasterization-edge ties. */
bool Coverage(const Mesh& mesh, const std::array<Float4,4>& world, const std::array<Float4,4>& view) {
	double points[4][2];
	for (size_t i = 0; i < 4; ++i) {
		const auto vertex = mesh.Get(i);
		double p[4] = {vertex.position[0],vertex.position[1],vertex.position[2],1}, w[4]{}, clip[4]{};
		for (size_t r = 0; r < 4; ++r) for (size_t c = 0; c < 4; ++c) w[r] += world[r][c]*p[c];
		for (size_t r = 0; r < 4; ++r) for (size_t c = 0; c < 4; ++c) clip[r] += view[r][c]*w[c];
		points[i][0] = (clip[0]/clip[3]+1)*32; points[i][1] = (clip[1]/clip[3]+1)*32;
	}
	bool ok = true;
	unsigned samples = 0;
	for (int y = 2; y < 64; y += 4) for (int x = 2; x < 64; x += 4) {
		bool inside = true, edge = false;
		for (unsigned i = 0; i < 4; ++i) {
			const unsigned j = (i+1)%4;
			const double dx = points[j][0]-points[i][0], dy = points[j][1]-points[i][1];
			const double distance = (dx*(y+.5-points[i][1])-dy*(x+.5-points[i][0]))/std::hypot(dx,dy);
			if (distance < 0) inside = false;
			if (std::abs(distance) < .75) edge = true;
		}
		if (edge) continue;
		++samples;
		if (!Near(ReadPixel(x,y), inside ? Pixel{255,0,0,255} : ClearPixel)) {
			ok = false; std::cout << "ORACLE mismatch x=" << x << " y=" << y << " inside=" << inside << '\n';
		}
	}
	std::cout << "ORACLE tested_pixels=" << samples << '\n';
	return ok;
}

std::vector<int64_t> Snapshot(GLuint borrowed) {
	std::vector<int64_t> s;
	for (GLenum query : {GL_CURRENT_PROGRAM, GL_VERTEX_ARRAY_BINDING, GL_ARRAY_BUFFER_BINDING,
		GL_ELEMENT_ARRAY_BUFFER_BINDING, GL_ACTIVE_TEXTURE, GL_DEPTH_FUNC, GL_CULL_FACE_MODE, GL_FRONT_FACE,
		GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB,
		GL_BLEND_EQUATION_ALPHA, GL_DEPTH_WRITEMASK, GL_ALPHA_TEST_FUNC, GL_LOGIC_OP_MODE,
		GL_CLAMP_VERTEX_COLOR, GL_CLAMP_FRAGMENT_COLOR}) {
		GLint v; glGetIntegerv(query, &v); s.push_back(v);
	}
	GLfloat alphaReference; glGetFloatv(GL_ALPHA_TEST_REF, &alphaReference);
	uint32_t alphaBits; std::memcpy(&alphaBits, &alphaReference, sizeof(alphaBits)); s.push_back(alphaBits);
	for (GLenum flag : {GL_DEPTH_TEST,GL_CULL_FACE,GL_BLEND,GL_ALPHA_TEST,GL_SCISSOR_TEST,GL_STENCIL_TEST,
		GL_DITHER,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD,GL_PROGRAM_POINT_SIZE,GL_POLYGON_OFFSET_FILL,
		GL_PRIMITIVE_RESTART,GL_DEPTH_CLAMP,GL_COLOR_LOGIC_OP,GL_VERTEX_PROGRAM_TWO_SIDE}) s.push_back(glIsEnabled(flag));
	GLint buffers = 0; glGetIntegerv(GL_MAX_DRAW_BUFFERS, &buffers);
	for (GLuint buffer = 0; buffer < GLuint(buffers); ++buffer) {
		GLboolean mask[4]; glGetBooleani_v(GL_COLOR_WRITEMASK, buffer, mask);
		for (auto value : mask) s.push_back(value);
		s.push_back(glIsEnabledi(GL_BLEND, buffer));
		if (IndexedBlendAvailable()) for (GLenum query : {GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB,
			GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA}) {
			GLint value = 0; glGetIntegeri_v(query, buffer, &value); s.push_back(value);
		}
	}
	GLfloat blendColor[4]; glGetFloatv(GL_BLEND_COLOR, blendColor);
	for (float value : blendColor) { uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); s.push_back(bits); }
	for (GLenum query : {GL_VIEWPORT,GL_SCISSOR_BOX,GL_COLOR_WRITEMASK}) {
		GLint v[4]; glGetIntegerv(query,v); for (auto x : v) s.push_back(x);
	}
	GLint polygon[2]; glGetIntegerv(GL_POLYGON_MODE,polygon); for (auto x : polygon) s.push_back(x);
	GLdouble depth[2]; glGetDoublev(GL_DEPTH_RANGE,depth); for (auto x : depth) s.push_back(int64_t(x*1000000));
	for (GLuint attribute = 0; attribute < 4; ++attribute) {
		for (GLenum query : {GL_VERTEX_ATTRIB_ARRAY_ENABLED,GL_VERTEX_ATTRIB_ARRAY_SIZE,GL_VERTEX_ATTRIB_ARRAY_STRIDE,
			GL_VERTEX_ATTRIB_ARRAY_TYPE,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
			GL_VERTEX_ATTRIB_ARRAY_INTEGER,GL_VERTEX_ATTRIB_ARRAY_DIVISOR}) {
			GLint v; glGetVertexAttribiv(attribute,query,&v); s.push_back(v);
		}
		void* pointer; glGetVertexAttribPointerv(attribute,GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer);
		s.push_back(int64_t(reinterpret_cast<uintptr_t>(pointer)));
	}
	GLint active; glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
	for (GLenum unit : {GL_TEXTURE0,GL_TEXTURE3}) {
		glActiveTexture(unit);
		for (GLenum query : {GL_TEXTURE_BINDING_2D,GL_SAMPLER_BINDING}) { GLint v; glGetIntegerv(query,&v); s.push_back(v); }
	}
	glActiveTexture(GL_TEXTURE0);
	GLint binding; glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding); glBindTexture(GL_TEXTURE_2D,borrowed);
	for (GLenum parameter : {GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,
		GL_TEXTURE_BASE_LEVEL,GL_TEXTURE_MAX_LEVEL}) { GLint v; glGetTexParameteriv(GL_TEXTURE_2D,parameter,&v); s.push_back(v); }
	glBindTexture(GL_TEXTURE_2D,GLuint(binding)); glActiveTexture(GLenum(active));
	return s;
}

/** Test-only foreign GL program compiled from the same real bytecode, not hand-written GLSL. */
struct Foreign {
	GLuint program = 0, vs = 0, ps = 0, vao = 0, vbo = 0, ibo = 0, sampler = 0;
	Foreign(const Bytes& archive, const Mesh& mesh) {
		GLuint stages[2]{};
		for (unsigned i = 0; i < 2; ++i) {
			const size_t offset = i ? 1236 : 772, length = i ? 300 : 464;
			const auto* p = MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSL120,"main",archive.data()+offset,
				unsigned(length),nullptr,0,nullptr,0,nullptr,nullptr,nullptr);
			if (p->error_count) throw std::runtime_error("foreign shader parse failed");
			stages[i] = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
			glShaderSource(stages[i],1,&p->output,&p->output_len); glCompileShader(stages[i]);
			GLint compiled; glGetShaderiv(stages[i],GL_COMPILE_STATUS,&compiled);
			MOJOSHADER_freeParseData(p);
			if (!compiled) throw std::runtime_error("foreign shader compile failed");
		}
		vs = stages[0]; ps = stages[1]; program = glCreateProgram();
		glAttachShader(program,vs); glAttachShader(program,ps); glLinkProgram(program);
		GLint linked; glGetProgramiv(program,GL_LINK_STATUS,&linked);
		if (!linked) throw std::runtime_error("foreign shader link failed");
		glGenVertexArrays(1,&vao); glBindVertexArray(vao);
		glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
		glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(mesh.vertices.size()),mesh.vertices.data(),GL_STATIC_DRAW);
		glGenBuffers(1,&ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
		const uint16_t indices[] = {1,2,3,1,3,4};
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
		for (unsigned i = 0; i < 2; ++i) {
			const GLint location = glGetAttribLocation(program,i ? "vs_v1" : "vs_v0");
			glVertexAttribPointer(GLuint(location),i ? 2 : 3,GL_FLOAT,GL_FALSE,sizeof(Mesh::Vertex),
				reinterpret_cast<void*>(12 + (i ? offsetof(Mesh::Vertex,uv) : offsetof(Mesh::Vertex,position))));
			glEnableVertexAttribArray(GLuint(location));
		}
		glVertexAttribPointer(3,2,GL_FLOAT,GL_TRUE,sizeof(Mesh::Vertex),reinterpret_cast<void*>(4));
		glEnableVertexAttribArray(3); glVertexAttribDivisor(3,2);
		glGenSamplers(1,&sampler);
		glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glUseProgram(program);
		std::array<Float4,9> values{};
		std::copy(Identity.begin(),Identity.end(),values.begin()); std::copy(Identity.begin(),Identity.end(),values.begin()+4);
		const Float4 colors[2] = {{0,0,1,1},{0,0,0,0}};
		glUniform4fv(glGetUniformLocation(program,"vs_uniforms_vec4[0]"),9,values[0].data());
		glUniform4fv(glGetUniformLocation(program,"ps_uniforms_vec4[0]"),2,colors[0].data());
		glUniform1i(glGetUniformLocation(program,"ps_s0"),0);
	}
	~Foreign() {
		glUseProgram(0); glBindVertexArray(0); glBindSampler(0,0);
		glDeleteSamplers(1,&sampler); glDeleteBuffers(1,&ibo); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
		glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(ps);
	}
};

void CleanRaster() {
	for (GLenum flag : {GL_BLEND,GL_DEPTH_TEST,GL_CULL_FACE,GL_ALPHA_TEST,GL_SCISSOR_TEST,GL_STENCIL_TEST,
		GL_DITHER,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD,GL_POLYGON_OFFSET_FILL,GL_COLOR_LOGIC_OP}) glDisable(flag);
	glColorMask(1,1,1,1); glDepthMask(1); glDepthRange(0,1); glViewport(0,0,64,64); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
}

/** Exercise final shader alpha, byte thresholds, discarded depth and explicit face policy. */
void RasterTests(Checks& check, ShaderBackend& backend, const ProgramRef& program, Target& target) {
	Mesh mesh, behind; behind.Z(.5f, .5f);
	Texture white(true), translucent(true, 126);
	auto draw = mesh.Draw(white.name);
	draw.alphaTest = AlphaTest::GreaterEqual;
	for (int reference : {0, 127, 255}) for (int alpha : {0, 126, 127, 128, 255}) {
		draw.alphaReference = reference;
		const bool visible = alpha >= reference;
		Registers(backend, Identity, Identity, {}, {1,0,0,0}, {0,0,0,alpha / 255.f});
		target.Clear();
		glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_LESS, .731f);
		glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); glFrontFace(GL_CW);
		const auto before = Snapshot(white.name);
		check.Good(backend.Draw(program, draw), "cutout threshold draw ref=" + std::to_string(reference) + " alpha=" + std::to_string(alpha));
		check.Test(Snapshot(white.name) == before, "cutout restores alpha function/reference and culling");
		PixelIs(check, 32, 32, visible ? Pixel{255,0,0,static_cast<unsigned char>(alpha)} : ClearPixel,
			"final diffuse alpha uses greater-or-equal byte threshold");
		float depth = 0; glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
		check.Test(std::abs(depth - (visible ? .25f : 1.f)) < .00001f, "discarded fragments do not write depth");
		Registers(backend, Identity, Identity, {}, {0,1,0,1});
		check.Good(backend.Draw(program, behind.Draw(white.name)), "draw opaque surface behind cutout");
		PixelIs(check, 32, 32, visible ? Pixel{255,0,0,static_cast<unsigned char>(alpha)} : Pixel{0,255,0,255},
			"cutout holes expose later opaque geometry");
	}
	draw.alphaReference = 127;
	draw.texture = translucent.name;
	for (int added : {0, 2}) {
		Registers(backend, Identity, Identity, {}, {1,0,0,1}, {0,0,0,added / 255.f}); target.Clear();
		check.Good(backend.Draw(program, draw), "texture alpha combined with diffuse add");
		PixelIs(check,32,32,added ? Pixel{255,0,0,128} : ClearPixel,"threshold sees texture alpha plus shader add");
	}
	draw.texture = white.name; draw.alphaTest = AlphaTest::Disabled;
	Registers(backend, Identity, Identity, {}, {1,0,0,0}); target.Clear();
	check.Good(backend.Draw(program, draw), "disabled alpha test accepts zero shader alpha");
	PixelIs(check,32,32,{255,0,0,0},"alpha disabled remains opaque without blending");
	for (bool mirrored : {false, true}) for (Winding face : {Winding::Clockwise, Winding::CounterClockwise})
		for (CullMode cull : {CullMode::Back, CullMode::Disabled}) {
			auto world = Identity; if (mirrored) world[0][0] = -1;
			Registers(backend, world); target.Clear();
			draw.frontFace = face; draw.cullMode = cull;
			check.Good(backend.Draw(program, draw), "explicit culling with mirrored WORLD");
			const bool visible = cull == CullMode::Disabled || (mirrored == (face == Winding::Clockwise));
			PixelIs(check,32,32,visible ? Pixel{255,255,255,255} : ClearPixel,"two-sided and mirrored face policy");
		}
	draw = mesh.Draw(white.name); Registers(backend); target.Clear();
	glEnable(GL_COLOR_LOGIC_OP); glLogicOp(GL_XOR);
	const auto before = Snapshot(white.name);
	check.Good(backend.Draw(program, draw), "opaque draw overrides foreign color logic operation");
	PixelIs(check,32,32,{255,255,255,255},"foreign XOR cannot modify opaque shader output");
	check.Test(Snapshot(white.name) == before,"color logic operation and alpha state restored");
	CleanRaster();
	for (int kind = 0; kind < 5; ++kind) {
		auto invalid = draw;
		if (kind == 0) invalid.alphaTest = static_cast<AlphaTest>(-1);
		if (kind == 1) invalid.cullMode = static_cast<CullMode>(99);
		if (kind >= 2) invalid.alphaReference = kind == 2 ? -1 : kind == 3 ? 256 : INT32_MAX;
		target.Clear(); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_NOTEQUAL, .37f);
		const auto state = Snapshot(white.name); const auto count = backend.GetDiagnostics().draws;
		check.Test(backend.Draw(program, invalid).code == Code::InvalidInput, "invalid raster descriptor rejected");
		check.Test(backend.GetDiagnostics().draws == count && Snapshot(white.name) == state,
			"invalid raster descriptor preserves draw count and GL state");
		PixelIs(check,32,32,ClearPixel,"invalid raster descriptor draws no fallback");
	}
	CleanRaster(); Registers(backend);
}

/** Independent RGB equations, not a copy of the production factor table. */
Pixel Blended(BlendMode mode, Pixel source, Pixel destination) {
	Pixel result = destination;
	const double alpha = source[3] / 255.;
	for (size_t i = 0; i < 3; ++i) {
		const double s = source[i] / 255., d = destination[i] / 255.;
		double value = 0;
		switch (mode) {
		case BlendMode::LerpByAlpha: value = s * alpha + d * (1 - alpha); break;
		case BlendMode::PremultipliedLerp: value = s + d * (1 - alpha); break;
		case BlendMode::AddColor: value = s + d; break;
		case BlendMode::AddColorMulAlpha: value = s * alpha + d; break;
		case BlendMode::MulColor: value = d * s; break;
		case BlendMode::MulInvColor: value = d * (1 - s); break;
		case BlendMode::Off: value = s; break;
		}
		result[i] = static_cast<unsigned char>(std::lround(std::clamp(value, 0., 1.) * 255));
	}
	return result;
}

void ClearBlendTarget(Target& target, Pixel destination, double depth = 1) {
	target.Clear();
	glClearColor(destination[0]/255.f, destination[1]/255.f, destination[2]/255.f, destination[3]/255.f);
	glClearDepth(depth); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void SourceColor(ShaderBackend& backend, Pixel source, bool mirrored = false) {
	auto world = Identity;
	if (mirrored) world[0][0] = -1;
	Registers(backend, world, Identity, {}, {}, {source[0]/255.f, source[1]/255.f, source[2]/255.f, source[3]/255.f});
}

void DepthIs(Checks& check, float expected, const char* name) {
	float depth = 0; glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
	check.Test(std::abs(depth - expected) < .00001f, name);
}

/** Exercise all equations against nontrivial destination color/alpha and hostile indexed state. */
void BlendTests(Checks& check, ShaderBackend& backend, const ProgramRef& program, Target& target, const Bytes& archive) {
	Mesh mesh; Texture white(true);
	const Pixel destination{47,89,131,73};
	auto draw = mesh.Draw(white.name);
	check.Test(draw.blendMode == BlendMode::Off, "default blend mode preserves opaque API");
	if (IndexedBlendAvailable()) {
		GLint major = 0; glGetIntegerv(GL_MAJOR_VERSION, &major);
		const auto function = reinterpret_cast<PFNGLBLENDFUNCSEPARATEIPROC>(glXGetProcAddressARB(
			reinterpret_cast<const GLubyte*>(major >= 4 ? "glBlendFuncSeparatei" : "glBlendFuncSeparateiARB")));
		const auto equation = reinterpret_cast<PFNGLBLENDEQUATIONSEPARATEIPROC>(glXGetProcAddressARB(
			reinterpret_cast<const GLubyte*>(major >= 4 ? "glBlendEquationSeparatei" : "glBlendEquationSeparateiARB")));
		if (!function || !equation) throw std::runtime_error("test context advertised incomplete indexed blending");
		{
			Foreign foreign(archive, mesh);
			for (BlendMode mode : BlendModes) for (int alpha : {0,64,128,255}) {
				CleanRaster(); ClearBlendTarget(target, destination);
				const Pixel source{61,103,149,static_cast<unsigned char>(alpha)};
				SourceColor(backend, source); draw.blendMode = mode;
				GLint buffers = 0; glGetIntegerv(GL_MAX_DRAW_BUFFERS, &buffers);
				for (GLuint i = 0; i < GLuint(buffers); ++i) {
					function(i, i % 2 ? GL_CONSTANT_COLOR : GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
						GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA);
					equation(i, i % 2 ? GL_MAX : GL_FUNC_REVERSE_SUBTRACT, GL_MIN);
					glColorMaski(i, i % 2, GL_TRUE, GL_FALSE, GL_TRUE);
					if (i % 2) glDisablei(GL_BLEND, i); else glEnablei(GL_BLEND, i);
				}
				glBlendColor(.13f,.27f,.39f,.51f);
				glEnable(GL_COLOR_LOGIC_OP); glLogicOp(GL_XOR);
				glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_NEVER,.731f);
				glDepthFunc(GL_GREATER); glDepthMask(GL_TRUE); glDepthRange(.2,.8);
				glUseProgram(foreign.program); glBindVertexArray(foreign.vao);
				const auto state = Snapshot(white.name);
				check.Good(backend.Draw(program, draw), "six-mode blend draw mode=" + std::to_string(int(mode)) + " alpha=" + std::to_string(alpha));
				PixelIs(check,32,32,Blended(mode,source,destination),"original RGB equation preserves destination alpha");
				DepthIs(check,1,"transparent draw preserves depth");
				check.Test(Snapshot(white.name) == state,"all indexed factors/equations/enables/masks and foreign program restored");
			}
		}
		CleanRaster();
		for (BlendMode mode : BlendModes) {
			draw = mesh.Draw(white.name); draw.blendMode = mode;
			const Pixel source{223,179,241,192}, bright{191,211,173,37};
			SourceColor(backend, source); ClearBlendTarget(target, bright);
			check.Good(backend.Draw(program,draw),"blend equation saturation draw");
			PixelIs(check,32,32,Blended(mode,source,bright),"blend saturation clamps RGB without changing alpha");
			for (bool mirrored : {false,true}) for (CullMode cull : {CullMode::Back,CullMode::Disabled})
				for (int alpha : {126,127}) {
					const Pixel cutout{61,103,149,static_cast<unsigned char>(alpha)};
					draw.cullMode = cull; draw.alphaTest = AlphaTest::GreaterEqual;
					SourceColor(backend,cutout,mirrored); ClearBlendTarget(target,destination);
					check.Good(backend.Draw(program,draw),"blend with mirrored culling and boundary alpha test");
					const bool visible = alpha >= 127 && (!mirrored || cull == CullMode::Disabled);
					PixelIs(check,32,32,visible ? Blended(mode,cutout,destination) : destination,"blend respects alpha and culling admission");
					DepthIs(check,1,"mixed raster modes keep depth read-only");
				}
			// A real opaque occluder, followed by layers behind/on/in front of it.
			Mesh opaque; opaque.Z(.5f,.5f);
			ClearBlendTarget(target,ClearPixel); SourceColor(backend,destination);
			check.Good(backend.Draw(program,opaque.Draw(white.name)),"opaque blend occluder");
			const Pixel near{37,71,109,83};
			Mesh layer; layer.Z(.75f,.75f); draw = layer.Draw(white.name); draw.blendMode = mode;
			SourceColor(backend,near);
			check.Good(backend.Draw(program,draw),"transparent layer behind occluder");
			PixelIs(check,32,32,destination,"opaque depth rejects transparent layer");
			layer.Z(.5f,.5f);
			check.Good(backend.Draw(program,draw),"transparent layer at equal depth");
			Pixel expected = Blended(mode,near,destination);
			PixelIs(check,32,32,expected,"LEQUAL accepts coplanar transparency");
			layer.Z(.1f,.1f);
			check.Good(backend.Draw(program,draw),"near transparent layer");
			expected = Blended(mode,near,expected);
			PixelIs(check,32,32,expected,"near layer blends in submission order");
			layer.Z(.3f,.3f);
			check.Good(backend.Draw(program,draw),"farther layer after near transparency");
			expected = Blended(mode,near,expected);
			PixelIs(check,32,32,expected,"near transparent layer did not occlude later geometry");
			DepthIs(check,.5f,"only opaque occluder writes depth");
		}
		// Failure after entering the state scope must also restore all indexed state.
		GLuint undefined = 0; glGenTextures(1,&undefined); glBindTexture(GL_TEXTURE_2D,undefined);
		draw = mesh.Draw(undefined); draw.blendMode = BlendMode::LerpByAlpha;
		const auto state = Snapshot(white.name);
		const auto draws = backend.GetDiagnostics().draws;
		check.Test(backend.Draw(program,draw).code == Code::InvalidInput,"blend rejects undefined borrowed texture");
		check.Test(Snapshot(white.name) == state && backend.GetDiagnostics().draws == draws,"blend failure restores indexed state without a draw");
		glDeleteTextures(1,&undefined);
	} else {
		for (BlendMode mode : BlendModes) {
			ClearBlendTarget(target,destination); SourceColor(backend,{61,103,149,128});
			draw.blendMode = mode;
			const auto state = Snapshot(white.name); const auto count = backend.GetDiagnostics().draws;
			check.Test(backend.Draw(program,draw).code == Code::Unsupported,"3.3 without extension rejects blended draw");
			check.Test(Snapshot(white.name) == state && backend.GetDiagnostics().draws == count,"unsupported blending does not mutate state or draw");
			PixelIs(check,32,32,destination,"unsupported blending leaves destination untouched");
		}
	}
	for (int invalid : {-1,7,INT32_MAX}) {
		draw = mesh.Draw(white.name); draw.blendMode = static_cast<BlendMode>(invalid);
		ClearBlendTarget(target,destination); SourceColor(backend,{61,103,149,128});
		const auto state = Snapshot(white.name); const auto count = backend.GetDiagnostics().draws;
		check.Test(backend.Draw(program,draw).code == Code::InvalidInput,"invalid blend mode rejected");
		check.Test(Snapshot(white.name) == state && backend.GetDiagnostics().draws == count,"invalid blend mode preserves GL state and draw count");
		PixelIs(check,32,32,destination,"invalid blend mode leaves pixels untouched");
	}
	CleanRaster(); Registers(backend);
}

/** Hide real context entrypoints only through the existing host callback, not a production flag. */
void BlendCapabilityTests(Checks& check, Host& host, const Bytes& archive) {
	Target target; Texture white(true); Mesh mesh;
	for (unsigned hidden : {1u,2u,3u}) {
		host.hiddenBlendFunctions = hidden; host.deniedBlendLookups = 0;
		Result status; auto backend = ShaderBackend::Create(host.Binding(),{},status);
		check.Good(status,"missing optional blend entrypoints do not prevent backend creation");
		if (!backend) continue;
		ProgramRef program;
		check.Good(backend->LoadBasicFxPair0({archive.data(),archive.size()},program),"opaque pair loads with missing optional entrypoints");
		SourceColor(*backend,{61,103,149,128});
		for (BlendMode mode : BlendModes) {
			auto draw = mesh.Draw(white.name); draw.blendMode = mode;
			target.Clear(); const auto state = Snapshot(white.name); const auto count = backend->GetDiagnostics().draws;
			check.Test(backend->Draw(program,draw).code == Code::Unsupported,"incomplete blend entrypoints reject non-Off draw");
			check.Test(Snapshot(white.name) == state && backend->GetDiagnostics().draws == count,"missing entrypoint rejection is state-atomic");
			PixelIs(check,32,32,ClearPixel,"missing entrypoint cannot produce opaque fallback");
			DepthIs(check,1,"missing entrypoint leaves depth untouched");
		}
		if (IndexedBlendAvailable()) check.Test(host.deniedBlendLookups > 0,"host actually withheld indexed blend entrypoint");
		for (bool mirrored : {false,true}) {
			auto draw = mesh.Draw(white.name); draw.cullMode = mirrored ? CullMode::Disabled : CullMode::Back;
			draw.alphaTest = AlphaTest::GreaterEqual;
			SourceColor(*backend,{61,103,149,128},mirrored); target.Clear();
			check.Good(backend->Draw(program,draw),"Off with alpha/culling works without optional blend entrypoints");
			PixelIs(check,32,32,{61,103,149,128},"Off retains RGBA output");
			DepthIs(check,.25f,"Off retains opaque depth write");
		}
		check.Good(backend->Shutdown(),"missing-entrypoint backend shutdown");
		check.Test(backend->GetDiagnostics().liveBytes == 0,"missing-entrypoint backend balances allocations");
	}
	host.hiddenBlendFunctions = 0; CleanRaster();
}

#include "family_pixel_tests.inc"
#include "baked_pixel_tests.inc"

void Run(Checks& check, Host& host, const Bytes& archive, ProgramRef& late) {
	Target target; Texture texture, white(true); Mesh mesh;
	Result status;
	glColorMaski(0,1,0,1,0); glColorMaski(1,0,1,0,1);
	glEnablei(GL_BLEND,0); glDisablei(GL_BLEND,1);
	const auto creationState = Snapshot(texture.name);
	auto backend = ShaderBackend::Create(host.Binding(),{},status);
	check.Good(status,"create system-header backend");
	if (!backend) return;
	check.Test(Snapshot(texture.name)==creationState,"create preserves indexed buffer masks and blend enables");
	auto duplicate=ShaderBackend::Create(host.Binding(),{},status);
	check.Test(!duplicate && status.code==Code::InvalidInput,"duplicate owner for current context rejected");
	const size_t createCalls = backend->GetDiagnostics().allocationCalls;
	std::cout << "CREATE allocation_calls=" << createCalls << '\n';
	ProgramRef program;
	check.Good(backend->LoadBasicFxPair0({archive.data(),archive.size()},program),"load real BasicFX pair0");
	check.Test(Snapshot(texture.name)==creationState,"load preserves indexed buffer masks and blend enables");
	CleanRaster();
	const size_t loadCalls = backend->GetDiagnostics().allocationCalls;
	std::cout << "LOAD allocation_calls=" << loadCalls << '\n';
	const auto draw = mesh.Draw(texture.name);
	target.Clear();
	check.Test(backend->Draw(program,draw).code == Code::UninitializedRegisters,"required registers must be initialized");
	PixelIs(check,32,32,ClearPixel,"missing constants leave pixels untouched");
	Registers(*backend);
	{
		Target second;
		glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,second.color,0);
		const GLenum buffers[] = {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
		glDrawBuffers(2,buffers); target.Clear();
		const auto before = backend->GetDiagnostics().draws;
		check.Test(backend->Draw(program,draw).code==Code::Unsupported,"multiple color outputs rejected");
		check.Test(backend->GetDiagnostics().draws==before,"rejected MRT does not count a draw");
		PixelIs(check,32,32,ClearPixel,"MRT first attachment untouched");
		glReadBuffer(GL_COLOR_ATTACHMENT1);
		PixelIs(check,32,32,ClearPixel,"MRT second attachment untouched");
		glReadBuffer(GL_COLOR_ATTACHMENT0); glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,0,0);
	}
	target.Clear();
	check.Good(backend->Draw(program,draw),"draw original padded layout/base vertex/index prefix");
	PixelIs(check,32,32,{255,0,0,255},"identity red texel");
	PixelIs(check,2,2,ClearPixel,"outside geometry untouched");
	float depth = 0; glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
	check.Test(std::abs(depth-.25f)<0.00001f,"D3D depth .25 maps to window depth .25");
	RasterTests(check, *backend, program, target);
	BlendTests(check, *backend, program, target, archive);

	Registers(*backend,Identity,Identity,{0,0,0,0},{.5f,.25f,.75f,.5f},{.1f,.2f,.3f,.1f}); target.Clear();
	check.Good(backend->Draw(program,draw),"diffuse mul/add draw");
	PixelIs(check,32,32,{153,51,77,153},"deterministic diffuse mul/add");
	Float4 uv{.5f,0,0,0};
	check.Good(backend->SetFloat4(Stage::Vertex,20,{&uv,1}),"VS c20 changes independently"); target.Clear();
	check.Good(backend->Draw(program,draw),"separate stage c20 draw");
	PixelIs(check,32,32,{26,115,77,153},"VS UV change preserves PS c20 multiplier");
	Registers(*backend,Identity,Identity,{1,-1,0,0}); target.Clear();
	check.Good(backend->Draw(program,draw),"explicit repeat wrapping draw");
	PixelIs(check,32,32,{255,0,0,255},"out-of-range UV repeats rather than clamps");
	Registers(*backend,Identity,Identity,{0,1,0,0}); target.Clear();
	check.Good(backend->Draw(program,draw),"original vOffset plus one draw");
	PixelIs(check,32,32,{255,0,0,255},"original vOffset plus one remains wrapped");

	const std::array<Float4,4> world{{{.8f,.2f,0,.15f},{-.1f,.6f,0,-.05f},{0,0,1,.1f},{0,0,0,1}}};
	const std::array<Float4,4> view{{{.9f,-.15f,0,-.1f},{.25f,1.1f,0,.2f},{0,0,1,0},{0,0,0,1}}};
	Registers(*backend,world,view); target.Clear();
	check.Good(backend->Draw(program,draw),"nonsymmetric WORLD and ViewProjection draw");
	check.Test(Coverage(mesh,world,view),"independent transformed-coverage pixel oracle");
	Registers(*backend);
	for (float z : {-.25f,0.f,1.f,1.25f}) {
		Mesh clipped; clipped.Z(z,z); target.Clear();
		glEnable(GL_DEPTH_CLAMP);
		check.Good(backend->Draw(program,clipped.Draw(texture.name)),"clip boundary draw z="+std::to_string(z));
		PixelIs(check,32,32,z>=0 && z<=1 ? Pixel{255,0,0,255} : ClearPixel,"near/far expected coverage");
		check.Test(glIsEnabled(GL_DEPTH_CLAMP), "incoming depth clamp is restored");
		glDisable(GL_DEPTH_CLAMP);
	}
	Mesh clipped; clipped.Z(-.25f,.75f); target.Clear();
	check.Good(backend->Draw(program,clipped.Draw(texture.name)),"near-plane straddling triangles");
	PixelIs(check,20,32,ClearPixel,"near-clipped portion absent"); PixelIs(check,40,32,{255,0,0,255},"near-valid portion present");
	clipped.Z(.25f,1.25f); target.Clear();
	check.Good(backend->Draw(program,clipped.Draw(texture.name)),"far-plane straddling triangles");
	PixelIs(check,20,32,{255,0,0,255},"far-valid portion present"); PixelIs(check,44,32,ClearPixel,"far-clipped portion absent");
	auto reversed = draw; reversed.frontFace = Winding::Clockwise; target.Clear();
	check.Good(backend->Draw(program,reversed),"explicit clockwise culling draw");
	PixelIs(check,32,32,ClearPixel,"original counterclockwise faces culled");

	std::vector<uint32_t> wide{99,99,0,1,2,0,2,3}; auto wideDraw = draw;
	wideDraw.indexType = IndexType::UInt32; wideDraw.indexData = {wide.data(),wide.size()*4}; target.Clear();
	check.Good(backend->Draw(program,wideDraw),"32-bit indices with prefix and base vertex");
	PixelIs(check,32,32,{255,0,0,255},"32-bit indexed pixel");
	wide = {99,99,2,3,4,2,4,5}; wideDraw.baseVertex = -1; target.Clear();
	check.Good(backend->Draw(program,wideDraw),"valid negative base vertex");
	PixelIs(check,32,32,{255,0,0,255},"negative-base indexed pixel");

	auto reject = [&](IndexedDraw invalid, const char* name) {
		target.Clear(); const auto before = backend->GetDiagnostics().draws;
		check.Test(!backend->Draw(program,invalid),name);
		check.Test(before == backend->GetDiagnostics().draws && Near(ReadPixel(32,32),ClearPixel),"rejected draw changes no pixels/count");
	};
	auto invalid = draw; invalid.stride = 0; reject(invalid,"zero stride rejected");
	invalid = draw; invalid.uvOffset = invalid.stride-4; reject(invalid,"attribute past stride rejected");
	invalid = draw; invalid.vertices.size -= 16; reject(invalid,"last referenced vertex truncated");
	invalid = draw; invalid.indexData.size -= 1; reject(invalid,"partial last index rejected");
	invalid = draw; invalid.firstIndex = SIZE_MAX; reject(invalid,"index arithmetic overflow rejected");
	invalid = draw; invalid.baseVertex = INT32_MIN; reject(invalid,"negative resolved vertex rejected");
	invalid = draw; invalid.texture = 0; reject(invalid,"zero borrowed texture rejected");
	invalid = draw; invalid.viewportWidth = 0; reject(invalid,"zero viewport rejected");
	Mesh nonfinite; auto vertex = nonfinite.Get(0); vertex.position[0] = std::numeric_limits<float>::infinity(); nonfinite.Put(0,vertex);
	reject(nonfinite.Draw(texture.name),"nonfinite geometry rejected");
	Float4 bad{std::numeric_limits<float>::quiet_NaN(),0,0,0}; target.Clear();
	check.Test(!backend->SetFloat4(Stage::Pixel,20,{&bad,1}),"nonfinite register update rejected");
	uv = {0,0,0,0}; check.Good(backend->SetFloat4(Stage::Vertex,20,{&uv,1}),"partial correction accepted as data");
	check.Test(backend->Draw(program,draw).code == Code::UninitializedRegisters,"partial correction cannot draw stale register frame");
	PixelIs(check,32,32,ClearPixel,"bad update no fallback pixels");
	Registers(*backend);
	check.Test(!backend->SetFloat4(Stage::Vertex,UINT32_MAX,{&uv,1}),"register index overflow rejected");
	check.Test(backend->Draw(program,draw).code == Code::UninitializedRegisters,"out-of-bounds write also invalidates frame");
	Registers(*backend);

	{
		Foreign foreign(archive,mesh);
		target.Clear();
		glUseProgram(foreign.program); glBindVertexArray(foreign.vao);
		glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,white.name); glBindSampler(0,foreign.sampler);
		glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,texture.name);
		glEnable(GL_BLEND); glBlendFuncSeparate(GL_ONE,GL_ONE,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_ADD);
		glDisable(GL_DEPTH_TEST); glDepthFunc(GL_GREATER); glDepthMask(0); glDepthRange(.2,.7);
		glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); glFrontFace(GL_CW);
		glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,.9f);
		glEnable(GL_SCISSOR_TEST); glScissor(0,0,1,1); glEnable(GL_DITHER);
		glColorMask(0,1,0,0); glViewport(3,4,17,19); glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
		glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(2,3);
		glColorMaski(1, 1, 0, 1, 1); glDisablei(GL_BLEND, 1);
		glEnable(GL_PRIMITIVE_RESTART); glPrimitiveRestartIndex(1);
		const auto before = Snapshot(texture.name);
		check.Good(backend->Draw(program,draw),"draw overrides nondefault foreign state");
		check.Test(Snapshot(texture.name) == before,"all captured generic/program/buffer/texture/raster state restored");
		PixelIs(check,32,32,{255,0,0,255},"nondefault incoming state does not corrupt production pixel");
		glDisable(GL_PRIMITIVE_RESTART);
		CleanRaster(); target.Clear();
		glActiveTexture(GL_TEXTURE0);
		// Use the restored foreign program, VAO, EBO and sampler, without rebinding them.
		glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,nullptr);
		PixelIs(check,32,32,{0,0,255,255},"foreign shader draw works immediately after backend");
		check.Test(glGetError()==GL_NO_ERROR,"foreign program follow-up has no GL error");
	}
	CleanRaster(); glUseProgram(0); glBindVertexArray(0); target.Clear();
	check.Good(backend->Draw(program,draw),"backend after foreign shader cleanup");
	GLint restoredProgram=-1; glGetIntegerv(GL_CURRENT_PROGRAM,&restoredProgram);
	check.Test(restoredProgram==0,"fixed-function program zero restored");
	glDisable(GL_TEXTURE_2D); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
	glColor4f(0,1,0,1); glBegin(GL_QUADS);
	glVertex2f(-.5f,-.5f); glVertex2f(.5f,-.5f); glVertex2f(.5f,.5f); glVertex2f(-.5f,.5f); glEnd();
	PixelIs(check,32,32,{0,255,0,255},"fixed-function draw immediately follows backend");
	check.Test(glGetError()==GL_NO_ERROR,"fixed-function follow-up has no GL error");

	// Exercise the module's own bounded directory admission, not only the earlier parser proof.
	auto malformed = [&](Bytes bytes, const char* name) {
		const auto previous = program;
		target.Clear();
		check.Test(!backend->LoadBasicFxPair0({bytes.data(),bytes.size()},program) && program.Empty(),name);
		check.Test(backend->Draw(previous,draw).code==Code::InvalidReference,"malformed reload invalidates prior drawable ticket");
		PixelIs(check,32,32,ClearPixel,"malformed reload leaves framebuffer unchanged");
		check.Good(backend->LoadBasicFxPair0({archive.data(),archive.size()},program),"reload after malformed directory");
		Registers(*backend);
	};
	auto put32 = [](Bytes& bytes, size_t at, uint32_t value) {
		for (unsigned i=0; i<4; ++i) bytes.at(at+i)=static_cast<unsigned char>(value>>(8*i));
	};
	Bytes corrupt=archive; put32(corrupt,0,UINT32_MAX); malformed(corrupt,"oversized directory rejected");
	corrupt=archive; put32(corrupt,4,4); malformed(corrupt,"offset inside directory rejected");
	corrupt=archive; put32(corrupt,4,773); malformed(corrupt,"misaligned stage offset rejected");
	corrupt=archive; put32(corrupt,4,UINT32_MAX); malformed(corrupt,"out-of-archive offset rejected");
	corrupt=archive; put32(corrupt,4,1236); malformed(corrupt,"vertex alias to pixel stage rejected");
	corrupt=archive; put32(corrupt,1232,0); malformed(corrupt,"missing END envelope rejected");
	corrupt=archive; put32(corrupt,776,0xffffffffU); malformed(corrupt,"malformed selected bytecode rejected by MojoShader");

	const ProgramRef old = program;
	check.Test(!backend->LoadBasicFxPair0({},program) && program.Empty(),"failed transactional load clears caller output");
	check.Test(backend->Draw(old,draw).code==Code::InvalidReference,"failed reload cannot retain old drawable success");
	check.Good(backend->LoadBasicFxPair0({archive.data(),archive.size()},program),"reload good archive");
	Registers(*backend);
	check.Test(backend->Draw(old,draw).code==Code::InvalidReference,"successful reload invalidates old serial");
	Result workerStatus;
	std::thread worker([&] { workerStatus=backend->Shutdown(); }); worker.join();
	check.Test(workerStatus.code==Code::WrongThread,"shutdown from wrong thread fails before GL");
	host.ReleaseCurrent();
	check.Test(backend->Shutdown().code==Code::WrongContext,"shutdown with no context fails explicitly");
	check.Test(backend->GetDiagnostics().liveBytes>0,"failed shutdown does not claim cleanup");
	host.MakeCurrent();
	{
		Host other(2); other.Open(); Result otherStatus;
		auto second=ShaderBackend::Create(other.Binding(),{},otherStatus);
		check.Good(otherStatus,"second context backend");
		if (second) {
			ProgramRef secondProgram;
			check.Good(second->LoadBasicFxPair0({archive.data(),archive.size()},secondProgram),"second context program");
			Registers(*second);
			check.Test(second->Draw(program,draw).code==Code::InvalidReference,"cross-backend generation rejected before texture use");
			check.Test(backend->Shutdown().code==Code::WrongContext,"shutdown under other context rejected");
			check.Good(second->Shutdown(),"second context shutdown");
			check.Test(second->GetDiagnostics().liveBytes==0,"second context allocation balance");
		}
	}
	host.MakeCurrent(); late=program;
	glColorMaski(0,1,0,1,0); glColorMaski(1,0,1,0,1);
	glEnablei(GL_BLEND,0); glDisablei(GL_BLEND,1);
	const auto shutdownState = Snapshot(texture.name);
	check.Good(backend->Shutdown(),"owning-context shutdown");
	check.Test(Snapshot(texture.name)==shutdownState,"shutdown preserves indexed buffer masks and blend enables");
	CleanRaster();
	check.Test(backend->GetDiagnostics().liveBytes==0,"all tracked allocations released");
	check.Good(backend->Shutdown(),"shutdown idempotent");
	check.Test(glIsTexture(texture.name),"borrowed texture survives backend shutdown");
	auto fresh=ShaderBackend::Create(host.Binding(),{},status);
	check.Good(status,"recreated backend on same host context");
	if (fresh) {
		ProgramRef next;
		check.Good(fresh->LoadBasicFxPair0({archive.data(),archive.size()},next),"new generation load"); Registers(*fresh);
		check.Test(fresh->Draw(late,draw).code==Code::InvalidReference,"old ticket rejected despite same host context identity");
		target.Clear(); check.Good(fresh->Draw(next,draw),"new generation draw"); PixelIs(check,32,32,{255,0,0,255},"new generation expected pixel");
		check.Good(fresh->Shutdown(),"new generation shutdown");
	}
	Limits none; none.liveAllocationBytes=0;
	auto noMemory=ShaderBackend::Create(host.Binding(),none,status);
	check.Test(!noMemory && status.code==Code::AllocationFailure,"context allocation refusal no usable owner");
	Limits limited; limited.allocationCalls=createCalls+70;
	auto partial=ShaderBackend::Create(host.Binding(),limited,status);
	check.Good(status,"bounded allocator permits context before load failure");
	if (partial) {
		ProgramRef failed=late;
		check.Test(!partial->LoadBasicFxPair0({archive.data(),archive.size()},failed) && failed.Empty(),"partial-stage allocation failure clears output");
		check.Test(partial->GetDiagnostics().refusedAllocations>0,"partial-stage allocator refusal observed");
		check.Good(partial->Shutdown(),"partial-stage failure shutdown");
		check.Test(partial->GetDiagnostics().liveBytes==0,"partial-stage failure ownership balanced");
	}
	// For this fixed pair the last link allocation is PS float uniforms, after the VS ref increment.
	Limits lateLimit; lateLimit.allocationCalls=loadCalls-1;
	auto lateFailure=ShaderBackend::Create(host.Binding(),lateLimit,status);
	check.Good(status,"late-link allocator permits context");
	if (lateFailure) {
		ProgramRef failed=late;
		check.Test(!lateFailure->LoadBasicFxPair0({archive.data(),archive.size()},failed) && failed.Empty(),"last link allocation refusal clears output");
		std::cout << "LATE_LINK live_before_shutdown=" << lateFailure->GetDiagnostics().liveBytes
			<< " calls=" << lateFailure->GetDiagnostics().allocationCalls << '\n';
		check.Test(lateFailure->GetDiagnostics().refusedAllocations>0,"late-link refusal observed");
		const auto shutdown=lateFailure->Shutdown();
		check.Good(shutdown,"late-link failure shutdown balances ownership");
		check.Test(lateFailure->Shutdown().code==shutdown.code,"repeated shutdown cannot claim cleanup after failed release");
		std::cout << "LATE_LINK live_after_shutdown=" << lateFailure->GetDiagnostics().liveBytes << '\n';
		check.Test(lateFailure->GetDiagnostics().liveBytes==0,"late-link failure has no orphan stage");
	}
	// Stop allocation at each selected-pair load step, including parser and link setup.
	for (size_t calls = createCalls; calls < loadCalls; ++calls) {
		Limits cap; cap.allocationCalls = calls;
		auto trial = ShaderBackend::Create(host.Binding(),cap,status);
		check.Good(status,"allocation sweep context at call "+std::to_string(calls));
		if (!trial) continue;
		ProgramRef failed = late;
		check.Test(!trial->LoadBasicFxPair0({archive.data(),archive.size()},failed) && failed.Empty(),
			"allocation sweep clears failed program at call "+std::to_string(calls));
		check.Test(trial->GetDiagnostics().refusedAllocations>0,"allocation sweep reaches refusal");
		check.Good(trial->Shutdown(),"allocation sweep shutdown");
		check.Test(trial->GetDiagnostics().liveBytes==0,"allocation sweep leaves no owned payload");
	}
	check.Test(glGetError()==GL_NO_ERROR,"final clean GL error state");
}
}

int main(int argc, char** argv) {
	std::cout << std::unitbuf;
	const bool familyOnly = argc == 3 && !std::strcmp(argv[2],"--family-only");
	const bool bakedOnly = argc == 4 && !std::strcmp(argv[3],"--baked-only");
	const bool withBaked = (argc == 3 && !familyOnly) || bakedOnly;
	if ((argc != 2 && !familyOnly && !withBaked) || !std::getenv("PW_BACKEND_APPROVED_XVFB")) {
		std::cerr << "run only using the parent-approved isolated wrapper\n"; return 2;
	}
	Checks check;
	try {
		const auto archive=Read(argv[1]); ProgramRef late; BasicFxOpacityPrograms lateFamily; StaticPrograms lateStatic;
		const auto baked = withBaked ? Read(argv[2]) : Bytes{};
		{
			Host host(1); host.Open();
			std::cout << "GL renderer=" << glGetString(GL_RENDERER) << " version=" << glGetString(GL_VERSION) << '\n';
			if (!familyOnly && !bakedOnly) {
				Run(check,host,archive,late);
				BlendCapabilityTests(check,host,archive);
			}
			if (!bakedOnly) FamilyTests(check,host,archive,lateFamily);
			if (withBaked) BakedTests(check,host,archive,baked,lateStatic);
		}
		check.Test(glXGetCurrentContext()==nullptr,"host GL context destroyed before late reference release");
		{
			Host recreated(3); recreated.Open(); Target target; Texture texture; Mesh mesh;
			Result status;
			auto backend=ShaderBackend::Create(recreated.Binding(),{},status);
			check.Good(status,"backend after actual host context destruction/recreation");
			if (backend) {
				ProgramRef program;
				check.Good(backend->LoadBasicFxPair0({archive.data(),archive.size()},program),"recreated host loads real pair");
				Registers(*backend); target.Clear();
				check.Test(backend->Draw(late,mesh.Draw(texture.name)).code==Code::InvalidReference,"prior destroyed-context ticket rejected");
				for (const auto& ticket : FamilyTickets(lateFamily))
					check.Test(backend->Draw(ticket,mesh.Draw(texture.name)).code==Code::InvalidReference,"prior destroyed-context family ticket rejected");
				if (withBaked) for (const auto& ticket : StaticTickets(lateStatic))
					check.Test(backend->Draw(ticket,mesh.Draw(texture.name)).code==Code::InvalidReference,"prior destroyed-context static ticket rejected");
				check.Good(backend->Draw(program,mesh.Draw(texture.name)),"recreated host draws");
				PixelIs(check,32,32,{255,0,0,255},"recreated host exact pixel");
				check.Good(backend->Shutdown(),"recreated host shutdown");
				check.Test(backend->GetDiagnostics().liveBytes==0,"recreated host ownership balanced");
			}
		}
		std::thread release([reference=std::move(late),family=std::move(lateFamily),statics=std::move(lateStatic)]() mutable {
			reference.Reset(); family = {}; statics = {};
		}); release.join();
		check.Test(true,"last CPU reference release without context/on other thread");
	} catch (const std::exception& e) { check.Test(false,std::string("exception: ")+e.what()); }
	return check.Finish();
}
