#pragma once

#include "pch.h"
#include "Debugger/DebugTypes.h"

#define DUMMYCPU
#define ZxCpu DummyZxCpu
#include "ZX/ZxCpu.h"
#undef ZxCpu
#undef DUMMYCPU
