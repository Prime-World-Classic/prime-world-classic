#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include "shader_backend.hpp"
#include "mojoshader.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#include <vector>

namespace pwshader {
namespace {
Result Fail(Code code, const char* message) { return {code, message}; }
using Bytes = std::vector<unsigned char>;
constexpr unsigned RequiredVS[] = {0,1,2,3,8,9,10,11,20};
constexpr unsigned RequiredPS[] = {20,21};
std::atomic<uint64_t> NextGeneration{1};
thread_local std::vector<ContextKey> Owners;

struct Budget {
	Limits limits;
	size_t live = 0, peak = 0, calls = 0, total = 0, refused = 0;
	struct alignas(std::max_align_t) Header { size_t size; };
	static void* Allocate(int bytes, void* user) {
		auto& b = *static_cast<Budget*>(user);
		const size_t size = bytes == 0 ? 1 : size_t(bytes);
		if (bytes < 0 || b.calls >= b.limits.allocationCalls ||
			size > b.limits.liveAllocationBytes - b.live || size > b.limits.totalAllocationBytes - b.total) {
			++b.refused;
			return nullptr;
		}
		auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
		if (!header) { ++b.refused; return nullptr; }
		header->size = size;
		b.live += size; b.total += size; ++b.calls;
		b.peak = std::max(b.peak, b.live);
		return header + 1;
	}
	static void Free(void* pointer, void* user) {
		if (!pointer) return;
		auto& b = *static_cast<Budget*>(user);
		auto* header = static_cast<Header*>(pointer) - 1;
		if (header->size > b.live) std::terminate();
		b.live -= header->size;
		std::free(header);
	}
};

void Enable(GLenum state, bool value) { if (value) glEnable(state); else glDisable(state); }

/** Optional per-context functions; their absence must not disable the GL 3.3 opaque path. */
struct IndexedBlendFunctions {
	PFNGLBLENDFUNCSEPARATEIPROC function = nullptr;
	PFNGLBLENDEQUATIONSEPARATEIPROC equation = nullptr;
	void Load(const ContextBinding& owner, GLint major) {
		const bool core = major >= 4;
		bool supported = core;
		if (!core) {
			GLint count = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &count);
			for (GLint i = 0; i < count; ++i) {
				const auto* name = glGetStringi(GL_EXTENSIONS, GLuint(i));
				if (name && !std::strcmp(reinterpret_cast<const char*>(name), "GL_ARB_draw_buffers_blend")) {
					supported = true;
					break;
				}
			}
		}
		if (!supported) return;
		function = reinterpret_cast<PFNGLBLENDFUNCSEPARATEIPROC>(
			owner.lookup(core ? "glBlendFuncSeparatei" : "glBlendFuncSeparateiARB", owner.user));
		equation = reinterpret_cast<PFNGLBLENDEQUATIONSEPARATEIPROC>(
			owner.lookup(core ? "glBlendEquationSeparatei" : "glBlendEquationSeparateiARB", owner.user));
	}
	bool Available() const { return function && equation; }
};

/** Only private VAO state is modified; global state and the borrowed texture are restored. */
struct GlState {
	GLint program = 0, vao = 0, array = 0, element = 0, active = 0, texture = 0, sampler = 0;
	GLint viewport[4]{}, depthFunction = 0, cullMode = 0, face = 0, polygon[2]{};
	GLint alphaFunction = 0;
	GLfloat alphaReference = 0;
	GLboolean depthMask = 0, colorMask[4]{}, blend = 0;
	GLdouble depthRange[2]{};
	static constexpr GLenum Flags[] = {GL_DEPTH_TEST, GL_CULL_FACE, GL_ALPHA_TEST,
		GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_DITHER, GL_FRAMEBUFFER_SRGB, GL_RASTERIZER_DISCARD,
		GL_PROGRAM_POINT_SIZE, GL_POLYGON_OFFSET_FILL, GL_PRIMITIVE_RESTART, GL_DEPTH_CLAMP, GL_COLOR_LOGIC_OP};
	GLboolean flags[sizeof(Flags) / sizeof(Flags[0])]{};
	GLuint borrowed = 0;
	static constexpr GLenum Parameters[] = {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_MIN_FILTER,
		GL_TEXTURE_MAG_FILTER, GL_TEXTURE_BASE_LEVEL, GL_TEXTURE_MAX_LEVEL};
	GLint parameters[6]{};
	const IndexedBlendFunctions* indexedBlend;
	static constexpr GLenum BlendParameters[] = {GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB,
		GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA};
	GLint blendParameters[6]{};
	explicit GlState(const IndexedBlendFunctions* functions = nullptr) : indexedBlend(functions) {
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array);
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element);
		glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
		glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
		glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
		glActiveTexture(GLenum(active));
		glGetIntegerv(GL_VIEWPORT, viewport);
		glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
		glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
		glGetIntegerv(GL_FRONT_FACE, &face);
		glGetIntegerv(GL_ALPHA_TEST_FUNC, &alphaFunction);
		glGetFloatv(GL_ALPHA_TEST_REF, &alphaReference);
		glGetIntegerv(GL_POLYGON_MODE, polygon);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
		glGetBooleani_v(GL_COLOR_WRITEMASK, 0, colorMask);
		blend = glIsEnabledi(GL_BLEND, 0);
		if (indexedBlend) for (size_t i = 0; i < std::size(BlendParameters); ++i)
			glGetIntegeri_v(BlendParameters[i], 0, &blendParameters[i]);
		glGetDoublev(GL_DEPTH_RANGE, depthRange);
		for (size_t i = 0; i < std::size(Flags); ++i) flags[i] = glIsEnabled(Flags[i]);
	}
	void Borrow(GLuint name) {
		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, 0);
		glBindTexture(GL_TEXTURE_2D, name);
		// A wrong-target name leaves an error and must not be treated as the bound texture.
		GLint bound = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
		if (GLuint(bound) != name) return;
		borrowed = name;
		for (size_t i = 0; i < std::size(Parameters); ++i)
			glGetTexParameteriv(GL_TEXTURE_2D, Parameters[i], &parameters[i]);
	}
	~GlState() {
		glActiveTexture(GL_TEXTURE0);
		if (borrowed) {
			glBindTexture(GL_TEXTURE_2D, borrowed);
			for (size_t i = 0; i < std::size(Parameters); ++i)
				glTexParameteri(GL_TEXTURE_2D, Parameters[i], parameters[i]);
		}
		glBindTexture(GL_TEXTURE_2D, GLuint(texture));
		glBindSampler(0, GLuint(sampler));
		glActiveTexture(GLenum(active));
		glBindVertexArray(GLuint(vao));
		glBindBuffer(GL_ARRAY_BUFFER, GLuint(array));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GLuint(element));
		glUseProgram(GLuint(program));
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
		glDepthFunc(GLenum(depthFunction));
		glDepthMask(depthMask);
		glDepthRange(depthRange[0], depthRange[1]);
		glColorMaski(0, colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
		if (indexedBlend) {
			indexedBlend->function(0, GLenum(blendParameters[0]), GLenum(blendParameters[1]),
				GLenum(blendParameters[2]), GLenum(blendParameters[3]));
			indexedBlend->equation(0, GLenum(blendParameters[4]), GLenum(blendParameters[5]));
		}
		if (blend) glEnablei(GL_BLEND, 0); else glDisablei(GL_BLEND, 0);
		glCullFace(GLenum(cullMode));
		glFrontFace(GLenum(face));
		glAlphaFunc(GLenum(alphaFunction), alphaReference);
		glPolygonMode(GL_FRONT, GLenum(polygon[0]));
		glPolygonMode(GL_BACK, GLenum(polygon[1]));
		for (size_t i = 0; i < std::size(Flags); ++i) Enable(Flags[i], flags[i]);
	}
};

