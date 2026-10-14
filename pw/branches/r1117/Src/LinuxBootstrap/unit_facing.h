#pragma once

#include <cmath>

namespace LinuxBootstrap
{
/** Normalize native float XY directions in double; preserve outputs for invalid or tiny norms. */
inline bool NormalizeFacing(double x,double y,float& nx,float& ny)
{
	if (!std::isfinite(x) || !std::isfinite(y)) return false;
	const double length=std::hypot(x,y);
	if (!std::isfinite(length) || length<=.01) return false;
	nx=static_cast<float>(x/length); ny=static_cast<float>(y/length);
	return true;
}

/** Native XY maps to OpenGL XZ; positive yaw rotates forward +Z toward +X. */
inline bool FacingYaw(double x,double y,float& yaw)
{
	float nx=0,ny=0;
	if (!NormalizeFacing(x,y,nx,ny)) return false;
	yaw=static_cast<float>(std::atan2(nx,ny)*57.29577951308232);
	return true;
}
}
