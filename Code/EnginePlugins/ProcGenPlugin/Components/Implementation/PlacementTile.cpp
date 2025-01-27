#include <ProcGenPlugin/ProcGenPluginPCH.h>

#include <Core/Messages/SetColorMessage.h>
#include <Core/Prefabs/PrefabReferenceComponent.h>
#include <Core/World/World.h>
#include <Foundation/SimdMath/SimdConversion.h>
#include <ProcGenPlugin/Components/Implementation/PlacementTile.h>
#include <ProcGenPlugin/Tasks/PlacementData.h>

using namespace ezProcGenInternal;

PlacementTile::PlacementTile()
  : m_pOutput(nullptr)
  , m_State(State::Invalid)
{
}

PlacementTile::PlacementTile(PlacementTile&& other)
{
  m_Desc = other.m_Desc;
  m_pOutput = other.m_pOutput;

  m_State = other.m_State;
  other.m_State = State::Invalid;

  m_PlacedObjects = std::move(other.m_PlacedObjects);
}

PlacementTile::~PlacementTile()
{
  EZ_ASSERT_DEV(m_State == State::Invalid, "Implementation error");
}

void PlacementTile::Initialize(const PlacementTileDesc& desc, ezSharedPtr<const PlacementOutput>& pOutput)
{
  m_Desc = desc;
  m_pOutput = pOutput;

  m_State = State::Initialized;
}

void PlacementTile::Deinitialize(ezWorld& world)
{
  for (auto hObject : m_PlacedObjects)
  {
    world.DeleteObjectDelayed(hObject);
  }
  m_PlacedObjects.Clear();

  m_Desc.m_hComponent.Invalidate();
  m_pOutput = nullptr;
  m_State = State::Invalid;
}

bool PlacementTile::IsValid() const
{
  return !m_Desc.m_hComponent.IsInvalidated() && m_pOutput != nullptr;
}

const PlacementTileDesc& PlacementTile::GetDesc() const
{
  return m_Desc;
}

const PlacementOutput* PlacementTile::GetOutput() const
{
  return m_pOutput;
}

ezArrayPtr<const ezGameObjectHandle> PlacementTile::GetPlacedObjects() const
{
  return m_PlacedObjects;
}

ezBoundingBox PlacementTile::GetBoundingBox() const
{
  return m_Desc.GetBoundingBox();
}

ezColor PlacementTile::GetDebugColor() const
{
  switch (m_State)
  {
    case State::Initialized:
      return ezColor::Orange;
    case State::Scheduled:
      return ezColor::Yellow;
    case State::Finished:
      return ezColor::Green;
    default:
      return ezColor::DarkRed;
  }
}

void PlacementTile::PreparePlacementData(const ezWorld* pWorld, const ezPhysicsWorldModuleInterface* pPhysicsModule, PlacementData& placementData)
{
  placementData.m_pPhysicsModule = pPhysicsModule;
  placementData.m_pWorld = pWorld;
  placementData.m_pOutput = m_pOutput;
  placementData.m_iTileSeed = (m_Desc.m_iPosX << 11) ^ (m_Desc.m_iPosY * 17);
  placementData.m_TileBoundingBox = GetBoundingBox();
  placementData.m_GlobalToLocalBoxTransforms = m_Desc.m_GlobalToLocalBoxTransforms;

  m_State = State::Scheduled;
}

ezUInt32 PlacementTile::PlaceObjects(ezWorld& world, ezArrayPtr<const PlacementTransform> objectTransforms)
{
  EZ_PROFILE_SCOPE("PlacementTile::PlaceObjects");

  ezGameObjectDesc desc;
  auto& objectsToPlace = m_pOutput->m_ObjectsToPlace;

  ezHybridArray<ezPrefabResource*, 4> prefabs;
  prefabs.SetCount(objectsToPlace.GetCount());



  for (auto& objectTransform : objectTransforms)
  {
    const ezUInt32 uiObjectIndex = objectTransform.m_uiObjectIndex;
    ezPrefabResource* pPrefab = prefabs[uiObjectIndex];

    if (pPrefab == nullptr)
    {
      pPrefab = ezResourceManager::BeginAcquireResource(objectsToPlace[uiObjectIndex], ezResourceAcquireMode::BlockTillLoaded);
      prefabs[uiObjectIndex] = pPrefab;
    }

    ezTransform transform = ezSimdConversion::ToTransform(objectTransform.m_Transform);
    ezHybridArray<ezGameObject*, 8> rootObjects;

    ezPrefabInstantiationOptions options;
    options.m_pCreatedRootObjectsOut = &rootObjects;

    pPrefab->InstantiatePrefab(world, transform, options);

    // only send the color message, if we actually have a custom color
    if (objectTransform.m_uiSetColor != 0)
    {
      for (auto pRootObject : rootObjects)
      {
        // Set the color
        ezMsgSetColor msg;
        msg.m_Color = objectTransform.m_ObjectColor;
        pRootObject->PostMessageRecursive(msg, ezTime::Zero(), ezObjectMsgQueueType::AfterInitialized);
      }
    }

    for (auto pRootObject : rootObjects)
    {
      m_PlacedObjects.PushBack(pRootObject->GetHandle());
    }
  }

  for (auto pPrefab : prefabs)
  {
    if (pPrefab != nullptr)
    {
      ezResourceManager::EndAcquireResource(pPrefab);
    }
  }

  m_State = State::Finished;

  return m_PlacedObjects.GetCount();
}
