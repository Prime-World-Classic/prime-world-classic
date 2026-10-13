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
constexpr unsigned RequiredOpacityPS[] = {6,20,21};
constexpr unsigned RequiredBakedVS[] = {0,1,2,8,9,10,11,234};
constexpr std::array<size_t,3> FamilyIndices{{0,12,24}};
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
	GLint vertexColorClamp = GL_FIXED_ONLY, fragmentColorClamp = GL_FIXED_ONLY;
	GLfloat alphaReference = 0;
	GLboolean depthMask = 0, colorMask[4]{}, blend = 0;
	GLdouble depthRange[2]{};
	static constexpr GLenum Flags[] = {GL_DEPTH_TEST, GL_CULL_FACE, GL_ALPHA_TEST,
		GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_DITHER, GL_FRAMEBUFFER_SRGB, GL_RASTERIZER_DISCARD,
		GL_PROGRAM_POINT_SIZE, GL_POLYGON_OFFSET_FILL, GL_PRIMITIVE_RESTART, GL_DEPTH_CLAMP, GL_COLOR_LOGIC_OP,
		GL_VERTEX_PROGRAM_TWO_SIDE};
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
		glGetIntegerv(GL_CLAMP_VERTEX_COLOR, &vertexColorClamp);
		glGetIntegerv(GL_CLAMP_FRAGMENT_COLOR, &fragmentColorClamp);
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
		glClampColor(GL_CLAMP_VERTEX_COLOR, GLenum(vertexColorClamp));
		glClampColor(GL_CLAMP_FRAGMENT_COLOR, GLenum(fragmentColorClamp));
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
using FamilySpans = std::array<std::array<Span, 2>, 3>;

/** Validate all original directory entries, while selecting only the fixed requested slots. */
template<size_t N>
Result CheckedDirectory(const Bytes& b, size_t expected, const std::array<size_t,N>& indices,
	std::array<std::array<Span,2>,N>& selected) {
	selected = {};
	if (b.size() < 4) return Fail(Code::InvalidInput, "short archive header");
	const size_t pairs = Read32(b, 0);
	if (pairs != expected || pairs > (b.size() - 4) / 8)
		return Fail(Code::Unsupported, "unexpected or truncated fixed shader directory");
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
		for (size_t slot = 0; slot < N; ++slot)
			if (i / 2 == indices[slot]) selected[slot][i % 2] = {o, length};
	}
	return {};
}

Result Directory(const Bytes& b, FamilySpans& selected) {
	return CheckedDirectory(b,96,FamilyIndices,selected);
}

Result BakedDirectory(const Bytes& b, std::array<Span,2>& selected) {
	std::array<std::array<Span,2>,1> spans{};
	const auto result = CheckedDirectory(b,36864,std::array<size_t,1>{{24}},spans);
	selected = result ? spans[0] : std::array<Span,2>{};
	return result;
}

/** Convert only referenced colours; checked source and destination bounds precede allocation. */
Result StageBakedColors(const Bgra8ColorStream& day, const Bgra8ColorStream& night,
	const std::vector<uint32_t>& indices, size_t limit, Bytes& output) {
	output.clear();
	if (indices.empty()) return Fail(Code::InvalidInput,"missing baked colour indices");
	const size_t maximum = *std::max_element(indices.begin(),indices.end());
	if (limit < 8 || maximum >= limit / 8)
		return Fail(Code::InvalidInput,"converted baked colours exceed geometry limit");
	for (const auto* stream : {&day,&night}) {
		if (!stream->bytes.data || stream->bytes.size > limit || stream->stride < 4 ||
			stream->offset > stream->bytes.size || stream->bytes.size - stream->offset < 4 ||
			maximum > (stream->bytes.size - stream->offset - 4) / stream->stride)
			return Fail(Code::InvalidInput,"baked colour stream/range out of bounds");
	}
	Bytes candidate((maximum + 1) * 8,0);
	for (uint32_t index : indices) for (size_t channel=0; channel<2; ++channel) {
		const auto& stream = channel ? night : day;
		const auto* bgra = static_cast<const unsigned char*>(stream.bytes.data) + stream.offset + size_t(index)*stream.stride;
		auto* rgba = candidate.data() + size_t(index)*8 + channel*4;
		rgba[0]=bgra[2]; rgba[1]=bgra[1]; rgba[2]=bgra[0]; rgba[3]=bgra[3];
	}
	output.swap(candidate);
	return {};
}

