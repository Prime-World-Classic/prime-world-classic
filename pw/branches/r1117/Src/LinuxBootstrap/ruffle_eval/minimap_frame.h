#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

/**
 * @brief Borrowed complete native minimap artwork, top-down straight-alpha RGBA8.
 *
 * Pass the actual decoded game asset (e.g. LinuxLoadingArtwork::rgba); this helper
 * neither loads assets nor invents a fallback. Pixels remain owned by the caller
 * and must stay valid/unchanged for the call. byteCount is accessible buffer size,
 * strideBytes is the byte distance between rows (>= width*4). Row padding is
 * allowed; the final row need only contain width*4 bytes. No source-sized copy.
 */
struct PwRuffleMinimapBackgroundView
{
	static constexpr std::uint32_t MaxDimension = 4096;
	static constexpr std::size_t MaxBytes = 64 * 1024 * 1024;

	const std::uint8_t* pixels = nullptr;
	std::size_t byteCount = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::size_t strideBytes = 0;
};

/**
 * @brief Axis-aligned bounds matching the supplied background; world +Y points up.
 *
 * (minX,maxY) maps to the top-left output pixel, (maxX,minY) to bottom-right.
 * Extents must be positive and finite. Caller resolves authored map offsets,
 * orientation and rotation; this helper does not reproduce native map transforms.
 */
struct PwRuffleMinimapWorldBounds
{
	double minX = std::numeric_limits<double>::quiet_NaN();
	double minY = std::numeric_limits<double>::quiet_NaN();
	double maxX = std::numeric_limits<double>::quiet_NaN();
	double maxY = std::numeric_limits<double>::quiet_NaN();
};

/** Plain marker extracted from the actual world, not a guessed or simulated unit. */
struct PwRuffleMinimapMarker
{
	static constexpr std::size_t MaxCount = 640;

	/** Relative to the local player, NOT raw native EFaction numbers. */
	enum class Team : std::int32_t { Neutral = 0, Ally = 1, Enemy = 2 };
	/** Map common/neutral creeps to Creep and towers/main buildings to Objective. */
	enum class Kind : std::int32_t { Unsupported = 0, Hero = 1, Creep = 2, Objective = 3 };

	double worldX = std::numeric_limits<double>::quiet_NaN();
	double worldY = std::numeric_limits<double>::quiet_NaN();
	Team team = Team::Neutral;
	Kind kind = Kind::Unsupported;
	bool self = false; ///< Only a Hero may be self; drawn last in white.
	bool dead = false;
	bool visible = false; ///< Must be supplied from actual local-player visibility.
};

/** Owned, tightly packed, top-down straight-alpha RGBA8; no GPU/VM ownership. */
struct PwRuffleMinimapFrame
{
	static constexpr std::uint32_t Side = 270;
	static constexpr std::size_t PixelBytes = Side * Side * 4;

	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t strideBytes = 0;
	std::vector<std::uint8_t> rgba;
};

/**
 * @brief Build a 270x270 bitmap from native artwork and bounded gameplay markers.
 * @throws std::invalid_argument For invalid dimensions/buffers/strides/budgets,
 * non-finite coordinates/bounds/extents, invalid enums, or >640 input markers.
 * All marker coordinates are validated, even for hidden/dead/unsupported entries.
 * No partial frame or invented background is returned on invalid input.
 *
 * Background sampling is nearest-neighbor at destination pixel centers; all four
 * RGBA channels are copied unchanged. Source dimensions are <=4096 per axis and
 * its view/strided footprint <=64 MiB. The only pixel allocation is the 291600-byte
 * output, independent of source size. No engine, JSON, renderer, or GL dependency.
 *
 * Only visible, living, supported markers whose centers lie inside the closed
 * world bounds are drawn; outside centers are omitted, NOT clamped to false edge
 * positions. Primitive edges are clipped to the output. Centers use nearest-pixel
 * mapping across 0..269. Creeps are 3x3 squares, objectives 7x7 squares, heroes
 * radius-3 circles, self radius-4 circles. Colors: ally (86,220,92), enemy
 * (232,74,66), neutral (232,192,72), self (255,255,255), all opaque. These are
 * simple gameplay indicators, not authored icon/color parity. Drawing order is
 * creeps, objectives, heroes, self; ties retain input order (last paint wins).
 *
 * Visibility is caller-supplied, NOT fog-of-war computation or parity. No explored
 * mask, last-seen units, ghost markers, camera footprint, rotation, or interaction.
 * Caller must supply artwork and world bounds in the same orientation.
 * The result is rectangular; call PwRuffleClipMinimapFrame explicitly before
 * uploading into a shipped SWF whose authored minimap mask is absent.
 */
PwRuffleMinimapFrame PwRuffleBuildMinimapFrame(const PwRuffleMinimapBackgroundView& background,
	const PwRuffleMinimapWorldBounds& bounds, const std::vector<PwRuffleMinimapMarker>& markers);

/**
 * @brief Clip an owned frame in place to the authored minimap circle, without allocation.
 * @throws std::invalid_argument Unless width/height are exactly 270, strideBytes
 * is exactly 1080, and rgba contains exactly 291600 bytes. Failure changes nothing.
 *
 * Session UI/classes/MainScreen/Minimap.as draws its mask at (135,135), radius 132.
 * Integer pixel coordinates satisfying (x-135)^2 + (y-135)^2 <= 132^2 retain all
 * four original RGBA bytes, including existing alpha. All other pixels become
 * transparent black (RGBA zero), covering background and markers alike.
 * This is an inclusive, hard-edged raster mask, not authored antialiasing parity.
 * The operation is idempotent, bounded to 72900 pixels, and does not change frame
 * metadata/storage. Build stays unclipped by default; the caller opts in once the
 * bitmap is complete and before uploading it into the circular HUD frame.
 */
void PwRuffleClipMinimapFrame(PwRuffleMinimapFrame& frame);
