#include "stdafx.h"
#include "LinuxPreviewSampler.h"

#if defined(PW_LINUX_NULL_RENDER)
#include "DBRender.h"
#include "DBRenderResources.h"
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include <GL/gl.h>
#include <GL/glx.h>
#include <array>
#include <thread>
#endif

namespace Render { namespace LinuxPreviewSampler {
Address Resolve(const NDb::Material* material)
{
	const auto* basic=dynamic_cast<const NDb::BasicMaterial*>(material);
	if (!basic || !basic->DiffuseMap.texture) return {};
	Address result;
	result.selected=true;
	result.u=basic->DiffuseMap.samplerState.addressU;
	result.v=basic->DiffuseMap.samplerState.addressV;
	result.valid=result.u>=NDb::TEXTUREADDRESSTYPE_WRAP && result.u<=NDb::TEXTUREADDRESSTYPE_BORDER &&
		result.v>=NDb::TEXTUREADDRESSTYPE_WRAP && result.v<=NDb::TEXTUREADDRESSTYPE_BORDER;
	return result;
}
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
namespace {
Address current;
GLXContext owner=nullptr;
std::thread::id ownerThread;
std::array<GLuint,16> samplers{};
unsigned activeScopes=0;
PFNGLBINDSAMPLERPROC bindSampler=nullptr;
PFNGLGENSAMPLERSPROC genSamplers=nullptr;
PFNGLDELETESAMPLERSPROC deleteSamplers=nullptr;
PFNGLSAMPLERPARAMETERIPROC parameter=nullptr;

/** Sampler objects isolate addressing from aliased textures and other texture units. */
bool Ready()
{
	const auto context=glXGetCurrentContext();
	if (!context) return false;
	if (owner) return owner==context && ownerThread==std::this_thread::get_id();
	GLint major=0, minor=0;
	glGetIntegerv(GL_MAJOR_VERSION,&major); glGetIntegerv(GL_MINOR_VERSION,&minor);
	if (glGetError()!=GL_NO_ERROR || major<3 || (major==3 && minor<3)) return false;
	bindSampler=reinterpret_cast<PFNGLBINDSAMPLERPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBindSampler")));
	genSamplers=reinterpret_cast<PFNGLGENSAMPLERSPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glGenSamplers")));
	deleteSamplers=reinterpret_cast<PFNGLDELETESAMPLERSPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDeleteSamplers")));
	parameter=reinterpret_cast<PFNGLSAMPLERPARAMETERIPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glSamplerParameteri")));
	if (!bindSampler || !genSamplers || !deleteSamplers || !parameter) return false;
	owner=context; ownerThread=std::this_thread::get_id();
	return true;
}
}
void Select(const NDb::Material* material) { current=Resolve(material); }
void Clear() { current={}; }
Address Current() { return current; }
Scope::Scope(unsigned texture, Address address)
{
	if (!address.selected) return;
	good=false;
	if (!address.valid || address.u<0 || address.u>3 || address.v<0 || address.v>3 ||
		!texture || !Ready() || glGetError()!=GL_NO_ERROR || !glIsTexture(texture)) return;
	glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding); glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
	changed=true; ++activeScopes;
	glBindTexture(GL_TEXTURE_2D,texture);
	GLint width=0, height=0;
	glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
	const GLenum textureError=glGetError();
	if (textureError!=GL_NO_ERROR || width<=0 || height<=0) return;
	auto& cached=samplers[size_t(address.u)*4+size_t(address.v)];
	if (!cached) {
		GLuint candidate=0; genSamplers(1,&candidate);
		const GLenum allocationError=glGetError();
		if (!candidate || allocationError!=GL_NO_ERROR) { if (candidate) deleteSamplers(1,&candidate); return; }
		const GLint wrap[]={GL_REPEAT,GL_CLAMP_TO_EDGE,GL_MIRRORED_REPEAT,GL_CLAMP_TO_BORDER};
		parameter(candidate,GL_TEXTURE_WRAP_S,wrap[address.u]); parameter(candidate,GL_TEXTURE_WRAP_T,wrap[address.v]);
		parameter(candidate,GL_TEXTURE_MIN_FILTER,GL_LINEAR); parameter(candidate,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		parameter(candidate,GL_TEXTURE_MIN_LOD,0); parameter(candidate,GL_TEXTURE_MAX_LOD,0);
		if (glGetError()!=GL_NO_ERROR) { deleteSamplers(1,&candidate); return; }
		cached=candidate;
	}
	bindSampler(0,cached);
	good=glGetError()==GL_NO_ERROR;
}
Scope::~Scope()
{
	if (!changed) return;
	if (owner!=glXGetCurrentContext() || ownerThread!=std::this_thread::get_id()) {
		fprintf(stderr,"Native preview sampler scope outlived its current context/thread.\n");
		return;
	}
	glActiveTexture(GL_TEXTURE0); bindSampler(0,GLuint(sampler));
	glBindTexture(GL_TEXTURE_2D,GLuint(binding)); glActiveTexture(GLenum(active));
	--activeScopes;
}
bool Shutdown()
{
	if (activeScopes || (owner && (owner!=glXGetCurrentContext() || ownerThread!=std::this_thread::get_id()))) return false;
	if (owner && glGetError()!=GL_NO_ERROR) return false;
	unsigned released=0;
	for (auto& sampler : samplers) if (sampler) {
		deleteSamplers(1,&sampler);
		if (glGetError()!=GL_NO_ERROR) return false;
		sampler=0; ++released;
	}
	if (released) fprintf(stdout,"Native preview sampler shutdown: released=%u\n",released);
	owner=nullptr; ownerThread={}; Clear();
	return true;
}
#endif
} }
#endif
