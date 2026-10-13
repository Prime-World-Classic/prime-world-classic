#pragma once
#if defined(PW_LINUX_NULL_RENDER)
#include "../LinuxBootstrap/dds_pixels.h"
class Stream;
namespace Render {
	namespace LinuxDdsPreview {
		/** Native preview's straight RGBA base image, independent of GL texture ownership. */
		using Image = LinuxBootstrap::DdsPixels;
		/** Decode a bounded DDS base image from a borrowed stream; clear output on failure.
		 * BC1/2/3 use Squish; RGB16/24/32 and aligned DXT2/4 retain legacy conversion.
		 * This does not validate an unused mip tail or replace the GPU mip-chain inspector.
		 */
		bool Load(Stream *source, Image &output);
	} // namespace LinuxDdsPreview
} // namespace Render
#endif