/** Keep Mojo's cached binding empty before restoring the host's unrelated GL program/VAO. */
struct MojoScope {
	MOJOSHADER_glContext* context;
	GLuint vao;
	MojoScope(MOJOSHADER_glContext* c, GLuint v) : context(c), vao(v) {
		MOJOSHADER_glMakeContextCurrent(context);
		glBindVertexArray(vao);
	}
	~MojoScope() {
		glBindVertexArray(vao);
		MOJOSHADER_glBindProgram(nullptr);
		MOJOSHADER_glMakeContextCurrent(nullptr);
	}
};

struct Pair {
	MOJOSHADER_glShader* vs = nullptr;
	MOJOSHADER_glShader* ps = nullptr;
	MOJOSHADER_glProgram* linked = nullptr;
	void Clear() {
		if (linked) MOJOSHADER_glDeleteProgram(linked);
		if (ps) MOJOSHADER_glDeleteShader(ps);
		if (vs) MOJOSHADER_glDeleteShader(vs);
		linked = nullptr; ps = nullptr; vs = nullptr;
	}
};

uint32_t Read32(const Bytes& b, size_t at) {
	return uint32_t(b[at]) | (uint32_t(b[at + 1]) << 8) |
		(uint32_t(b[at + 2]) << 16) | (uint32_t(b[at + 3]) << 24);
}
struct Span { size_t offset, size; };

