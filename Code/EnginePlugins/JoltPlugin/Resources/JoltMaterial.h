#pragma once

#include <Core/Physics/SurfaceResource.h>
#include <JoltPlugin/JoltPluginDLL.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>

class EZ_JOLTPLUGIN_DLL ezJoltMaterial : public JPH::PhysicsMaterial
{
public:
  ezJoltMaterial();
  ~ezJoltMaterial();

  ezSurfaceResource* m_pSurface = nullptr;

  float m_fRestitution = 0.0f;
  float m_fFriction = 0.2f;
};
