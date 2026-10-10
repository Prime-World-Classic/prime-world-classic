#pragma once

#include <memory>
#include <cstddef>

namespace UI { class User; }

namespace LinuxBootstrap
{
/// Linux host for the authored Combat layout. Does not replace the Windows AdventureScreen.
class AdventurePresentation
{
public:
	AdventurePresentation();
	~AdventurePresentation();
	bool Initialize(UI::User* user);
	bool IsReady() const;
	void Step(float deltaSeconds);
	void Render();
	/// Release Flash GC roots and UI windows before the renderer and UI subsystem shut down.
	void Reset();
	bool WasAttempted() const { return attempted; }
	std::size_t GetFrames() const { return frames; }
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
	bool attempted = false;
	std::size_t frames = 0;
};
}