bool ExactReflection(const MOJOSHADER_parseData* p, bool vertex, bool opacity = false) {
	if (!p || p->error_count || p->major_ver != 3 || p->minor_ver || p->preshader ||
		p->shader_type != (vertex ? MOJOSHADER_TYPE_VERTEX : MOJOSHADER_TYPE_PIXEL)) return false;
	const unsigned* registers = vertex ? RequiredVS : opacity ? RequiredOpacityPS : RequiredPS;
	const size_t count = vertex ? std::size(RequiredVS) : opacity ? std::size(RequiredOpacityPS) : std::size(RequiredPS);
	if (p->uniform_count != int(count) || !p->uniforms) return false;
	for (size_t i = 0; i < count; ++i) {
		const auto& u = p->uniforms[i];
		if (u.type != MOJOSHADER_UNIFORM_FLOAT || u.array_count || u.constant || u.index != int(registers[i])) return false;
	}
	if (p->attribute_count != (vertex ? 2 : 1) || !p->attributes || p->sampler_count != (vertex ? 0 : 1)) return false;
	if (vertex) {
		return p->attributes[0].usage == MOJOSHADER_USAGE_POSITION && p->attributes[0].index == 0 &&
			p->attributes[1].usage == MOJOSHADER_USAGE_TEXCOORD && p->attributes[1].index == 0;
	}
	return p->samplers && p->attributes[0].usage == MOJOSHADER_USAGE_TEXCOORD && p->attributes[0].index == 0 &&
		p->samplers[0].index == 0 && p->samplers[0].type == MOJOSHADER_SAMPLER_2D && !p->samplers[0].texbem;
}

/** Family admission checks the complete selected interface, including shader-owned constants. */
bool ExactFamilyReflection(const MOJOSHADER_parseData* p, bool vertex, size_t slot) {
	if (slot >= std::size(FamilyIndices) || !ExactReflection(p, vertex, slot != 0) ||
		p->output_count != (vertex ? 2 : 1) || !p->outputs) return false;
	if (vertex) {
		if (p->constant_count != 1 || !p->constants) return false;
		const auto& c = p->constants[0];
		return p->outputs[0].usage == MOJOSHADER_USAGE_POSITION && p->outputs[0].index == 0 &&
			p->outputs[1].usage == MOJOSHADER_USAGE_TEXCOORD && p->outputs[1].index == 0 &&
			c.type == MOJOSHADER_UNIFORM_FLOAT && c.index == 4 &&
			c.value.f[0] == 1 && c.value.f[1] == 0 && c.value.f[2] == 0 && c.value.f[3] == 0;
	}
	return p->constant_count == 0 && p->outputs[0].usage == MOJOSHADER_USAGE_COLOR && p->outputs[0].index == 0;
}

