#pragma once

// Unreal builds EclipseCore as its own DLL in the editor and defines this to
// export or import the symbols; everywhere else it expands to nothing.
#ifndef ECLIPSECORE_API
#define ECLIPSECORE_API
#endif
