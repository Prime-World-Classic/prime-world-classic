#pragma once

/// Exercise AdventureFlashInterface through the native Flash host without assets or a display.
/// Link with the Flash/Tamarin runtime and TestFileSystem; run before starting a live UI session.
/// The mock SWF receiver checks the C++ bridge contract, not the authored Adventure SWF behavior.
bool RunPrimeWorldLinuxAdventureFlashProbe();
