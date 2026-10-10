#pragma once

namespace LinuxBootstrap
{
/// Scripted input follows the active session after the lobby is hidden; normal handlers still gate actions.
inline bool CanDispatchBootstrapInput(bool lobbyReady, bool sessionReady)
{
	return lobbyReady || sessionReady;
}

enum class SessionPresentation
{
	Loading,
	World,
	LegacyOverlay
};

/// Choose presentation independently of the retained loading/session objects.
/// The inspection flag pins loading even after readiness; failed loads never expose a world.
inline SessionPresentation ResolveSessionPresentation(bool inspectLoading, bool legacyOverlay,
	bool mapLoaded, bool worldAttached, bool renderReady)
{
	if (inspectLoading)
		return SessionPresentation::Loading;
	if (legacyOverlay)
		return SessionPresentation::LegacyOverlay;
	return mapLoaded && worldAttached && renderReady ?
		SessionPresentation::World : SessionPresentation::Loading;
}
}
