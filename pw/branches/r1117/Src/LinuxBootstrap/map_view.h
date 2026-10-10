#pragma once

#include <algorithm>

namespace LinuxBootstrap
{
/** Orthographic half-extents shared by drawing and picking; preserve the short axis.
 * Landscape framing is unchanged. Portrait windows expand vertical coverage rather
 * than cropping the map horizontally. Callers must supply positive dimensions.
 */
struct MapView
{
	float halfWidth, halfHeight;
	MapView(float extent, int width, int height)
	{
		const float aspect = static_cast<float>(width) / height;
		halfWidth = extent * std::max(1.f, aspect);
		halfHeight = extent * std::max(1.f, 1.f / aspect);
	}
};
}
