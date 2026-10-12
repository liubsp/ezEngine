#include <Foundation/Configuration/CVar.h>
#include <Foundation/Configuration/Plugin.h>

// A loadable module with a deliberately unavailable declared dependency. Its callback must not run.
EZ_PLUGIN_DEPENDENCY(ezDeliberatelyMissingRuntimeDependency);

EZ_PLUGIN_ON_LOADED()
{
  if (auto* pCount = static_cast<ezCVarInt*>(ezCVar::FindCVarByName("TestMissingDependencyInitCount")))
    *pCount = *pCount + 1;
}
