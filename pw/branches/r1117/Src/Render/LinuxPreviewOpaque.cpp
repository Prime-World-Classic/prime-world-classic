#include "stdafx.h"
#include "LinuxPreviewOpaque.h"

#if defined(PW_LINUX_NULL_RENDER)
#include "DBRender.h"
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include <GL/gl.h>
#include <GL/glx.h>
#endif
namespace Render { namespace LinuxPreviewOpaque {
State Resolve(const NDb::Material* material)
{
	const auto* basic=dynamic_cast<const NDb::BasicMaterial*>(material);
	if (!basic || basic->ignoreRenderState || basic->isBlendModeOverrided || basic->specialTransparency ||
		basic->renderState.blendMode!=NDb::BLENDMODE_OFF || basic->EmissivePinValue!=NDb::EMISSIVEPIN_EMISSIVEOFF ||
		basic->OpacityPinValue!=NDb::BOOLEANPIN_NONE) return {};
	State result; result.selected=true;
	result.alphaTest=basic->renderState.alphaTest; result.alphaRef=basic->renderState.alphaTestRef;
	result.valid=(result.alphaTest==NDb::ONOFFSTATE_OFF || result.alphaTest==NDb::ONOFFSTATE_ON) &&
		result.alphaRef>=0 && result.alphaRef<=255;
	return result;
}
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
namespace {
State current;
PFNGLENABLEIPROC enablei=nullptr;
PFNGLDISABLEIPROC disablei=nullptr;
PFNGLISENABLEDIPROC isEnabledi=nullptr;

/** Indexed enable avoids changing borrowed blend enables on other render outputs. */
bool Ready()
{
	if (enablei && disablei && isEnabledi) return true;
	enablei=reinterpret_cast<PFNGLENABLEIPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glEnablei")));
	disablei=reinterpret_cast<PFNGLDISABLEIPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDisablei")));
	isEnabledi=reinterpret_cast<PFNGLISENABLEDIPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glIsEnabledi")));
	return enablei && disablei && isEnabledi;
}
}
void Select(const NDb::Material* material) { current=Resolve(material); }
void Clear() { current={}; }
State Current() { return current; }
Scope::Scope(State state)
{
	if (!state.selected) return;
	good=false;
	if (!state.valid || state.alphaTest<0 || state.alphaTest>1 || state.alphaRef<0 || state.alphaRef>255 ||
		!glXGetCurrentContext() || glGetError()!=GL_NO_ERROR || !Ready()) return;
	blend=isEnabledi(GL_BLEND,0); alpha=glIsEnabled(GL_ALPHA_TEST);
	glGetIntegerv(GL_ALPHA_TEST_FUNC,&alphaFunction); glGetFloatv(GL_ALPHA_TEST_REF,&alphaReference);
	if (glGetError()!=GL_NO_ERROR) return;
	context=glXGetCurrentContext(); thread=std::this_thread::get_id(); changed=true;
	disablei(GL_BLEND,0);
	glAlphaFunc(GL_GEQUAL,float(state.alphaRef)/255.f);
	if (state.alphaTest) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
	good=glGetError()==GL_NO_ERROR;
}
Scope::~Scope()
{
	if (!changed) return;
	if (context!=glXGetCurrentContext() || thread!=std::this_thread::get_id()) {
		fprintf(stderr,"Native opaque scope outlived its current context/thread.\n");
		return;
	}
	(blend ? enablei : disablei)(GL_BLEND,0);
	glAlphaFunc(GLenum(alphaFunction),alphaReference);
	if (alpha) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
}
#endif
} }
#endif
