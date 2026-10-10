#pragma once

// No-PCH Unreal translation units still need the platform definitions behind
// UBT's DLLEXPORT/DLLIMPORT macros. Standalone builds keep this header portable.
#ifdef UE_MODULE_NAME
#include "HAL/Platform.h"
#endif

// Unreal builds EclipseCore as its own DLL in the editor and defines this to
// export or import the symbols; everywhere else it expands to nothing.
#ifndef ECLIPSECORE_API
#define ECLIPSECORE_API
#endif
