#pragma once

#include <Core/World/ComponentManager.h>
#include <GameEngine/Physics/RopeSimulator.h>
#include <GameEngineDLL.h>

//////////////////////////////////////////////////////////////////////////

class EZ_GAMEENGINE_DLL ezFakeRopeComponentManager : public ezComponentManager<class ezFakeRopeComponent, ezBlockStorageType::Compact>
{
public:
  ezFakeRopeComponentManager(ezWorld* pWorld);
  ~ezFakeRopeComponentManager();

  virtual void Initialize() override;

private:
  void Update(const ezWorldModule::UpdateContext& context);
};

//////////////////////////////////////////////////////////////////////////

class EZ_GAMEENGINE_DLL ezFakeRopeComponent : public ezComponent
{
  EZ_DECLARE_COMPONENT_TYPE(ezFakeRopeComponent, ezComponent, ezFakeRopeComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // ezComponent

public:
  virtual void SerializeComponent(ezWorldWriter& stream) const override;
  virtual void DeserializeComponent(ezWorldReader& stream) override;

  virtual void OnActivated() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // ezFakeRopeComponent

public:
  ezFakeRopeComponent();
  ~ezFakeRopeComponent();

  ezUInt16 m_uiPieces = 16; // [ property ]

  void SetAnchorReference(const char* szReference); // [ property ]
  void SetAnchor(ezGameObjectHandle hActor);

  void SetSlack(float val);
  float GetSlack() const { return m_fSlack; }

  void SetAttachToOrigin(bool val);
  bool GetAttachToOrigin() const;
  void SetAttachToAnchor(bool val);
  bool GetAttachToAnchor() const;

  float m_fSlack = 0.0f;
  float m_fDamping = 0.1f;

private:
  ezResult ConfigureRopeSimulator();
  void SendCurrentPose();
  void SendPreviewPose();
  void RuntimeUpdate();

  ezGameObjectHandle m_hAnchor;

  ezUInt32 m_uiPreviewHash = 0;

  ezRopeSimulator m_RopeSim;

private:
  const char* DummyGetter() const { return nullptr; }
};