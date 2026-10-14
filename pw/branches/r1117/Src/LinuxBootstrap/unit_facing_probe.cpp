#include "unit_facing.h"
#include <array>
#include <cstdio>
#include <limits>

/** Cardinal, scaled, malformed and coincident mock directions exercise presentation geometry only. */
int main()
{
	unsigned checks=0,failed=0;
	const auto check=[&](bool good) { ++checks; if (!good) ++failed; };
	for (const auto& direction : std::array<std::array<double,3>,8>{{
		{{0,1,0}},{{1,0,90}},{{0,-1,180}},{{-1,0,-90}},
		{{1,1,45}},{{1,-1,135}},{{-1,-1,-135}},{{-1,1,-45}}
	}}) {
		for (double scale : {1.,1000.,double(std::numeric_limits<float>::max()),1e300}) {
			float nx=3,ny=4,yaw=999;
			check(LinuxBootstrap::NormalizeFacing(direction[0]*scale,direction[1]*scale,nx,ny));
			check(std::abs(std::hypot(nx,ny)-1)<1e-6);
			check(LinuxBootstrap::FacingYaw(direction[0]*scale,direction[1]*scale,yaw) && std::abs(yaw-direction[2])<1e-5);
		}
	}
	for (double invalid : {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()})
	for (bool second : {false,true}) {
		float nx=3,ny=4,yaw=999;
		const double x=second ? 1 : invalid,y=second ? invalid : 1;
		check(!LinuxBootstrap::NormalizeFacing(x,y,nx,ny) && nx==3 && ny==4);
		check(!LinuxBootstrap::FacingYaw(x,y,yaw) && yaw==999);
	}
	for (double small : {0.,1e-300,.001,.01}) {
		float nx=3,ny=4,yaw=999;
		check(!LinuxBootstrap::NormalizeFacing(small,0,nx,ny) && nx==3 && ny==4);
		check(!LinuxBootstrap::FacingYaw(0,small,yaw) && yaw==999);
	}
	{
		float nx=3,ny=4,yaw=999;
		check(LinuxBootstrap::FacingYaw(.009,.009,yaw) && std::abs(yaw-45)<1e-5);
		const double huge=std::numeric_limits<double>::max();
		check(!LinuxBootstrap::NormalizeFacing(huge,huge,nx,ny) && nx==3 && ny==4);
		check(!LinuxBootstrap::FacingYaw(huge,huge,yaw) && std::abs(yaw-45)<1e-5);
	}
	std::printf("Unit facing: checks=%u failures=%u\n",checks,failed);
	return failed ? 1 : 0;
}
