// Standalone native regression; link with --wrap=_ZdaPv and --wrap=_ZdaPvm.
#include "../Render/stdafx.h"
#include "../Render/MeshResource.h"
#include "../Render/renderresourcemanager.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

// The standalone executable supplies the same native render signal as the client runtime.
IMPLEMENT_SIMPLE_SIGNAL_ST(NullRenderSignal)

/** Valid shipped data must not hit engine diagnostics; no client logger is needed by this executable. */
void TraceMsg(const char* message)
{
	std::fprintf(stderr, "Unexpected engine diagnostic: %s\n", message ? message : "<null>");
	std::abort();
}
NLogg::CChannelLogger& GetSystemLog()
{
	TraceMsg("mesh loader requested the engine log");
	std::abort();
}
void NLogg::StreamBuffer::WriteHeader(unsigned)
{
	TraceMsg("mesh loader attempted to format an engine diagnostic");
}

namespace
{
/** Watch only the exact two allocations returned by the real loader, without an allocation registry. */
struct ReindexDeletes
{
	static ReindexDeletes* active;
	const void* first;
	const void* second;
	int firstDeletes = 0, secondDeletes = 0;
	ReindexDeletes(const void* first, const void* second = nullptr) : first(first), second(second) { active = this; }
	~ReindexDeletes() { active = nullptr; }
	static void Observe(const void* memory)
	{
		if (!active || !memory) return;
		if (memory == active->first) ++active->firstDeletes;
		if (memory == active->second) ++active->secondDeletes;
	}
};
ReindexDeletes* ReindexDeletes::active = nullptr;
int checks = 0, failures = 0;

/** Keep running on the old retaining implementation so its real deferred cleanup still executes. */
void Check(bool condition, const char* description)
{
	++checks;
	if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", description); }
}
}

// GNU linker wrappers preserve the actual allocator and observe destruction before forwarding it.
extern "C" void __real__ZdaPv(void*) noexcept;
extern "C" void __real__ZdaPvm(void*, std::size_t) noexcept;
extern "C" void __wrap__ZdaPv(void* memory) noexcept
{
	ReindexDeletes::Observe(memory);
	__real__ZdaPv(memory);
}
extern "C" void __wrap__ZdaPvm(void* memory, std::size_t size) noexcept
{
	ReindexDeletes::Observe(memory);
	__real__ZdaPvm(memory, size);
}

/** Repeatedly load the shipped skeletal mesh through the original in-memory renderer loader. */
int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::fprintf(stderr, "Usage: %s <Data/Summons/Demon2/_.skin>\n", argv[0]);
		return 2;
	}
	std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
	if (!input || input.tellg() < static_cast<std::streamoff>(sizeof(H6GeometryFileHeader)))
	{
		std::fprintf(stderr, "Cannot read shipped mesh: %s\n", argv[1]);
		return 2;
	}
	std::vector<char> payload(static_cast<std::size_t>(input.tellg()));
	input.seekg(0);
	if (!input.read(payload.data(), static_cast<std::streamsize>(payload.size()))) return 2;
	const auto* header = reinterpret_cast<const H6GeometryFileHeader*>(payload.data());
	Check(header->jointsCount > 0 && header->reindexOffset > header->ibOffset
		&& header->reindexOffset < payload.size(), "shipped fixture has a skeletal reindex payload");
	if (failures) return 1;
	Render::RenderResourceManager::Init();
	Render::SmartRenderer::Init();
	{
		Render::MeshGeometry empty;
		Check(empty.pReindex == nullptr, "default geometry has no reindex ownership");
		empty.Swap(&empty);
		Check(empty.pReindex == nullptr, "null self-swap leaves ownership empty");
	}
	for (int reload = 0; reload != 32; ++reload)
	{
		std::unique_ptr<Render::MeshGeometry> first(Render::RenderResourceManager::LoadMeshGeometry(payload.data(), true, false));
		std::unique_ptr<Render::MeshGeometry> second(Render::RenderResourceManager::LoadMeshGeometry(payload.data(), true, false));
		Check(first && second && first->pReindex && second->pReindex, "repeated original loads produce owned skeletal reindices");
		if (!first || !second || !first->pReindex || !second->pReindex) break;
		const auto* a = first->pReindex;
		const auto* b = second->pReindex;
		Check(a != b && reinterpret_cast<const char*>(a) != payload.data() + header->reindexOffset
			&& reinterpret_cast<const char*>(b) != payload.data() + header->reindexOffset,
			"independent loads do not alias one another or borrow the source payload");
		ReindexDeletes observed(a, b);
		first->Swap(second.get());
		Check(first->pReindex == b && second->pReindex == a, "swap transfers both actual loader allocations");
		first.reset();
		Check(observed.firstDeletes == 0 && observed.secondDeletes == 1,
			"destroying the first swapped owner releases only its current reindex immediately");
		second->Swap(second.get());
		Check(second->pReindex == a && observed.firstDeletes == 0, "self-swap preserves the surviving allocation");
		second.reset();
		Check(observed.firstDeletes == 1 && observed.secondDeletes == 1,
			"both reindex allocations are released exactly once without renderer shutdown");
	}
	{
		std::unique_ptr<Render::MeshGeometry> loaded(Render::RenderResourceManager::LoadMeshGeometry(payload.data(), true, false));
		Check(loaded && loaded->pReindex, "load for transfer into an empty owner succeeds");
		if (loaded && loaded->pReindex)
		{
			const auto* allocation = loaded->pReindex;
			ReindexDeletes observed(allocation);
			{
				Render::MeshGeometry empty;
				loaded->Swap(&empty);
				Check(!loaded->pReindex && empty.pReindex == allocation, "swap into a null owner transfers sole ownership");
				loaded.reset();
				Check(observed.firstDeletes == 0, "destroying the moved-from null owner does not release the live allocation");
			}
			Check(observed.firstDeletes == 1, "new owner releases the transferred allocation exactly once");
		}
	}
	// This also exposes a stale global reindex entry as a double free after the ownership fix.
	Render::RenderResourceManager::Release();
	Render::RenderResourceManager::Release();
	Render::SmartRenderer::Release();
	std::printf("Mesh reindex ownership: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
