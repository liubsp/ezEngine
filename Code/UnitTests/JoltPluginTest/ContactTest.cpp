#include <Core/World/World.h>
#include <Foundation/Threading/Lock.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <JoltPlugin/Resources/JoltMaterial.h>
#include <JoltPlugin/System/JoltWorldModule.h>
#include <TestFramework/Framework/TestFramework.h>

#include <limits>

EZ_CREATE_SIMPLE_TEST(Jolt, ContactMaterialFriction)
{
  ezWorldDesc desc("ContactMaterialFriction");
  ezWorld world(desc);
  EZ_LOCK(world.GetWriteMarker());
  auto* pModule = world.GetOrCreateModule<ezJoltWorldModule>();
  auto& bodies = pModule->GetBodyInterface();
  JPH::ContactListener* pListener = pModule->GetJoltSystem()->GetContactListener();
  EZ_TEST_BOOL(pListener != nullptr);
  if (pListener == nullptr)
    return;

  JPH::Ref<ezJoltMaterial> pMaterial0 = new ezJoltMaterial;
  JPH::Ref<ezJoltMaterial> pMaterial1 = new ezJoltMaterial;
  pMaterial0->m_fRestitution = 0.1f;
  pMaterial1->m_fRestitution = 0.3f;
  JPH::RefConst<JPH::Shape> pShape0 = new JPH::SphereShape(0.5f, pMaterial0);
  JPH::RefConst<JPH::Shape> pShape1 = new JPH::SphereShape(0.5f, pMaterial1);
  const auto layer = ezJoltCollisionFiltering::ConstructObjectLayer(0, ezJoltBroadphaseLayer::Dynamic);
  JPH::BodyCreationSettings bodySettings(pShape0, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, layer);
  JPH::Body* pBody0 = bodies.CreateBody(bodySettings);
  EZ_TEST_BOOL(pBody0 != nullptr);
  if (pBody0 == nullptr)
    return;
  EZ_SCOPE_EXIT(bodies.DestroyBody(pBody0->GetID()));

  bodySettings.SetShape(pShape1);
  JPH::Body* pBody1 = bodies.CreateBody(bodySettings);
  EZ_TEST_BOOL(pBody1 != nullptr);
  if (pBody1 == nullptr)
    return;
  EZ_SCOPE_EXIT(bodies.DestroyBody(pBody1->GetID()));

  struct FrictionCase
  {
    const char* m_szName;
    float m_fFriction0;
    float m_fFriction1;
    float m_fExpected;
  };
  constexpr float fMax = std::numeric_limits<float>::max();
  const FrictionCase cases[] = {
    {"Ordinary", 0.8f, 1.3f, 1.0198039f},
    {"Zero", 0.0f, fMax, 0.0f},
    {"Overflowing product", fMax, 1.3f, 2.1032523e19f},
    {"Maximum", fMax, fMax, fMax},
    // Both inputs are normal floats, but their float product would underflow to zero.
    {"Underflowing product", 0x1p-100f, 0x1p-100f, 0x1p-100f},
  };

  JPH::ContactManifold manifold;
  for (const auto& testCase : cases)
  {
    for (ezUInt32 uiSwap = 0; uiSwap < 2; ++uiSwap)
    {
      pMaterial0->m_fFriction = uiSwap == 0 ? testCase.m_fFriction0 : testCase.m_fFriction1;
      pMaterial1->m_fFriction = uiSwap == 0 ? testCase.m_fFriction1 : testCase.m_fFriction0;

      // Reuse the same bodies and shapes to include material edits on persisted contacts.
      for (ezUInt32 uiPersistent = 0; uiPersistent < 2; ++uiPersistent)
      {
        JPH::ContactSettings settings;
        settings.mCombinedFriction = -1.0f;
        if (uiPersistent == 0)
          pListener->OnContactAdded(*pBody0, *pBody1, manifold, settings);
        else
          pListener->OnContactPersisted(*pBody0, *pBody1, manifold, settings);

        EZ_TEST_BOOL_MSG(ezMath::IsFinite(settings.mCombinedFriction), "%s, swapped=%u, persisted=%u", testCase.m_szName, uiSwap, uiPersistent);
        EZ_TEST_BOOL(settings.mCombinedFriction >= 0.0f);
        if (testCase.m_fExpected == 0.0f)
        {
          EZ_TEST_FLOAT(settings.mCombinedFriction, 0.0f, 0.0f);
        }
        else
        {
          // Relative tolerance covers float rounding without hiding small nonzero means.
          EZ_TEST_FLOAT_MSG(settings.mCombinedFriction / testCase.m_fExpected, 1.0f, 2.0f * std::numeric_limits<float>::epsilon(),
            "%s, swapped=%u, persisted=%u", testCase.m_szName, uiSwap, uiPersistent);
        }
        EZ_TEST_FLOAT(settings.mCombinedRestitution, 0.3f, 0.0f);
      }
    }
  }
}
