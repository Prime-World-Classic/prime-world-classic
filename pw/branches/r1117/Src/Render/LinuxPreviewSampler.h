#pragma once

#if defined(PW_LINUX_NULL_RENDER)
namespace NDb { struct Material; }
namespace Render { namespace LinuxPreviewSampler {

/** Authored addressing only. Legacy preview images still have level-zero linear filtering. */
struct Address {
	bool selected=false, valid=true;
	int u=0, v=0;
};
/** Snapshot supported material metadata; never retain a borrowed DB pointer. */
Address Resolve(const NDb::Material* material);

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
/** Immediate batches clear this selection between owners, independently of shader selection. */
void Select(const NDb::Material* material);
void Clear();
Address Current();

/** Bind a private level-zero sampler on unit zero and restore host bindings on scope exit.
 * Must be constructed/destroyed outside glBegin/glEnd in one current GLX context/thread.
 * Unsupported materials leave the old preview unchanged; invalid selected inputs reject.
 */
class Scope {
	bool good=true, changed=false;
	int active=0, binding=0, sampler=0;
public:
	Scope(unsigned texture, Address address);
	~Scope();
	Scope(const Scope&)=delete;
	Scope& operator=(const Scope&)=delete;
	bool Ok() const { return good; }
};
/** Release the bounded context-owned sampler cache before destroying its GLX context. */
bool Shutdown();
#endif
} }
#endif