Result Directory(const Bytes& b, Span (&selected)[2]) {
	if (b.size() < 4) return Fail(Code::InvalidInput, "short archive header");
	const size_t pairs = Read32(b, 0);
	if (pairs != 96 || pairs > (b.size() - 4) / 8)
		return Fail(Code::Unsupported, "expected the 96-pair BasicFX archive");
	const size_t end = 4 + pairs * 8;
	std::vector<size_t> offsets;
	for (size_t i = 0; i < pairs * 2; ++i) {
		const size_t o = Read32(b, 4 + i * 4);
		if (o < end || o >= b.size() || (o & 3)) return Fail(Code::InvalidInput, "invalid archive offset");
		offsets.push_back(o);
	}
	std::sort(offsets.begin(), offsets.end());
	offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
	for (size_t i = 0; i < pairs * 2; ++i) {
		const size_t o = Read32(b, 4 + i * 4);
		const auto next = std::upper_bound(offsets.begin(), offsets.end(), o);
		const size_t length = (next == offsets.end() ? b.size() : *next) - o;
		if (length < 8 || length > 1024 * 1024 || (length & 3) ||
			Read32(b, o) != (i % 2 ? 0xffff0300U : 0xfffe0300U) || Read32(b, o + length - 4) != 0x0000ffffU)
			return Fail(Code::InvalidInput, "invalid shader envelope");
		if (i < 2) selected[i] = {o, length};
	}
	return {};
}

bool ExactReflection(const MOJOSHADER_parseData* p, bool vertex) {
	if (!p || p->error_count || p->major_ver != 3 || p->minor_ver || p->preshader ||
		p->shader_type != (vertex ? MOJOSHADER_TYPE_VERTEX : MOJOSHADER_TYPE_PIXEL)) return false;
	const unsigned* registers = vertex ? RequiredVS : RequiredPS;
	const size_t count = vertex ? std::size(RequiredVS) : std::size(RequiredPS);
	if (p->uniform_count != int(count)) return false;
	for (size_t i = 0; i < count; ++i) {
		const auto& u = p->uniforms[i];
		if (u.type != MOJOSHADER_UNIFORM_FLOAT || u.array_count || u.constant || u.index != int(registers[i])) return false;
	}
	if (p->attribute_count != (vertex ? 2 : 1) || p->sampler_count != (vertex ? 0 : 1)) return false;
	if (vertex) {
		return p->attributes[0].usage == MOJOSHADER_USAGE_POSITION && p->attributes[0].index == 0 &&
			p->attributes[1].usage == MOJOSHADER_USAGE_TEXCOORD && p->attributes[1].index == 0;
	}
	return p->attributes[0].usage == MOJOSHADER_USAGE_TEXCOORD && p->attributes[0].index == 0 &&
		p->samplers[0].index == 0 && p->samplers[0].type == MOJOSHADER_SAMPLER_2D && !p->samplers[0].texbem;
}
}

struct ShaderBackend::Impl {
	ContextBinding owner;
	ContextKey key;
	std::thread::id thread = std::this_thread::get_id();
	uint64_t generation = NextGeneration.fetch_add(1), serial = 0, draws = 0;
	Budget budget;
	IndexedBlendFunctions blend;
	MOJOSHADER_glContext* context = nullptr;
	GLuint vao = 0, vertexBuffer = 0, indexBuffer = 0;
	bool registered = false, closed = false, ready = false;
	Pair pair;
	Bytes archive;
	std::array<Float4, 256> vs{};
	std::array<Float4, 224> ps{};
	std::array<bool, 256> vsInit{};
	std::array<bool, 224> psInit{};
	Result Check() const {
		if (std::this_thread::get_id() != thread) return Fail(Code::WrongThread, "render-thread-only backend");
		if (closed) return Fail(Code::Closed, "backend shut down");
		if (!(owner.current(owner.user) == key)) return Fail(Code::WrongContext, "owning context not current");
		return {};
	}
	void InvalidateRegisters() { vsInit.fill(false); psInit.fill(false); }
};