/** Exact shipped BasicMaterial24 interface; no other baked/SH/lighting permutations. */
bool ExactBakedReflection(const MOJOSHADER_parseData* p, bool vertex) {
	if (!p || p->error_count || p->major_ver!=3 || p->minor_ver || p->preshader ||
		p->shader_type!=(vertex?MOJOSHADER_TYPE_VERTEX:MOJOSHADER_TYPE_PIXEL)) return false;
	const auto* required=vertex?RequiredBakedVS:RequiredPS;
	const size_t count=vertex?std::size(RequiredBakedVS):std::size(RequiredPS);
	if (p->uniform_count!=int(count) || !p->uniforms || !p->attributes || !p->outputs ||
		p->attribute_count!=(vertex?4:2) || p->output_count!=(vertex?4:1) ||
		p->sampler_count!=(vertex?0:1) || p->constant_count!=(vertex?1:0)) return false;
	for(size_t i=0;i<count;++i) {
		const auto& u=p->uniforms[i];
		if(u.type!=MOJOSHADER_UNIFORM_FLOAT || u.index!=int(required[i]) || u.array_count || u.constant) return false;
	}
	const auto attr=[](const MOJOSHADER_attribute& a, MOJOSHADER_usage usage, int index) {
		return a.usage==usage && a.index==index;
	};
	if(vertex) {
		if(!p->constants) return false;
		const auto& c=p->constants[0];
		return c.type==MOJOSHADER_UNIFORM_FLOAT && c.index==3 && c.value.f[0]==1 && c.value.f[1]==0 &&
			c.value.f[2]==255 && c.value.f[3]==0 &&
			attr(p->attributes[0],MOJOSHADER_USAGE_POSITION,0) && attr(p->attributes[1],MOJOSHADER_USAGE_TEXCOORD,0) &&
			attr(p->attributes[2],MOJOSHADER_USAGE_COLOR,0) && attr(p->attributes[3],MOJOSHADER_USAGE_COLOR,1) &&
			attr(p->outputs[0],MOJOSHADER_USAGE_TEXCOORD,0) && attr(p->outputs[1],MOJOSHADER_USAGE_TEXCOORD,2) &&
			attr(p->outputs[2],MOJOSHADER_USAGE_COLOR,0) && attr(p->outputs[3],MOJOSHADER_USAGE_POSITION,0);
	}
	return p->samplers && p->samplers[0].index==0 && p->samplers[0].type==MOJOSHADER_SAMPLER_2D && !p->samplers[0].texbem &&
		attr(p->attributes[0],MOJOSHADER_USAGE_TEXCOORD,0) && attr(p->attributes[1],MOJOSHADER_USAGE_COLOR,0) &&
		attr(p->outputs[0],MOJOSHADER_USAGE_COLOR,0);
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
	GLuint vao = 0, vertexBuffer = 0, indexBuffer = 0, colorBuffer = 0;
	bool registered = false, closed = false;
	unsigned activeSlots = 0;
	std::array<Pair, 4> pairs{};
	Bytes archive, bakedArchive;
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
		glGenBuffers(1, &p.colorBuffer);
		if (!p.vao || !p.vertexBuffer || !p.indexBuffer || !p.colorBuffer || glGetError() != GL_NO_ERROR) {
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
	std::array<ProgramRef, 4> programs;
	const auto status = LoadPrograms(source, {}, programs, 1);
	output = std::move(programs[0]);
	return status;
}

Result ShaderBackend::LoadStaticFamily(ByteView source, ByteView bakedSource, StaticPrograms& output) {
	output = {};
	std::array<ProgramRef,4> programs;
	const auto status = LoadPrograms(source,bakedSource,programs,4);
	output = {{std::move(programs[0]),std::move(programs[1]),std::move(programs[2])},std::move(programs[3])};
	return status;
}

Result ShaderBackend::LoadBasicFxOpacityFamily(ByteView source, BasicFxOpacityPrograms& output) {
	output = {};
	std::array<ProgramRef, 4> programs;
	const auto status = LoadPrograms(source, {}, programs, 3);
	output = {std::move(programs[0]), std::move(programs[1]), std::move(programs[2])};
	return status;
}

/** Stage every requested pair and ticket before committing the new load serial. */
Result ShaderBackend::LoadPrograms(ByteView source, ByteView bakedSource, std::array<ProgramRef,4>& output, size_t count) {
	output = {};
	auto& p = *impl_;
	auto status = p.Check();
	if (!status) return status;
	p.activeSlots = 0;
	p.InvalidateRegisters();
	const size_t cap = std::min(p.budget.limits.archiveBytes,size_t(4*1024*1024));
	if (!source.data || source.size < 4 || source.size > cap ||
		(count==4 && (!bakedSource.data || bakedSource.size<4 || bakedSource.size>cap-source.size)))
		return Fail(Code::InvalidInput, "archive view out of bounds");
	if (p.serial==std::numeric_limits<uint64_t>::max()) return Fail(Code::InvalidReference,"load serial exhausted");
	try {
		const auto* begin = static_cast<const unsigned char*>(source.data);
		Bytes archive(begin, begin + source.size);
		FamilySpans spans{};
		status = Directory(archive, spans);
		if (!status) return status;
		Bytes bakedArchive;
		std::array<Span,2> bakedSpans{};
		if(count==4) {
			const auto* first=static_cast<const unsigned char*>(bakedSource.data);
			bakedArchive.assign(first,first+bakedSource.size);
			status=BakedDirectory(bakedArchive,bakedSpans);
			if(!status) return status;
		}
		std::array<ProgramRef, 4> programs;
		for (size_t slot = 0; slot < count; ++slot)
			programs[slot].ticket_ = std::make_shared<ProgramRef::Ticket>(
				ProgramRef::Ticket{p.generation, p.serial + 1, static_cast<uint8_t>(slot)});
		GlState state;
		MojoScope scope(p.context, p.vao);
		std::array<Pair, 4> candidates{};
		struct Cleanup {
			std::array<Pair, 4>& pairs;
			~Cleanup() { for (auto& pair : pairs) pair.Clear(); }
		} cleanup{candidates};
		for (size_t slot = 0; slot < count; ++slot) {
			auto& candidate = candidates[slot];
			const auto& stage = slot==3 ? bakedSpans : spans[slot];
			const auto& bytes = slot==3 ? bakedArchive : archive;
			candidate.vs = MOJOSHADER_glCompileShader(bytes.data() + stage[0].offset, unsigned(stage[0].size), nullptr, 0, nullptr, 0);
			if (!candidate.vs) return Fail(Code::ShaderFailure, "vertex compilation failed");
			candidate.ps = MOJOSHADER_glCompileShader(bytes.data() + stage[1].offset, unsigned(stage[1].size), nullptr, 0, nullptr, 0);
			if (!candidate.ps) return Fail(Code::ShaderFailure, "pixel compilation failed");
			const auto* vs = MOJOSHADER_glGetShaderParseData(candidate.vs);
			const auto* ps = MOJOSHADER_glGetShaderParseData(candidate.ps);
			const bool exact = slot==3 ? ExactBakedReflection(vs,true) && ExactBakedReflection(ps,false) :
				count!=1 ? ExactFamilyReflection(vs,true,slot) && ExactFamilyReflection(ps,false,slot) :
				ExactReflection(vs,true) && ExactReflection(ps,false);
			if (!exact) return Fail(Code::Unsupported, "reflection differs from fixed selected program");
			candidate.linked = MOJOSHADER_glLinkProgram(candidate.vs, candidate.ps);
			if (!candidate.linked) return Fail(Code::ShaderFailure, "program link failed");
		}
		if (glGetError() != GL_NO_ERROR) return Fail(Code::DriverFailure, "shader creation GL error");
		for (auto& pair : p.pairs) pair.Clear();
		p.pairs = candidates;
		candidates = {};
		p.archive = std::move(archive);
		p.bakedArchive = std::move(bakedArchive);
		++p.serial;
		output = std::move(programs);
		p.activeSlots = (1u << count)-1;
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
	if (!reference.ticket_ || reference.ticket_->backend != p.generation || reference.ticket_->serial != p.serial ||
		reference.ticket_->slot >= p.pairs.size() || !(p.activeSlots & (1u << reference.ticket_->slot)))
		return Fail(Code::InvalidReference, "empty, stale, foreign or failed-load program");
	const size_t slot = reference.ticket_->slot;
	const bool baked = slot==3, opacity = slot==1 || slot==2;
	const unsigned* requiredVS = baked ? RequiredBakedVS : RequiredVS;
	const size_t vsCount = baked ? std::size(RequiredBakedVS) : std::size(RequiredVS);
	const unsigned* requiredPS = opacity ? RequiredOpacityPS : RequiredPS;
	const size_t psCount = opacity ? std::size(RequiredOpacityPS) : std::size(RequiredPS);
	for (size_t i=0;i<vsCount;++i) if (!p.vsInit[requiredVS[i]]) return Fail(Code::UninitializedRegisters, "required VS register missing");
	for (size_t i = 0; i < psCount; ++i) if (!p.psInit[requiredPS[i]]) return Fail(Code::UninitializedRegisters, "required PS register missing");
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
	if (baked && blended) return Fail(Code::Unsupported,"baked BasicMaterial24 supports only blend Off");
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
		Bytes colors;
		if (baked) {
			status=StageBakedColors(d.dayColors,d.nightColors,indices,p.budget.limits.geometryBytes,colors);
			if(!status) return status;
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
		MOJOSHADER_glBindProgram(p.pairs[slot].linked);
		for (size_t i=0;i<vsCount;++i) MOJOSHADER_glSetVertexShaderUniformF(requiredVS[i], p.vs[requiredVS[i]].data(), 1);
		for (size_t i = 0; i < psCount; ++i) MOJOSHADER_glSetPixelShaderUniformF(requiredPS[i], p.ps[requiredPS[i]].data(), 1);
		MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_POSITION, 0, 3, MOJOSHADER_ATTRIBUTE_FLOAT, 0,
			unsigned(d.stride), reinterpret_cast<const void*>(d.streamOffset + d.positionOffset));
		MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_TEXCOORD, 0, 2, MOJOSHADER_ATTRIBUTE_FLOAT, 0,
			unsigned(d.stride), reinterpret_cast<const void*>(d.streamOffset + d.uvOffset));
		if (baked) {
			glBindBuffer(GL_ARRAY_BUFFER,p.colorBuffer);
			glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(colors.size()),colors.data(),GL_STREAM_DRAW);
			for(int channel=0;channel<2;++channel)
				MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_COLOR,channel,4,MOJOSHADER_ATTRIBUTE_UBYTE,1,
					8,reinterpret_cast<const void*>(uintptr_t(channel*4)));
			// GLSL120 uses gl_FrontColor for HDR baked light; host clamps must not truncate it.
			glClampColor(GL_CLAMP_VERTEX_COLOR,GL_FALSE);
			glClampColor(GL_CLAMP_FRAGMENT_COLOR,GL_FALSE);
		}
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
		for (auto& pair : p.pairs) pair.Clear();
		MOJOSHADER_glDestroyContext(p.context);
		MOJOSHADER_glMakeContextCurrent(nullptr);
		p.context = nullptr;
	}
	if (p.vertexBuffer) glDeleteBuffers(1, &p.vertexBuffer);
	if (p.indexBuffer) glDeleteBuffers(1, &p.indexBuffer);
	if (p.colorBuffer) glDeleteBuffers(1, &p.colorBuffer);
	if (p.vao) glDeleteVertexArrays(1, &p.vao);
	p.vertexBuffer = p.indexBuffer = p.colorBuffer = p.vao = 0;
	p.activeSlots = 0; p.closed = true;
	p.archive.clear();
	p.bakedArchive.clear();
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
