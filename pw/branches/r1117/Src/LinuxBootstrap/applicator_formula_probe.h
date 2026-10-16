#pragma once

/** Exercise actual engine effects. Clock and summon suites run separately with a required dataRoot. */
bool RunPrimeWorldLinuxApplicatorFormulaProbe(const char* dataRoot = nullptr, bool mapClocksOnly = false, bool summonsOnly = false,
	bool targetedTalentsOnly = false);
