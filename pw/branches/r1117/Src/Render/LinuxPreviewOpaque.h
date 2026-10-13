#pragma once

#if defined(PW_LINUX_NULL_RENDER)
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include <thread>
#endif
namespace NDb { struct Material; }
namespace Render { namespace LinuxPreviewOpaque {
/** Snapshot only ordinary opaque BasicMaterial alpha policy, without retaining DB pointers.
 * Culling, lighting, emissive, fading and transparent passes are not implemented here.
 */
struct State {
	bool selected=false, valid=true;
	int alphaTest=0, alphaRef=127;
};
/** Reuse original BasicMaterial/RenderStatesManager admission and raw alpha-test fields. */
State Resolve(const NDb::Material* material);
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
/** Selection belongs to one immediate batch and is cleared between renderer owners. */
void Select(const NDb::Material* material);
void Clear();
State Current();
/** Temporarily disable blending on output zero and apply the authored GEQUAL cutout.
 * Requires the native preview's OpenGL compatibility profile.
 * Keep one context/thread current for the entire scope, outside glBegin/glEnd.
 * Restore host alpha-test and output-zero blend enable; other outputs are untouched.
 */
class Scope {
	bool good=true, changed=false, blend=false, alpha=false;
	int alphaFunction=0;
	float alphaReference=0;
	void* context=nullptr;
	std::thread::id thread;
public:
	explicit Scope(State state);
	~Scope();
	Scope(const Scope&)=delete;
	Scope& operator=(const Scope&)=delete;
	bool Ok() const { return good; }
	/** Eligible opaque shaders use texture alpha directly, without preview vertex attenuation. */
	bool TextureAlpha() const { return changed && good; }
};
#endif
} }
#endif
