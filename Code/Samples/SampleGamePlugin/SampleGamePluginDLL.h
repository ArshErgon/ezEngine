#pragma once

#include <Foundation/Basics.h>
#include <Foundation/Configuration/Plugin.h>

// BEGIN-DOCS-CODE-SNIPPET: dll-export-defines
// Configure the DLL Import/Export Define
#if EZ_ENABLED(EZ_COMPILE_ENGINE_AS_DLL)
#  ifdef BUILDSYSTEM_BUILDING_SAMPLEGAMEPLUGIN_LIB
#    define EZ_SAMPLEGAMEPLUGIN_DLL __declspec(dllexport)
#  else
#    define EZ_SAMPLEGAMEPLUGIN_DLL __declspec(dllimport)
#  endif
#else
#  define EZ_SAMPLEGAMEPLUGIN_DLL
#endif
// END-DOCS-CODE-SNIPPET
