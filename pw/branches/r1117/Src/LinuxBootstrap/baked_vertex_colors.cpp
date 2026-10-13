#include "baked_vertex_colors.h"

#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>
#include <utility>

namespace LinuxBootstrap
{
namespace
{
using Bytes = std::vector<std::uint8_t>;
using Code = BakedVertexColorError::Code;

struct ObjectIndex { std::uint32_t firstScene, scenes; };
struct SceneIndex { std::uint32_t hash, firstComponent, components; };
struct ComponentIndex { std::uint32_t crc, day, night, dayCount, nightCount; };

struct Counts
{
	std::size_t objects = 0, scenes = 0, components = 0, colors = 0;
	std::size_t parsed = 0, metadata = 0;
};

/** Read words without unaligned loads, host-endian assumptions, or unchecked sums. */
class Cursor
{
public:
	Cursor(const Bytes& bytes, BakedVertexColorError& error) : bytes_(bytes), error_(error) {}
	std::size_t Position() const noexcept { return at_; }
	std::size_t Remaining() const noexcept { return bytes_.size() - at_; }
	bool Fail(Code code, std::size_t offset) noexcept { error_ = {code, offset}; return false; }
	bool Word(std::uint32_t& value) noexcept
	{
		if (Remaining() < 4) return Fail(Code::Truncated, at_);
		value = static_cast<std::uint32_t>(bytes_[at_]) |
			(static_cast<std::uint32_t>(bytes_[at_ + 1]) << 8) |
			(static_cast<std::uint32_t>(bytes_[at_ + 2]) << 16) |
			(static_cast<std::uint32_t>(bytes_[at_ + 3]) << 24);
		at_ += 4;
		return true;
	}
	bool Count(std::uint32_t& count, std::size_t cap, std::size_t minimumBytes) noexcept
	{
		const auto offset = at_;
		if (!Word(count)) return false;
		if (count > 0x7fffffffu) return Fail(Code::NegativeCount, offset);
		if (count > cap) return Fail(Code::CountLimit, offset);
		if (minimumBytes && count > Remaining() / minimumBytes) return Fail(Code::Truncated, offset);
		return true;
	}
	bool Colors(std::uint32_t& offset, std::uint32_t& count, Counts& counts,
		const BakedVertexColorLimits& limits) noexcept
	{
		const auto countOffset = at_;
		if (!Count(count, limits.maxColorsPerStream, 4)) return false;
		if (count > limits.maxColors - counts.colors) return Fail(Code::CountLimit, countOffset);
		counts.colors += count;
		offset = static_cast<std::uint32_t>(at_);
		at_ += static_cast<std::size_t>(count) * 4;
		return true;
	}
	bool Padding() noexcept
	{
		while (at_ < bytes_.size())
		{
			if (bytes_[at_] != 0) return Fail(Code::NonzeroPadding, at_);
			++at_;
		}
		return true;
	}
private:
	const Bytes& bytes_;
	BakedVertexColorError& error_;
	std::size_t at_ = 0;
};

/** Independent count/storage budgets checked before increment or allocation. */
bool Account(Cursor& cursor, std::size_t count, std::size_t cap, std::size_t& total,
	std::size_t elementBytes, Counts& counts, const BakedVertexColorLimits& limits,
	std::size_t offset) noexcept
{
	if (count > cap - total) return cursor.Fail(Code::CountLimit, offset);
	if (count > (limits.maxMetadataBytes - counts.metadata) / elementBytes)
		return cursor.Fail(Code::MetadataLimit, offset);
	total += count;
	counts.metadata += count * elementBytes;
	return true;
}

BakedVertexColorLimits Clamp(const BakedVertexColorLimits& requested) noexcept
{
	BakedVertexColorLimits limits;
	limits.maxSourceBytes = std::min(limits.maxSourceBytes, requested.maxSourceBytes);
	limits.maxMetadataBytes = std::min(limits.maxMetadataBytes, requested.maxMetadataBytes);
	limits.maxObjects = std::min(limits.maxObjects, requested.maxObjects);
	limits.maxScenesPerObject = std::min(limits.maxScenesPerObject, requested.maxScenesPerObject);
	limits.maxScenes = std::min(limits.maxScenes, requested.maxScenes);
	limits.maxComponentsPerScene = std::min(limits.maxComponentsPerScene, requested.maxComponentsPerScene);
	limits.maxComponents = std::min(limits.maxComponents, requested.maxComponents);
	limits.maxColorsPerStream = std::min(limits.maxColorsPerStream, requested.maxColorsPerStream);
	limits.maxColors = std::min(limits.maxColors, requested.maxColors);
	limits.maxHashBuckets = std::min(limits.maxHashBuckets, requested.maxHashBuckets);
	return limits;
}
}

struct BakedVertexColors::Data
{
	std::shared_ptr<const Bytes> bytes;
	std::vector<ObjectIndex> objects;
	std::vector<SceneIndex> scenes;
	std::vector<ComponentIndex> components;
	Counts counts;

