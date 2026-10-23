#include <Core/World/World.h>
#include <Foundation/Threading/Lock.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <JoltPlugin/Actors/JoltDynamicActorComponent.h>
#include <JoltPlugin/System/JoltWorldModule.h>
#include <TestFramework/Framework/TestFramework.h>

EZ_CREATE_SIMPLE_TEST_GROUP(Jolt);

namespace
{
  class OtherBase
  {
  public:
    virtual ~OtherBase() = default;
    ezUInt64 m_uiValue = 0;
  };

  // The component is not the first base, so storing the complete object pointer would lose its adjustment.
  class AuxiliaryOwner : public OtherBase, public ezJoltDynamicActorComponent
  {
  };
} // namespace

EZ_CREATE_SIMPLE_TEST(Jolt, AuxiliaryBodyIdentity)
{
  AuxiliaryOwner owner;
  ezComponent* pComponent = &owner;
  ezJoltUserData userData;
  const ezBitflags<ezOnJoltContact> contactFlags = ezOnJoltContact::SendContactMsg | ezOnJoltContact::ImpactReactions;
  userData.InitAuxiliaryBody(&owner, contactFlags);

  EZ_TEST_BOOL(ezJoltUserData::GetType(&userData) == ezJoltUserData::Type::AuxiliaryBody);
  EZ_TEST_BOOL(userData.GetObject() == pComponent);
  EZ_TEST_BOOL(ezJoltUserData::GetComponent(&userData) == pComponent);
  EZ_TEST_BOOL(ezJoltUserData::GetDynamicActorComponent(&userData) == nullptr);
  EZ_TEST_BOOL(ezJoltUserData::GetTriggerComponent(&userData) == nullptr);
  EZ_TEST_BOOL(ezJoltUserData::GetContactFlags(&userData) == contactFlags);
  EZ_TEST_BOOL(userData.GetContactFlags() == contactFlags);

  userData.Invalidate();
  EZ_TEST_BOOL(ezJoltUserData::GetType(&userData) == ezJoltUserData::Type::Invalid);
  EZ_TEST_BOOL(ezJoltUserData::GetComponent(&userData) == nullptr);
  EZ_TEST_BOOL(ezJoltUserData::GetDynamicActorComponent(&userData) == nullptr);
}

EZ_CREATE_SIMPLE_TEST(Jolt, AuxiliaryBodyActivation)
{
  ezWorldDesc desc("AuxiliaryBodyActivation");
  ezWorld world(desc);
  EZ_LOCK(world.GetWriteMarker());
  auto* pModule = world.GetOrCreateModule<ezJoltWorldModule>();
  auto& bodies = pModule->GetBodyInterface();

  ezJoltDynamicActorComponent owner;
  ezJoltUserData actorData;
  actorData.Init(&owner, ezOnJoltContact::None);
  ezJoltUserData auxiliaryData;
  auxiliaryData.InitAuxiliaryBody(&owner, ezOnJoltContact::SendContactMsg);
  EZ_TEST_BOOL(ezJoltUserData::GetDynamicActorComponent(&actorData) == &owner);

  JPH::RefConst<JPH::Shape> pShape = new JPH::SphereShape(0.5f);
  const auto layer = ezJoltCollisionFiltering::ConstructObjectLayer(0, ezJoltBroadphaseLayer::Dynamic);
  JPH::BodyCreationSettings settings(pShape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, layer);
  settings.mUserData = reinterpret_cast<JPH::uint64>(&actorData);
  JPH::Body* pActor = bodies.CreateBody(settings);
  EZ_TEST_BOOL(pActor != nullptr);
  if (pActor == nullptr)
    return;

  const JPH::BodyID actorId = pActor->GetID();
  EZ_SCOPE_EXIT(bodies.RemoveBody(actorId); bodies.DestroyBody(actorId));
  bodies.AddBody(actorId, JPH::EActivation::Activate);
  EZ_TEST_BOOL(pModule->GetActiveActors().Contains(&owner));

  settings.mPosition = JPH::RVec3(2, 0, 0);
  settings.mUserData = reinterpret_cast<JPH::uint64>(&auxiliaryData);
  JPH::Body* pAuxiliary = bodies.CreateBody(settings);
  EZ_TEST_BOOL(pAuxiliary != nullptr);
  if (pAuxiliary == nullptr)
    return;

  const JPH::BodyID auxiliaryId = pAuxiliary->GetID();
  EZ_SCOPE_EXIT(bodies.RemoveBody(auxiliaryId); bodies.DestroyBody(auxiliaryId));
  bodies.AddBody(auxiliaryId, JPH::EActivation::Activate);
  EZ_TEST_INT(pModule->GetActiveActors().GetCount(), 1);

  bodies.DeactivateBody(auxiliaryId);
  EZ_TEST_BOOL(pModule->GetActiveActors().Contains(&owner));
  bodies.ActivateBody(auxiliaryId);
  EZ_TEST_INT(pModule->GetActiveActors().GetCount(), 1);

  bodies.DeactivateBody(actorId);
  EZ_TEST_BOOL(pModule->GetActiveActors().IsEmpty());
  bodies.DeactivateBody(auxiliaryId);
  bodies.ActivateBody(auxiliaryId);
  EZ_TEST_BOOL(pModule->GetActiveActors().IsEmpty());
  bodies.ActivateBody(actorId);
  EZ_TEST_BOOL(pModule->GetActiveActors().Contains(&owner));
}
