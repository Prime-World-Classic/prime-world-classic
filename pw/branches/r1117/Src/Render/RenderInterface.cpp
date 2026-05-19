#include "stdafx.h"
#include "RenderInterface.h"

#include "../System/MainFrame.h"

#include "smartrenderer.h"
#include "ImmediateRenderer.h"
#include "Rect.h"
#include "ShadowManager.h"
#include "texture.h"
#include "WaterMesh.h"
#include "StaticMesh.h"
#include "SkeletalMesh.h"
#include "ParticleFX.h"
#include "AOERenderer.h"

namespace Render
{

Interface* Interface::s_pSelf = 0;
Interface::Factory Interface::s_creationFactory = 0;

DECLARE_NULL_RENDER_FLAG

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Interface::Interface(HWND hwnd)
	: pScene(0)
  , disableWarFog(false)
{
	unsigned int nWnd = (unsigned int)(uintptr_t)( hwnd ? hwnd : NMainFrame::GetWnd() );
    fprintf(stderr, "Interface::Interface: calling Renderer::Init with nWnd %p\n", (void*)(uintptr_t)nWnd);
    fflush(stderr);
	Renderer::Init(nWnd);

	s_pSelf = this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Interface::~Interface()
{
	Renderer::Term();
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
  fprintf(stderr, "Render::Interface::Start\n");
  fflush(stderr);
  if ( !Render::GetRenderer()->Start( renderMode ) )
	{
    if(RENDER_DISABLED)
      Render::AOERenderer::Init();
    else
	  {
		  //MessageBox( 0, "Failed to set display mode", "Error", MB_OK );
          fprintf(stderr, "Renderer::Start failed\n");
          fflush(stderr);
	  }
		return false;
	}

  fprintf(stderr, "Renderer::Start succeeded, initializing sub-renderers...\n");
  fflush(stderr);
  Render::ImmRenderer::Init();
  Render::SmartRenderer::Init();
  Render::AOERenderer::Init();
  fprintf(stderr, "Render::Interface::Start finished\n");
  fflush(stderr);
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Stop()
{
	Render::UnloadAllTextures();
  Render::AOERenderer::Term();
  Render::ImmRenderer::Term();
	Render::SmartRenderer::Release();
  Render::GetRenderer()->Stop();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Clear()
{
	Render::GetRenderer()->Clear( clearColor );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Clear( Color color )
{
	Render::GetRenderer()->Clear( color );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Present()
{
	static int calls = 0; if (++calls % 60 == 0) printf("Interface::Present called %d\n", calls);
	Render::GetRenderer()->Present();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Present( HWND hWnd, const Render::Rect& sourceRect, const Render::Rect& destRect )
{
	Render::GetRenderer()->Present( hWnd, &sourceRect, &destRect );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::Present( HWND hWnd, const Render::Rect * sourceRect, const Render::Rect* destRect )
{
  Render::GetRenderer()->Present( hWnd, sourceRect, destRect );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::GetTriangleAndDipCount(unsigned int& triangleCount, unsigned int& dipCount)
{
  SmartRenderer::GetTriangleAndDipCount(triangleCount, dipCount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Interface::SetHWND( HWND hwnd)
{
  Render::GetRenderer()->SetHWND(hwnd);
}
} // end namespace Render
