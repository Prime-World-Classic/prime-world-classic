#pragma once

#include "map_view.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace LinuxBootstrap
{
/** One orthographic snapshot for the native interim map view and its inverse.
 * World coordinates are XY ground/Z height; the existing renderer uses XZ ground.
 * This preserves its rotation order, without iterative/clamped screen projection.
 * It is not the Windows perspective AdventureCameraController.
 */
struct MapCameraFrame
{
	struct Point { double x = 0, y = 0, z = 0; };
	bool valid = false;
	int width = 0, height = 0;
	double centerX = 0, centerY = 0, scale = 1;
	double halfWidth = 1, halfHeight = 1, verticalShift = 0, focusHeight = 0;
	double panX = 0, panZ = 0, pitch = 0, yaw = 0;
	double cp = 1, sp = 0, cy = 1, sy = 0;

	MapCameraFrame() = default;
	MapCameraFrame(double minX, double minY, double maxX, double maxY,
		int w, int h, double zoom, double pitchDegrees, double yawDegrees,
		double panHorizontal, double panVertical, bool local, double groundHeight)
	{
		for (double value : {minX, minY, maxX, maxY, zoom, pitchDegrees, yawDegrees,
			panHorizontal, panVertical, groundHeight})
			if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return;
		if (w <= 0 || h <= 0 || maxX <= minX || maxY <= minY ||
			zoom <= 0 || pitchDegrees < 35 || pitchDegrees > 72) return;
		width = w; height = h;
		centerX = (minX + maxX) * .5; centerY = (minY + maxY) * .5;
		const double rangeX = std::max(1., maxX - minX), rangeY = std::max(1., maxY - minY);
		scale = 92. / std::max(rangeX, rangeY);
		const double extent = std::max(48., std::max(std::max(14., rangeX * scale * .5 + 8.),
			std::max(14., rangeY * scale * .5 + 8.)) * 1.38) / (std::clamp)(zoom, .55, 64.);
		const MapView view(static_cast<float>(extent), w, h);
		halfWidth = view.halfWidth; halfHeight = view.halfHeight;
		verticalShift = local ? halfHeight * .18 : -8.;
		focusHeight = local ? groundHeight : 0.;
		panX = panHorizontal; panZ = panVertical;
		pitch = pitchDegrees; yaw = std::remainder(yawDegrees, 360.);
		const double radians = 0.017453292519943295;
		cp = std::cos(pitch * radians); sp = std::sin(pitch * radians);
		cy = std::cos(yaw * radians); sy = std::sin(yaw * radians);
		valid = std::isfinite(scale) && scale > 0 && std::isfinite(halfWidth) &&
			std::isfinite(halfHeight) && halfWidth > 0 && halfHeight > 0;
	}

	/** Project an actual world height. Rejected/offscreen points never publish output. */
	bool Project(Point world, double& screenX, double& screenY, bool inside = true) const
	{
		if (!valid || !std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) return false;
		const double x = (world.x - centerX) * scale + panX;
		const double z = (world.y - centerY) * scale + panZ;
		const double y = (world.z - focusHeight) * scale;
		const double ex = cy * x + sy * z, rz = -sy * x + cy * z;
		const double ey = cp * y - sp * rz + verticalShift;
		const double ez = sp * y + cp * rz;
		const double sx = (ex / halfWidth + 1.) * width * .5;
		const double syScreen = (1. - ey / halfHeight) * height * .5;
		if (!std::isfinite(sx) || !std::isfinite(syScreen) || !std::isfinite(ez) ||
			(inside && (sx < 0 || sx >= width || syScreen < 0 || syScreen >= height || std::abs(ez) > 220.))) return false;
		screenX = sx; screenY = syScreen;
		return true;
	}

	/** World ray parameter spans the renderer's front/back clip planes at t=0..440. */
	bool Ray(double screenX, double screenY, Point& origin, Point& direction) const
	{
		if (!valid || !std::isfinite(screenX) || !std::isfinite(screenY)) return false;
		const double ex = (screenX * 2. / width - 1.) * halfWidth;
		const double ey = (1. - screenY * 2. / height) * halfHeight - verticalShift;
		const double ry = ey * cp + 220. * sp, rz = -ey * sp + 220. * cp;
		const Point nextOrigin{centerX + (ex * cy - rz * sy - panX) / scale,
			centerY + (ex * sy + rz * cy - panZ) / scale, ry / scale + focusHeight};
		const Point nextDirection{cp * sy / scale, -cp * cy / scale, -sp / scale};
		if (!std::isfinite(nextOrigin.x) || !std::isfinite(nextOrigin.y) || !std::isfinite(nextOrigin.z) ||
			!std::isfinite(nextDirection.x) || !std::isfinite(nextDirection.y) || !std::isfinite(nextDirection.z)) return false;
		origin = nextOrigin; direction = nextDirection;
		return true;
	}
};
}
