#define WIN32_LEAN_AND_MEAN
#include "System/systemStdAfx.h"
#include <Windows.h>
#include "stdafx.h"
#include "UserMessage.h"
#ifdef NI_PLATF_LINUX
#include <stdio.h>
#include <stdlib.h>
#endif

namespace UserMessage
{
static HWND g_hWnd = 0;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Init( HWND hWnd )
{
  g_hWnd = hWnd;  
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ShowMessageAndTerminate( DWORD dwExceptionCode, LPCSTR lpText )
{
#ifndef NI_PLATF_LINUX
  if( g_hWnd != 0 )
    ShowWindow(g_hWnd, SW_MINIMIZE);

  MessageBox( 
    NULL, lpText,
    "Prime World: The application will be terminated now.", 
    MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND
  );

  RaiseException( dwExceptionCode, EXCEPTION_NONCONTINUABLE, 0, 0 );
#else
  fprintf(stderr, "Prime World: The application will be terminated now.\n%s\n", lpText);
  abort();
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ShowMessageAndTerminate(DWORD dwExceptionCode, LPCWSTR lpTitle, LPCWSTR lpText)
{
#ifndef NI_PLATF_LINUX
  if( g_hWnd != 0 )
    ShowWindow(g_hWnd, SW_MINIMIZE);

  MessageBoxW(NULL, lpText, lpTitle, MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);

  RaiseException( dwExceptionCode, EXCEPTION_NONCONTINUABLE, 0, 0 );
#else
  fprintf(stderr, "Prime World: The application will be terminated now.\n");
  abort();
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}