ShaderBackend::ShaderBackend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<ShaderBackend> ShaderBackend::Create(ContextBinding owner, Limits limits, Result& status) {
	status = {};
	if (!owner.current || !owner.lookup) { status = Fail(Code::InvalidInput, "missing context hooks"); return {}; }
	const auto key = owner.current(owner.user);
	if (!key.identity || !key.lifetime) { status = Fail(Code::WrongContext, "no live current context"); return {}; }
	if (std::find(Owners.begin(), Owners.end(), key) != Owners.end()) {
		status = Fail(Code::InvalidInput, "one Mojo backend per context"); return {};
	}
	GLint major = 0, minor = 0, profile = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
	if (glGetError() != GL_NO_ERROR || major < 3 || (major == 3 && minor < 3) ||
		!(profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)) {
		status = Fail(Code::Unsupported, "requires clean OpenGL 3.3 compatibility context"); return {};
	}
	std::unique_ptr<ShaderBackend> result;
	try {
		auto impl = std::make_unique<Impl>();
		impl->owner = owner; impl->key = key; impl->budget.limits = limits;
		result.reset(new ShaderBackend(std::move(impl)));
		auto& p = *result->impl_;
		p.blend.Load(owner, major);
		GlState state;
		p.context = MOJOSHADER_glCreateContext(MOJOSHADER_PROFILE_GLSL120, owner.lookup, owner.user,
			Budget::Allocate, Budget::Free, &p.budget);
		if (!p.context) { status = Fail(Code::AllocationFailure, "Mojo context creation failed"); return {}; }
		MOJOSHADER_glMakeContextCurrent(p.context);
		glGenVertexArrays(1, &p.vao);
		glGenBuffers(1, &p.vertexBuffer);
		glGenBuffers(1, &p.indexBuffer);
		if (!p.vao || !p.vertexBuffer || !p.indexBuffer || glGetError() != GL_NO_ERROR) {
			status = Fail(Code::DriverFailure, "private draw resources failed"); return {};
		}
		Owners.push_back(key);
		p.registered = true;
		MOJOSHADER_glMakeContextCurrent(nullptr);
		return result;
	} catch (const std::bad_alloc&) {
		status = Fail(Code::AllocationFailure, "backend allocation failed"); return {};
	}
}

ShaderBackend::~ShaderBackend() {
	if (impl_ && !impl_->closed && !Shutdown()) std::terminate();
}

Result ShaderBackend::LoadBasicFxPair0(ByteView source, ProgramRef& output) {
	output.Reset();
	auto& p = *impl_;
	auto status = p.Check();
	if (!status) return status;
	p.ready = false;
	p.InvalidateRegisters();
	if (!source.data || source.size < 4 || source.size > p.budget.limits.archiveBytes || source.size > 4 * 1024 * 1024)
		return Fail(Code::InvalidInput, "archive view out of bounds");
	try {
		const auto* begin = static_cast<const unsigned char*>(source.data);
		Bytes archive(begin, begin + source.size);
		Span spans[2]{};
		status = Directory(archive, spans);
		if (!status) return status;
		auto ticket = std::make_shared<ProgramRef::Ticket>(ProgramRef::Ticket{p.generation, p.serial + 1});
		GlState state;
		MojoScope scope(p.context, p.vao);
		Pair candidate;
		struct Cleanup { Pair& pair; ~Cleanup() { pair.Clear(); } } cleanup{candidate};
		candidate.vs = MOJOSHADER_glCompileShader(archive.data() + spans[0].offset, unsigned(spans[0].size), nullptr, 0, nullptr, 0);
		if (!candidate.vs) return Fail(Code::ShaderFailure, "vertex compilation failed");
		candidate.ps = MOJOSHADER_glCompileShader(archive.data() + spans[1].offset, unsigned(spans[1].size), nullptr, 0, nullptr, 0);
		if (!candidate.ps) return Fail(Code::ShaderFailure, "pixel compilation failed");
		if (!ExactReflection(MOJOSHADER_glGetShaderParseData(candidate.vs), true) ||
			!ExactReflection(MOJOSHADER_glGetShaderParseData(candidate.ps), false))
			return Fail(Code::Unsupported, "reflection differs from selected BasicFX pair0");
		candidate.linked = MOJOSHADER_glLinkProgram(candidate.vs, candidate.ps);
		if (!candidate.linked) return Fail(Code::ShaderFailure, "program link failed");
		if (glGetError() != GL_NO_ERROR) return Fail(Code::DriverFailure, "shader creation GL error");
		p.pair.Clear();
		p.pair = candidate;
		candidate = {};
		p.archive = std::move(archive);
		++p.serial;
		output.ticket_ = std::move(ticket);
		p.ready = true;
		return {};
	} catch (const std::bad_alloc&) { return Fail(Code::AllocationFailure, "archive/reference allocation failed"); }
}

