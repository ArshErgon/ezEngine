#include <JoltPlugin/JoltPluginPCH.h>

#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <JoltPlugin/Constraints/JoltSliderConstraintComponent.h>
#include <JoltPlugin/System/JoltCore.h>
#include <JoltPlugin/System/JoltWorldModule.h>

// clang-format off
EZ_BEGIN_COMPONENT_TYPE(ezJoltSliderConstraintComponent, 1, ezComponentMode::Static)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ENUM_ACCESSOR_PROPERTY("LimitMode", ezJoltConstraintLimitMode, GetLimitMode, SetLimitMode),
    EZ_ACCESSOR_PROPERTY("LowerLimit", GetLowerLimitDistance, SetLowerLimitDistance)->AddAttributes(new ezClampValueAttribute(0.0f, ezVariant())),
    EZ_ACCESSOR_PROPERTY("UpperLimit", GetUpperLimitDistance, SetUpperLimitDistance)->AddAttributes(new ezClampValueAttribute(0.0f, ezVariant())),
    EZ_ACCESSOR_PROPERTY("Friction", GetFriction, SetFriction)->AddAttributes(new ezClampValueAttribute(0.0f, ezVariant())),
    EZ_ENUM_ACCESSOR_PROPERTY("DriveMode", ezJoltConstraintDriveMode, GetDriveMode, SetDriveMode),
    EZ_ACCESSOR_PROPERTY("DriveTargetValue", GetDriveTargetValue, SetDriveTargetValue),
    EZ_ACCESSOR_PROPERTY("DriveStrength", GetDriveStrength, SetDriveStrength)->AddAttributes(new ezClampValueAttribute(0.0f, ezVariant()), new ezMinValueTextAttribute("Maximum")),
  }
  EZ_END_PROPERTIES;

  EZ_BEGIN_ATTRIBUTES
  {
    new ezDirectionVisualizerAttribute(ezBasisAxis::PositiveX, 1.0f, ezColor::Orange, nullptr, "UpperLimit"),
    new ezDirectionVisualizerAttribute(ezBasisAxis::NegativeX, 1.0f, ezColor::Teal, nullptr, "LowerLimit"),
  }
  EZ_END_ATTRIBUTES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

ezJoltSliderConstraintComponent::ezJoltSliderConstraintComponent() = default;
ezJoltSliderConstraintComponent::~ezJoltSliderConstraintComponent() = default;

void ezJoltSliderConstraintComponent::SerializeComponent(ezWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);

  auto& s = stream.GetStream();

  s << m_fLowerLimitDistance;
  s << m_fUpperLimitDistance;
  s << m_fFriction;
  s << m_LimitMode;

  s << m_DriveMode;
  s << m_fDriveTargetValue;
  s << m_fDriveStrength;
}

void ezJoltSliderConstraintComponent::DeserializeComponent(ezWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  const ezUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());

  auto& s = stream.GetStream();

  s >> m_fLowerLimitDistance;
  s >> m_fUpperLimitDistance;
  s >> m_fFriction;
  s >> m_LimitMode;

  s >> m_DriveMode;
  s >> m_fDriveTargetValue;
  s >> m_fDriveStrength;
}

void ezJoltSliderConstraintComponent::SetLimitMode(ezJoltConstraintLimitMode::Enum mode)
{
  m_LimitMode = mode;
  QueueApplySettings();
}

void ezJoltSliderConstraintComponent::SetLowerLimitDistance(float f)
{
  m_fLowerLimitDistance = f;
  QueueApplySettings();
}

void ezJoltSliderConstraintComponent::SetUpperLimitDistance(float f)
{
  m_fUpperLimitDistance = f;
  QueueApplySettings();
}

void ezJoltSliderConstraintComponent::SetFriction(float f)
{
  m_fFriction = f;
  QueueApplySettings();
}

