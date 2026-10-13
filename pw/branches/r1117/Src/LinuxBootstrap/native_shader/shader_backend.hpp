#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace pwshader {

enum class Code {
	Ok, InvalidInput, Unsupported, WrongContext, WrongThread, Closed,
	InvalidReference, UninitializedRegisters, AllocationFailure, ShaderFailure, DriverFailure
};
struct Result {
	Code code = Code::Ok;
	std::string message;
	explicit operator bool() const { return code == Code::Ok; }
};
struct ByteView { const void* data = nullptr; size_t size = 0; };
using Float4 = std::array<float, 4>;
struct Float4View { const Float4* data = nullptr; size_t count = 0; };
enum class Stage { Vertex, Pixel };
enum class IndexType { UInt16, UInt32 };
enum class Winding { CounterClockwise, Clockwise };
enum class AlphaTest { Disabled, GreaterEqual };
enum class CullMode { Back, Disabled };
/** Original BasicFX factors; non-Off modes preserve destination alpha and depth. */
enum class BlendMode { Off, LerpByAlpha, PremultipliedLerp, AddColor, AddColorMulAlpha, MulColor, MulInvColor };

/** Host identity plus a host-assigned monotonic lifetime, not a recycled GLX address alone. */
struct ContextKey {
	uintptr_t identity = 0;
	uint64_t lifetime = 0;
	bool operator==(ContextKey b) const { return identity == b.identity && lifetime == b.lifetime; }
};
struct ContextBinding {
	void* user = nullptr;
	ContextKey (*current)(void*) noexcept = nullptr;
	void* (*lookup)(const char*, void*) = nullptr;
};

/** Caps cover archive, staging buffers and upstream allocation payloads independently. */
struct Limits {
	size_t archiveBytes = 4 * 1024 * 1024;
	size_t geometryBytes = 16 * 1024 * 1024;
	size_t indices = 1024 * 1024;
	size_t liveAllocationBytes = 8 * 1024 * 1024;
	size_t totalAllocationBytes = 64 * 1024 * 1024;
	size_t allocationCalls = 100000;
};
struct Diagnostics {
	size_t liveBytes = 0, peakBytes = 0, allocationCalls = 0, refusedAllocations = 0;
	uint64_t draws = 0;
};

/** Copyable CPU ticket only. Its last release never calls GL or touches a backend. */
class ProgramRef {
public:
	ProgramRef() = default;
	void Reset() noexcept { ticket_.reset(); }
	bool Empty() const noexcept { return !ticket_; }
private:
	struct Ticket { uint64_t backend; uint64_t serial; };
	std::shared_ptr<const Ticket> ticket_;
	friend class ShaderBackend;
};

/** Borrowed buffers remain valid through Draw. Offsets/stride are in bytes; indices in elements.
 * Texture is a live level-0 2D name in the owning context. Sampling is explicitly Repeat/Linear.
 * Draw targets the caller's framebuffer, without clearing it. All modified GL state is restored.
 * Alpha testing uses final shader alpha; discarded pixels write neither color nor depth.
 * All modes use LEQUAL depth testing. Off writes RGBA/depth without blending; other modes
 * blend RGB only and leave destination alpha/depth unchanged. Non-Off requires OpenGL 4.0
 * or ARB_draw_buffers_blend and its indexed entrypoints; otherwise Draw returns Unsupported.
 */
struct IndexedDraw {
	ByteView vertices, indexData;
	size_t streamOffset = 0, stride = 0, positionOffset = 0, uvOffset = 0;
	IndexType indexType = IndexType::UInt16;
	size_t firstIndex = 0, indexCount = 0;
	int32_t baseVertex = 0;
	uint32_t texture = 0;
	int viewportX = 0, viewportY = 0, viewportWidth = 0, viewportHeight = 0;
	Winding frontFace = Winding::CounterClockwise;
	AlphaTest alphaTest = AlphaTest::Disabled;
	CullMode cullMode = CullMode::Back;
	/** Original D3D byte reference, validated even when alpha testing is disabled. */
	int alphaReference = 127;
	BlendMode blendMode = BlendMode::Off;
};

/** Render-thread-only, noncopyable owner for one selected BasicFX pair.
 * Requires OpenGL 3.3 compatibility, and exclusive MojoShader use on its host context.
 * Call Shutdown successfully before the host destroys its context. The destructor attempts
 * Shutdown, then terminates on a lifetime-contract violation rather than pretending cleanup.
 */
class ShaderBackend final {
public:
	static std::unique_ptr<ShaderBackend> Create(ContextBinding owner, Limits limits, Result& status);
	~ShaderBackend();
	ShaderBackend(const ShaderBackend&) = delete;
	ShaderBackend& operator=(const ShaderBackend&) = delete;

	/** Clears output even on failure; failed reload prevents old tickets from drawing. */
	Result LoadBasicFxPair0(ByteView archive, ProgramRef& output);
	/** Rejected write invalidates all required initialization bits; reupload the full required set. */
	Result SetFloat4(Stage stage, uint32_t firstRegister, Float4View values);
	Result Draw(const ProgramRef& program, const IndexedDraw& draw);
	/** Wrong/no context or wrong thread returns failure without releasing resources. */
	Result Shutdown();
	Diagnostics GetDiagnostics() const;
private:
	struct Impl;
	explicit ShaderBackend(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};
}
