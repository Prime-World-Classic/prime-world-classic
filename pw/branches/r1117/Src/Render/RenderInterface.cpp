#include "stdafx.h"
#include "RenderInterface.h"

#if !defined(PW_LINUX_NULL_RENDER)
#include "../System/MainFrame.h"

#include "smartrenderer.h"
#include "ImmediateRenderer.h"
#include "rect.h"
#include "ShadowManager.h"
#include "texture.h"
#include "WaterMesh.h"
#include "StaticMesh.h"
#include "SkeletalMesh.h"
#include "ParticleFX.h"
#include "AOERenderer.h"
#endif

namespace Render
{

Interface* Interface::s_pSelf = 0;
Interface::Factory Interface::s_creationFactory = 0;

#if !defined(PW_LINUX_NULL_RENDER)
DECLARE_NULL_RENDER_FLAG
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Interface::Interface(HWND hwnd)
	: pScene(0)
  , disableWarFog(false)
{
#if defined(PW_LINUX_NULL_RENDER)
  (void)hwnd;
  clearColor = Color();
  s_pSelf = this;
#else
	unsigned int nWnd = (unsigned int)( hwnd ? hwnd : NMainFrame::GetWnd() );
	Renderer::Init(nWnd);

	s_pSelf = this;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Interface::~Interface()
{
#if !defined(PW_LINUX_NULL_RENDER)
	Renderer::Term();
#endif
	s_pSelf = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Interface *Interface::Get() { return s_pSelf; }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::RegisterFactory(Factory func) { s_creationFactory = func; }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Interface *Interface::Create(HWND hwnd) 
{ 
	NI_ASSERT(s_creationFactory, "Render::Interface creation factory should be already set");
	return s_creationFactory(hwnd); 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Interface::Start( RenderMode& renderMode )
{
#if defined(PW_LINUX_NULL_RENDER)
  (void)renderMode;
  return true;
#else
  if ( !Render::GetRenderer()->Start( renderMode ) )
	{
    if(RENDER_DISABLED)
      Render::AOERenderer::Init();
    else
		  MessageBox( 0, "Failed to set display mode", "Error", MB_OK );
		return false;
	}

  Render::ImmRenderer::Init();
  Render::SmartRenderer::Init();
  Render::AOERenderer::Init();
	return true;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Stop()
{
#if !defined(PW_LINUX_NULL_RENDER)
	Render::UnloadAllTextures();
  Render::AOERenderer::Term();
  Render::ImmRenderer::Term();
	Render::SmartRenderer::Release();
  Render::GetRenderer()->Stop();
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Clear()
{
#if !defined(PW_LINUX_NULL_RENDER)
	Render::GetRenderer()->Clear( clearColor );
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Clear( Color color )
{
#if defined(PW_LINUX_NULL_RENDER)
  clearColor = color;
#else
	Render::GetRenderer()->Clear( color );
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Present()
{
#if !defined(PW_LINUX_NULL_RENDER)
	Render::GetRenderer()->Present();
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Present( HWND hWnd, const Render::Rect& sourceRect, const Render::Rect& destRect )
{
#if defined(PW_LINUX_NULL_RENDER)
  (void)hWnd;
  (void)sourceRect;
  (void)destRect;
#else
	Render::GetRenderer()->Present( hWnd, &sourceRect, &destRect );
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Present( HWND hWnd, const Render::Rect * sourceRect, const Render::Rect* destRect )
{
#if defined(PW_LINUX_NULL_RENDER)
  (void)hWnd;
  (void)sourceRect;
  (void)destRect;
#else
  Render::GetRenderer()->Present( hWnd, sourceRect, destRect );
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::GetTriangleAndDipCount(unsigned int& triangleCount, unsigned int& dipCount)
{
#if defined(PW_LINUX_NULL_RENDER)
  triangleCount = 0;
  dipCount = 0;
#else
  SmartRenderer::GetTriangleAndDipCount(triangleCount, dipCount);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::SetHWND( HWND hwnd)
{
#if defined(PW_LINUX_NULL_RENDER)
  (void)hwnd;
#else
  Render::GetRenderer()->SetHWND(hwnd);
#endif
}
} // end namespace Render
