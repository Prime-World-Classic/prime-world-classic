#pragma once

#include "../System/systemStdAfx.h"
#include "../System/BoundCalcer.h"
#include "../libdb/libdbStdAfx.h"

#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "specific.h"
#include "../Render/NullRenderSignal.h"
#else
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#if defined(PW_LINUX_NULL_RENDER)
#include "../Render/DeviceLost.h"
#include "../Render/NullRenderSignal.h"
#include "../Render/sceneconstants.h"
#include "../Render/TextureManager.h"
#endif
#endif

#if defined(NV_LINUX_PLATFORM)
  #define SCENE_CPP_ALIGN16 __attribute__((aligned(16)))
#else
  #define SCENE_CPP_ALIGN16 __declspec(align(16))
#endif
