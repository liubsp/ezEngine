#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <JoltPlugin/Resources/JoltMaterial.h>
#include <JoltPlugin/System/JoltCore.h>
#include <JoltPluginTest_Plugin/MaterialConsumer.h>
#include <TestFramework/Framework/TestFramework.h>

void TestJoltMaterialConsumer()
{
  const auto* pDefaultMaterial = ezJoltCore::GetDefaultMaterial();
  const float fDefaultFriction = pDefaultMaterial->m_fFriction;
  JPH::Ref<ezJoltMaterial> pMaterial = new ezJoltMaterial;
  pMaterial->m_fFriction = 0.8f;
  pMaterial->m_fRestitution = 0.1f;

  JPH::RefConst<JPH::Shape> pShape = new JPH::SphereShape(0.5f, pMaterial);
  const auto* pShapeMaterial = static_cast<const ezJoltMaterial*>(pShape->GetMaterial(JPH::SubShapeID()));
  EZ_TEST_BOOL(pShapeMaterial == pMaterial.GetPtr());
  EZ_TEST_FLOAT(pShapeMaterial->m_fFriction, 0.8f, 0.0f);
  EZ_TEST_FLOAT(pShapeMaterial->m_fRestitution, 0.1f, 0.0f);
  EZ_TEST_FLOAT(pDefaultMaterial->m_fFriction, fDefaultFriction, 0.0f);

  // Exercise the imported destructor directly as well as Jolt's reference-counted ownership.
  ezJoltMaterial material;
  EZ_TEST_BOOL(material.m_pSurface == nullptr);
}