Result ShaderBackend::SetFloat4(Stage stage, uint32_t first, Float4View values) {
	auto& p = *impl_;
	auto status = p.Check();
	if (!status) return status;
	const bool vertex = stage == Stage::Vertex;
	const size_t count = vertex ? p.vs.size() : p.ps.size();
	if ((stage != Stage::Vertex && stage != Stage::Pixel) || !values.data || !values.count ||
		first >= count || values.count > count - first) {
		p.InvalidateRegisters();
		return Fail(Code::InvalidInput, "register span out of bounds");
	}
	for (size_t i = 0; i < values.count; ++i) for (float f : values.data[i]) if (!std::isfinite(f)) {
		p.InvalidateRegisters();
		return Fail(Code::InvalidInput, "nonfinite register write");
	}
	for (size_t i = 0; i < values.count; ++i) {
		if (vertex) { p.vs[first + i] = values.data[i]; p.vsInit[first + i] = true; }
		else { p.ps[first + i] = values.data[i]; p.psInit[first + i] = true; }
	}
	return {};
}

Result ShaderBackend::Draw(const ProgramRef& reference, const IndexedDraw& d) {
	auto& p = *impl_;
	auto status = p.Check();
	if (!status) return status;
	if (!p.ready || !reference.ticket_ || reference.ticket_->backend != p.generation || reference.ticket_->serial != p.serial)
		return Fail(Code::InvalidReference, "empty, stale, foreign or failed-load program");
	for (unsigned i : RequiredVS) if (!p.vsInit[i]) return Fail(Code::UninitializedRegisters, "required VS register missing");
	for (unsigned i : RequiredPS) if (!p.psInit[i]) return Fail(Code::UninitializedRegisters, "required PS register missing");
	const size_t indexBytes = d.indexType == IndexType::UInt16 ? 2 : 4;
	if ((d.indexType != IndexType::UInt16 && d.indexType != IndexType::UInt32) ||
		!d.vertices.data || !d.indexData.data || !d.stride || d.stride > size_t(std::numeric_limits<GLsizei>::max()) ||
		d.vertices.size > p.budget.limits.geometryBytes || d.indexData.size > p.budget.limits.geometryBytes ||
		d.streamOffset > d.vertices.size || d.positionOffset > d.stride || d.stride - d.positionOffset < 12 ||
		d.uvOffset > d.stride || d.stride - d.uvOffset < 8 || !d.indexCount || d.indexCount % 3 ||
		d.indexCount > p.budget.limits.indices || d.indexCount > size_t(std::numeric_limits<GLsizei>::max()) ||
		d.firstIndex > d.indexData.size / indexBytes || d.indexCount > d.indexData.size / indexBytes - d.firstIndex ||
		d.viewportWidth <= 0 || d.viewportHeight <= 0 || d.viewportWidth > 16384 || d.viewportHeight > 16384 ||
		(d.frontFace != Winding::Clockwise && d.frontFace != Winding::CounterClockwise))
		return Fail(Code::InvalidInput, "draw layout/range out of bounds");
	if ((d.alphaTest != AlphaTest::Disabled && d.alphaTest != AlphaTest::GreaterEqual) ||
		(d.cullMode != CullMode::Back && d.cullMode != CullMode::Disabled) ||
		d.alphaReference < 0 || d.alphaReference > 255)
		return Fail(Code::InvalidInput, "invalid alpha-test or culling state");
	// Original RenderStatesManager factors; the transparent pass preserves alpha and depth.
	GLenum sourceFactor = GL_ONE, destinationFactor = GL_ZERO;
	switch (d.blendMode) {
	case BlendMode::Off: break;
	case BlendMode::LerpByAlpha: sourceFactor = GL_SRC_ALPHA; destinationFactor = GL_ONE_MINUS_SRC_ALPHA; break;
	case BlendMode::PremultipliedLerp: destinationFactor = GL_ONE_MINUS_SRC_ALPHA; break;
	case BlendMode::AddColor: destinationFactor = GL_ONE; break;
	case BlendMode::AddColorMulAlpha: sourceFactor = GL_SRC_ALPHA; destinationFactor = GL_ONE; break;
	case BlendMode::MulColor: sourceFactor = GL_ZERO; destinationFactor = GL_SRC_COLOR; break;
	case BlendMode::MulInvColor: sourceFactor = GL_ZERO; destinationFactor = GL_ONE_MINUS_SRC_COLOR; break;
	default: return Fail(Code::InvalidInput, "invalid blend mode");
	}
	const bool blended = d.blendMode != BlendMode::Off;
	if (blended && !p.blend.Available())
		return Fail(Code::Unsupported, "blending requires GL 4.0 or ARB_draw_buffers_blend with indexed entrypoints");
	try {
		std::vector<uint32_t> indices(d.indexCount);
		const size_t available = d.vertices.size - d.streamOffset;
		const size_t extent = std::max(d.positionOffset + 12, d.uvOffset + 8);
		const auto* vertices = static_cast<const unsigned char*>(d.vertices.data);
		const auto* indexData = static_cast<const unsigned char*>(d.indexData.data);
		for (size_t i = 0; i < d.indexCount; ++i) {
			uint32_t index = 0;
			if (indexBytes == 2) { uint16_t v; std::memcpy(&v, indexData + (d.firstIndex + i) * 2, 2); index = v; }
			else std::memcpy(&index, indexData + (d.firstIndex + i) * 4, 4);
			const int64_t resolved = int64_t(index) + d.baseVertex;
			if (resolved < 0 || uint64_t(resolved) > UINT32_MAX || available < extent ||
				uint64_t(resolved) > (available - extent) / d.stride)
				return Fail(Code::InvalidInput, "referenced vertex out of bounds");
			indices[i] = uint32_t(resolved);
			const size_t at = d.streamOffset + size_t(resolved) * d.stride;
			float position[3], uv[2];
			std::memcpy(position, vertices + at + d.positionOffset, sizeof(position));
			std::memcpy(uv, vertices + at + d.uvOffset, sizeof(uv));
			for (float f : position) if (!std::isfinite(f)) return Fail(Code::InvalidInput, "nonfinite vertex");
			for (float f : uv) if (!std::isfinite(f)) return Fail(Code::InvalidInput, "nonfinite UV");
		}
		if (!d.texture || !glIsTexture(d.texture)) return Fail(Code::InvalidInput, "borrowed texture is not live");
		// GLSL 1.20 gl_FragColor broadcasts to all active color outputs.
		GLint outputs = 0;
		glGetIntegerv(GL_MAX_DRAW_BUFFERS, &outputs);
		for (GLint i = 1; i < outputs; ++i) {
			GLint buffer = GL_NONE;
			glGetIntegerv(GL_DRAW_BUFFER0 + i, &buffer);
			if (buffer != GL_NONE) return Fail(Code::Unsupported, "BasicFX requires a single color output");
		}
		GlState state(blended ? &p.blend : nullptr);
		MojoScope scope(p.context, p.vao);
		state.Borrow(d.texture);
		GLint width = 0, height = 0;
		if (state.borrowed) {
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
		}
		if (!state.borrowed || width <= 0 || height <= 0 || glGetError() != GL_NO_ERROR)
			return Fail(Code::InvalidInput, "texture is not a defined level0 2D image");
		for (GLenum parameter : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T}) glTexParameteri(GL_TEXTURE_2D, parameter, GL_REPEAT);
		for (GLenum parameter : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER}) glTexParameteri(GL_TEXTURE_2D, parameter, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glBindBuffer(GL_ARRAY_BUFFER, p.vertexBuffer);
		glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(d.vertices.size), d.vertices.data, GL_STREAM_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.indexBuffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(uint32_t)), indices.data(), GL_STREAM_DRAW);
		MOJOSHADER_glBindProgram(p.pair.linked);
		for (unsigned i : RequiredVS) MOJOSHADER_glSetVertexShaderUniformF(i, p.vs[i].data(), 1);
		for (unsigned i : RequiredPS) MOJOSHADER_glSetPixelShaderUniformF(i, p.ps[i].data(), 1);
		MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_POSITION, 0, 3, MOJOSHADER_ATTRIBUTE_FLOAT, 0,
			unsigned(d.stride), reinterpret_cast<const void*>(d.streamOffset + d.positionOffset));
		MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_TEXCOORD, 0, 2, MOJOSHADER_ATTRIBUTE_FLOAT, 0,
			unsigned(d.stride), reinterpret_cast<const void*>(d.streamOffset + d.uvOffset));
		for (GLenum flag : GlState::Flags) glDisable(flag);
		glEnable(GL_DEPTH_TEST);
		if (d.cullMode == CullMode::Back) glEnable(GL_CULL_FACE);
		if (d.alphaTest == AlphaTest::GreaterEqual) {
			glAlphaFunc(GL_GEQUAL, d.alphaReference / 255.f);
			glEnable(GL_ALPHA_TEST);
		}
		glDepthFunc(GL_LEQUAL); glDepthMask(blended ? GL_FALSE : GL_TRUE); glDepthRange(0, 1);
		glCullFace(GL_BACK);
		glFrontFace(d.frontFace == Winding::CounterClockwise ? GL_CCW : GL_CW);
		glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, blended ? GL_FALSE : GL_TRUE);
		if (blended) {
			p.blend.function(0, sourceFactor, destinationFactor, GL_ONE, GL_ZERO);
			p.blend.equation(0, GL_FUNC_ADD, GL_FUNC_ADD);
			glEnablei(GL_BLEND, 0);
		} else glDisablei(GL_BLEND, 0);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glViewport(d.viewportX, d.viewportY, d.viewportWidth, d.viewportHeight);
		MOJOSHADER_glProgramReady();
		MOJOSHADER_glProgramViewportInfo(d.viewportWidth, d.viewportHeight, d.viewportWidth, d.viewportHeight, 0);
		if (glGetError() != GL_NO_ERROR) return Fail(Code::DriverFailure, "draw preparation failed");
