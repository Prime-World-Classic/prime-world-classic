#pragma once

/**
 * @brief Exercise production Linux FogOfWar using rectangular mock grids/heights.
 * @return True when wide/tall visibility, movement, obstacles, masks and heights pass.
 *
 * Call only from a PW_LINUX_NULL_RENDER bootstrap executable. No assets, live
 * world, rendering or gameplay commands are needed. The first wide-grid query
 * reproduces the legacy [x][y] out-of-bounds access at tile (95,40) on a 120x80
 * grid, so this probe can also be linked against the pre-fix WarFog object.
 *
 * Linux fog snapshots now use canonical [y][x] cell bytes, including object-local
 * caches. Legacy Linux/Windows fog snapshots require migration before Linux use;
 * Windows indexing and snapshot layout remain unchanged. This probe does not
 * claim cross-layout serialization compatibility or exercise GPU orientation.
 */
bool RunPrimeWorldLinuxFogGridProbe();