	/** Same non-recursive schema for preflight and index fill. Only the first pass
	 * scans padding. Keys precede all scene values in BinSaver::DoHashMap order;
	 * sort the completed scene index, not the serialized traversal order.
	 */
	static bool Scan(const Bytes& source, const BakedVertexColorLimits& limits,
		BakedVertexColorError& error, Counts& counts, Data* output)
	{
		Cursor cursor(source, error);
		std::uint32_t objectCount = 0;
		if (!cursor.Count(objectCount, limits.maxObjects, 8) ||
			!Account(cursor, objectCount, limits.maxObjects, counts.objects, sizeof(ObjectIndex), counts, limits, 0))
			return false;
		for (std::uint32_t object = 0; object < objectCount; ++object)
		{
			std::uint32_t sceneCount = 0, buckets = 0;
			const auto sceneOffset = cursor.Position(), firstScene = counts.scenes;
			if (!cursor.Count(sceneCount, limits.maxScenesPerObject, 8) ||
				!Account(cursor, sceneCount, limits.maxScenes, counts.scenes, sizeof(SceneIndex), counts, limits, sceneOffset) ||
				!cursor.Count(buckets, limits.maxHashBuckets, 0)) return false;
			// Fixed upper bound also bounds duplicate-check scratch and work.
			std::array<std::uint32_t, 256> keys{};
			const auto keyOffset = cursor.Position();
			for (std::uint32_t scene = 0; scene < sceneCount; ++scene)
				if (!cursor.Word(keys[scene])) return false;
			auto sortedKeys = keys;
			std::sort(sortedKeys.begin(), sortedKeys.begin() + sceneCount);
			if (std::adjacent_find(sortedKeys.begin(), sortedKeys.begin() + sceneCount) != sortedKeys.begin() + sceneCount)
				return cursor.Fail(Code::DuplicateScene, keyOffset);
			if (output) output->objects[object] = {static_cast<std::uint32_t>(firstScene), sceneCount};
			for (std::uint32_t scene = 0; scene < sceneCount; ++scene)
			{
				std::uint32_t componentCount = 0;
				const auto componentOffset = cursor.Position(), firstComponent = counts.components;
				if (!cursor.Count(componentCount, limits.maxComponentsPerScene, 12) ||
					!Account(cursor, componentCount, limits.maxComponents, counts.components, sizeof(ComponentIndex), counts, limits, componentOffset))
					return false;
				if (output) output->scenes[firstScene + scene] = {keys[scene], static_cast<std::uint32_t>(firstComponent), componentCount};
				for (std::uint32_t component = 0; component < componentCount; ++component)
				{
					ComponentIndex record{};
					if (!cursor.Word(record.crc) || !cursor.Colors(record.day, record.dayCount, counts, limits) ||
						!cursor.Colors(record.night, record.nightCount, counts, limits)) return false;
					if (output) output->components[firstComponent + component] = record;
				}
			}
			if (output)
			{
				auto first = output->scenes.begin() + static_cast<std::ptrdiff_t>(firstScene);
				std::sort(first, first + sceneCount, [](const SceneIndex& a, const SceneIndex& b) { return a.hash < b.hash; });
			}
		}
		counts.parsed = cursor.Position();
		return output || cursor.Padding();
	}
};

BakedVertexColors::BakedVertexColors(std::shared_ptr<const Data> data) noexcept : data_(std::move(data)) {}

const std::uint8_t* BakedVertexColorView::DayData() const noexcept
{
	return Valid() ? bytes_->data() + day_ : nullptr;
}

const std::uint8_t* BakedVertexColorView::NightData() const noexcept
{
	return Valid() ? bytes_->data() + night_ : nullptr;
}

bool BakedVertexColors::Decode(Bytes&& bytes, Ptr& output, BakedVertexColorError& error,
	const BakedVertexColorLimits& requested) noexcept
{
	output.reset();
	error = {};
	const auto limits = Clamp(requested);
	if (bytes.size() > limits.maxSourceBytes || bytes.capacity() > limits.maxSourceBytes)
	{
		error = {Code::SourceLimit, 0};
		return false;
	}
	try
	{
		Counts counts;
		if (!Data::Scan(bytes, limits, error, counts, nullptr)) return false;
		auto data = std::make_shared<Data>();
		data->objects.resize(counts.objects);
		data->scenes.resize(counts.scenes);
		data->components.resize(counts.components);
		if (!Data::Scan(bytes, limits, error, data->counts, data.get())) return false;
		data->bytes = std::make_shared<const Bytes>(std::move(bytes));
		output = Ptr(new BakedVertexColors(std::move(data)));
		return true;
	}
	catch (const std::bad_alloc&)
	{
		error = {Code::AllocationFailure, 0};
		return false;
	}
	catch (const std::length_error&)
	{
		error = {Code::AllocationFailure, 0};
		return false;
	}
}

bool BakedVertexColors::Lookup(std::size_t objectIndex, std::uint32_t sceneHash,
	std::size_t componentIndex, std::uint32_t geometryCRC, std::size_t vertexCount,
	BakedVertexColorView& output, BakedVertexColorError& error) const noexcept
{
	output = {};
	error = {};
	const auto fail = [&error](Code code) { error = {code, 0}; return false; };
	if (objectIndex >= data_->objects.size()) return fail(Code::MissingObject);
	const auto& object = data_->objects[objectIndex];
	const auto first = data_->scenes.begin() + object.firstScene;
	const auto last = first + object.scenes;
	const auto scene = std::lower_bound(first, last, sceneHash,
		[](const SceneIndex& record, std::uint32_t hash) { return record.hash < hash; });
	if (scene == last || scene->hash != sceneHash) return fail(Code::MissingScene);
	if (componentIndex >= scene->components) return fail(Code::MissingComponent);
	const auto& component = data_->components[scene->firstComponent + componentIndex];
	if (!component.dayCount || !component.nightCount) return fail(Code::EmptyColors);
	if (component.crc != geometryCRC) return fail(Code::GeometryMismatch);
	if (component.dayCount != vertexCount || component.nightCount != vertexCount) return fail(Code::VertexCountMismatch);
	output.bytes_ = data_->bytes;
	output.day_ = component.day;
	output.night_ = component.night;
	output.count_ = vertexCount;
	output.crc_ = geometryCRC;
	return true;
}

std::size_t BakedVertexColors::ObjectCount() const noexcept { return data_->counts.objects; }
std::size_t BakedVertexColors::SceneCount() const noexcept { return data_->counts.scenes; }
std::size_t BakedVertexColors::ComponentCount() const noexcept { return data_->counts.components; }
std::size_t BakedVertexColors::ColorCount() const noexcept { return data_->counts.colors; }
std::size_t BakedVertexColors::SourceBytes() const noexcept { return data_->bytes->size(); }
std::size_t BakedVertexColors::ParsedBytes() const noexcept { return data_->counts.parsed; }
std::size_t BakedVertexColors::MetadataBytes() const noexcept { return data_->counts.metadata; }
}