#ifdef BACKEND_NEGATIVE_DRAW
		return Fail(Code::Unsupported, "intentional no-draw negative control");
#else
		glDrawElements(GL_TRIANGLES, GLsizei(indices.size()), GL_UNSIGNED_INT, nullptr);
		if (glGetError() != GL_NO_ERROR) return Fail(Code::DriverFailure, "indexed draw failed");
		++p.draws;
		return {};
#endif
	} catch (const std::bad_alloc&) { return Fail(Code::AllocationFailure, "index staging allocation failed"); }
}

Result ShaderBackend::Shutdown() {
	auto& p = *impl_;
	if (p.closed) return p.budget.live ? Fail(Code::DriverFailure, "upstream allocations remain after shutdown") : Result{};
	auto status = p.Check();
	if (!status) return status;
	GlState state;
	if (p.context) {
		MOJOSHADER_glMakeContextCurrent(p.context);
		glBindVertexArray(p.vao);
		MOJOSHADER_glBindProgram(nullptr);
		p.pair.Clear();
		MOJOSHADER_glDestroyContext(p.context);
		MOJOSHADER_glMakeContextCurrent(nullptr);
		p.context = nullptr;
	}
	if (p.vertexBuffer) glDeleteBuffers(1, &p.vertexBuffer);
	if (p.indexBuffer) glDeleteBuffers(1, &p.indexBuffer);
	if (p.vao) glDeleteVertexArrays(1, &p.vao);
	p.vertexBuffer = p.indexBuffer = p.vao = 0;
	p.ready = false; p.closed = true;
	p.archive.clear();
	if (p.registered) {
		Owners.erase(std::remove(Owners.begin(), Owners.end(), p.key), Owners.end());
		p.registered = false;
	}
	if (p.budget.live) return Fail(Code::DriverFailure, "upstream allocations remain after shutdown");
	return {};
}

Diagnostics ShaderBackend::GetDiagnostics() const {
	const auto& p = *impl_;
	return {p.budget.live, p.budget.peak, p.budget.calls, p.budget.refused, p.draws};
}
}
