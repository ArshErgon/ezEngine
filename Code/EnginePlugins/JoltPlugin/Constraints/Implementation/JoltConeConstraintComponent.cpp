#include <JoltPlugin/JoltPluginPCH.h>

#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <JoltPlugin/Constraints/JoltConeConstraintComponent.h>
#include <JoltPlugin/System/JoltWorldModule.h>

// clang-format off
EZ_BEGIN_COMPONENT_TYPE(ezJoltConeConstraintComponent, 1, ezComponentMode::Static)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ACCESSOR_PROPERTY("ConeAngle", GetConeAngle, SetConeAngle)->AddAttributes(new ezClampValueAttribute(ezAngle(), ezAngle::Degree(175))),
  }
  EZ_END_PROPERTIES;
  EZ_BEGIN_ATTRIBUTES
  {
    new ezConeVisualizerAttribute(ezBasisAxis::PositiveX, "ConeAngle", 0.3f, nullptr)
  }
  EZ_END_ATTRIBUTES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

ezJoltConeConstraintComponent::ezJoltConeConstraintComponent() = default;
ezJoltConeConstraintComponent::~ezJoltConeConstraintComponent() = default;

void ezJoltConeConstraintComponent::SerializeComponent(ezWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);

  auto& s = stream.GetStream();

  s << m_ConeAngle;
}

void ezJoltConeConstraintComponent::DeserializeComponent(ezWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  const ezUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());

  auto& s = stream.GetStream();

  s >> m_ConeAngle;
}

void ezJoltConeConstraintComponent::CreateContstraintType(JPH::Body* pBody0, JPH::Body* pBody1)
{
  const auto diff0 = pBody0->GetPosition() - pBody0->GetCenterOfMassPosition();
  const auto diff1 = pBody1->GetPosition() - pBody1->GetCenterOfMassPosition();

  JPH::ConeConstraintSettings opt;
  opt.mDrawConstraintSize = 0.1f;
  opt.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
  opt.mPoint1 = ezJoltConversionUtils::ToVec3(m_localFrameA.m_vPosition) + diff0;
  opt.mPoint2 = ezJoltConversionUtils::ToVec3(m_localFrameB.m_vPosition) + diff1;
  opt.mHalfConeAngle = m_ConeAngle.GetRadian() * 0.5f;
  opt.mTwistAxis1 = ezJoltConversionUtils::ToVec3(m_localFrameA.m_qRotation * ezVec3::UnitXAxis());
  opt.mTwistAxis2 = ezJoltConversionUtils::ToVec3(m_localFrameB.m_qRotation * ezVec3::UnitXAxis());

  m_pConstraint = opt.Create(*pBody0, *pBody1);
}

void ezJoltConeConstraintComponent::ApplySettings()
{
  ezJoltConstraintComponent::ApplySettings();

  auto pConstraint = static_cast<JPH::ConeConstraint*>(m_pConstraint);
  pConstraint->SetHalfConeAngle(m_ConeAngle.GetRadian() * 0.5f);

  if (pConstraint->GetBody2()->IsInBroadPhase())
  {
    // wake up the bodies that are attached to this constraint
    ezJoltWorldModule* pModule = GetWorld()->GetOrCreateModule<ezJoltWorldModule>();
    pModule->GetJoltSystem()->GetBodyInterface().ActivateBody(pConstraint->GetBody2()->GetID());
  }
}

void ezJoltConeConstraintComponent::SetConeAngle(ezAngle f)
{
  m_ConeAngle = f;
  QueueApplySettings();
}
