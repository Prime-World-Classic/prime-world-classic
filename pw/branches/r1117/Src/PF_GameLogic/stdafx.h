#pragma once

#include "../System/systemStdAfx.h"
#include "../libdb/libdbStdAfx.h"

#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "specific.h"
#include <Render/NullRenderSignal.h>
#else
#include "StringExecutorBootstrap.h"
#endif