void ezJoltSliderConstraintComponent::SetDriveMode(ezJoltConstraintDriveMode::Enum mode)
{
  m_DriveMode = mode;
  QueueApplySettings();
}

void ezJoltSliderConstraintComponent::SetDriveTargetValue(float f)
{
  m_fDriveTargetValue = f;
  QueueApplySettings();
}

void ezJoltSliderConstraintComponent::SetDriveStrength(float f)
{
  m_fDriveStrength = ezMath::Max(f, 0.0f);
  QueueApplySettings();
}


void ezJoltSliderConstraintComponent::ApplySettings()
{
  ezJoltConstraintComponent::ApplySettings();

  JPH::SliderConstraint* pConstraint = static_cast<JPH::SliderConstraint*>(m_pConstraint);

  pConstraint->SetMaxFrictionForce(m_fFriction);

  if (m_LimitMode != ezJoltConstraintLimitMode::NoLimit)
  {
    float low = m_fLowerLimitDistance;
    float high = m_fUpperLimitDistance;

    if (low == high) // both zero
    {
      high = low + 0.01f;
    }

    pConstraint->SetLimits(-low, high);
  }
  else
  {
    pConstraint->SetLimits(-FLT_MAX, FLT_MAX);
  }

  // drive
  {
    if (m_DriveMode == ezJoltConstraintDriveMode::NoDrive)
    {
      pConstraint->SetMotorState(JPH::EMotorState::Off);
    }
    else
    {
      if (m_DriveMode == ezJoltConstraintDriveMode::DriveVelocity)
      {
        pConstraint->SetMotorState(JPH::EMotorState::Velocity);
        pConstraint->SetTargetVelocity(m_fDriveTargetValue);
      }
      else
      {
        pConstraint->SetMotorState(JPH::EMotorState::Position);
        pConstraint->SetTargetPosition(m_fDriveTargetValue);
      }

      const float strength = (m_fDriveStrength == 0) ? FLT_MAX : m_fDriveStrength;

      pConstraint->GetMotorSettings().SetForceLimit(strength);
      pConstraint->GetMotorSettings().SetTorqueLimit(strength);
    }
  }

  if (pConstraint->GetBody2()->IsInBroadPhase())
  {
    // wake up the bodies that are attached to this constraint
    ezJoltWorldModule* pModule = GetWorld()->GetOrCreateModule<ezJoltWorldModule>();
    pModule->GetJoltSystem()->GetBodyInterface().ActivateBody(pConstraint->GetBody2()->GetID());
  }
}

void ezJoltSliderConstraintComponent::CreateContstraintType(JPH::Body* pBody0, JPH::Body* pBody1)
{
  const auto diff0 = pBody0->GetPosition() - pBody0->GetCenterOfMassPosition();
  const auto diff1 = pBody1->GetPosition() - pBody1->GetCenterOfMassPosition();

  JPH::SliderConstraintSettings opt;
  opt.mDrawConstraintSize = 0.1f;
  opt.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
  opt.mPoint1 = ezJoltConversionUtils::ToVec3(m_localFrameA.m_vPosition) + diff0;
  opt.mPoint2 = ezJoltConversionUtils::ToVec3(m_localFrameB.m_vPosition) + diff1;
  opt.mSliderAxis1 = ezJoltConversionUtils::ToVec3(m_localFrameA.m_qRotation * ezVec3(1, 0, 0));
  opt.mSliderAxis2 = ezJoltConversionUtils::ToVec3(m_localFrameB.m_qRotation * ezVec3(1, 0, 0));
  opt.mNormalAxis1 = ezJoltConversionUtils::ToVec3(m_localFrameA.m_qRotation * ezVec3(0, 1, 0));
  opt.mNormalAxis2 = ezJoltConversionUtils::ToVec3(m_localFrameB.m_qRotation * ezVec3(0, 1, 0));

  m_pConstraint = opt.Create(*pBody0, *pBody1);
}