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
	struct Ticket { uint64_t backend; uint64_t serial; uint8_t slot; };
	std::shared_ptr<const Ticket> ticket_;
	friend class ShaderBackend;
};

/** CPU-only tickets for the three supported constant BasicFX opacity programs. */
struct BasicFxOpacityPrograms {
	ProgramRef simple;
	ProgramRef additiveOpacity;
	ProgramRef blendOpacity;
};

/** Fixed CPU tickets for original BasicFX 0/12/24 and baked BasicMaterial 24. */
struct StaticPrograms {
	BasicFxOpacityPrograms basicFx;
	ProgramRef bakedBasicMaterial;
};

/** Original static family plus eight ordinary TerrainElementMaterial variants.
 * Terrain slot = 2 * (layerCount - 1) + localUV, selecting archive pairs 0,30,...,210.
 * Cached, shadow and depth-output terrain permutations are intentionally excluded.
 */
struct ScenePrograms {
	StaticPrograms statics;
	std::array<ProgramRef, 8> terrain;
};

/** Borrowed packed BGRA8 colours, normalized by the backend, not predecoded lighting.
 * A colour starts at offset + (index + baseVertex) * stride, independently of stream0.
 */
struct Bgra8ColorStream {
	ByteView bytes;
	size_t offset = 0;
	size_t stride = 4;
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
	/** Required by baked BasicMaterial and terrain; BasicFX ignores these two views. */
	Bgra8ColorStream dayColors, nightColors;
	/** Terrain-only COLOR2 weights and samplers 0..3/4. Unused layers are ignored.
	 * All samplers use repeat/linear level0 sampling; names may alias. Diffuse
	 * mipmapping/anisotropic filtering from the Windows terrain pass is not yet ported.
	 * Day/night colors are also required. Terrain ignores the single texture field.
	 */
	Bgra8ColorStream terrainWeights;
	std::array<uint32_t, 4> terrainTextures{};
	uint32_t natureTexture = 0;
};

/** Render-thread-only owner for pair zero, BasicFX, static, or static-plus-terrain families.
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

	/** Clears output even on failure; owning-context reload invalidates all prior tickets.
	 * Retains pair-zero-only compilation, reflection and register requirements.
	 */
	Result LoadBasicFxPair0(ByteView archive, ProgramRef& output);
	/** Clears output on every call; publishes all three tickets only on success.
	 * An owning-context attempt invalidates the previous set even on failure.
	 * Wrong-context/thread calls leave backend state unchanged.
	 */
	Result LoadBasicFxOpacityFamily(ByteView archive, BasicFxOpacityPrograms& output);
	/** Atomically publishes the fixed four-program set; archive limits apply to the sum.
	 * Owning-context attempts invalidate all old tickets even on failure, just like BasicFX.
	 * Baked draws require BlendMode::Off, both colour streams, VS c0..2/c8..11/c234,
	 * and PS c20/c21. Shader-owned c3 and HDR/day-night decoding are not CPU lighting.
	 */
	Result LoadStaticFamily(ByteView basicFxArchive, ByteView basicMaterialArchive, StaticPrograms& output);
	/** Atomic twelve-program set in the same context owner, with summed archive limits.
	 * Terrain requires VS c8..11/c20/c21/c234 and PS c20/c21. Positions are world-space;
	 * c0 is shader-owned, not WORLD. Only Off (first nature pass) and AddColor (later
	 * passes preserving destination alpha/depth) are admitted. All three BGRA streams
	 * and exactly layerCount diffuse samplers plus nature sampler4 must be valid.
	 */
	Result LoadSceneFamily(ByteView basicFxArchive, ByteView basicMaterialArchive,
		ByteView terrainArchive, ScenePrograms& output);
	/** Rejected write invalidates all required initialization bits; reupload the full required set. */
	Result SetFloat4(Stage stage, uint32_t firstRegister, Float4View values);
	Result Draw(const ProgramRef& program, const IndexedDraw& draw);
	/** Wrong/no context or wrong thread returns failure without releasing resources. */
	Result Shutdown();
	Diagnostics GetDiagnostics() const;
private:
	struct Impl;
	Result LoadPrograms(ByteView basicFx, ByteView basicMaterial, ByteView terrain,
		std::array<ProgramRef, 12>& output, size_t count);
	explicit ShaderBackend(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};
}
