#include <EditorPluginProcGen/EditorPluginProcGenPCH.h>

#include <EditorPluginProcGen/ProcGenGraphAsset/ProcGenGraphAsset.h>
#include <EditorPluginProcGen/ProcGenGraphAsset/ProcGenNodeManager.h>
#include <Foundation/CodeUtils/Expression/ExpressionByteCode.h>
#include <Foundation/CodeUtils/Expression/ExpressionCompiler.h>
#include <Foundation/IO/ChunkStream.h>
#include <Foundation/IO/StringDeduplicationContext.h>
#include <Foundation/Utilities/DGMLWriter.h>
#include <ToolsFoundation/Command/NodeCommands.h>
#include <ToolsFoundation/Serialization/DocumentObjectConverter.h>

namespace
{
  void DumpAST(const ezExpressionAST& ast, ezStringView sAssetName, ezStringView sOutputName)
  {
    ezDGMLGraph dgmlGraph;
    ast.PrintGraph(dgmlGraph);

    ezStringBuilder sFileName;
    sFileName.Format(":appdata/{0}_{1}_AST.dgml", sAssetName, sOutputName);

    ezDGMLGraphWriter dgmlGraphWriter;
    EZ_IGNORE_UNUSED(dgmlGraphWriter);
    if (dgmlGraphWriter.WriteGraphToFile(sFileName, dgmlGraph).Succeeded())
    {
      ezLog::Info("AST was dumped to: {0}", sFileName);
    }
    else
    {
      ezLog::Error("Failed to dump AST to: {0}", sFileName);
    }
  }

  static const char* s_szSphereAssetId = "{ a3ce5d3d-be5e-4bda-8820-b1ce3b3d33fd }";     // Base/Prefabs/Sphere.ezPrefab
  static const char* s_szBWGradientAssetId = "{ 3834b7d0-5a3f-140d-31d8-3a2bf48b09bd }"; // Base/Textures/BlackWhiteGradient.ezColorGradientAsset

} // namespace

////////////////////////////////////////////////////////////////

struct DocObjAndOutput
{
  EZ_DECLARE_POD_TYPE();

  const ezDocumentObject* m_pObject;
  const char* m_szOutputName;
};

template <>
struct ezHashHelper<DocObjAndOutput>
{
  EZ_ALWAYS_INLINE static ezUInt32 Hash(const DocObjAndOutput& value)
  {
    const ezUInt32 hashA = ezHashHelper<const void*>::Hash(value.m_pObject);
    const ezUInt32 hashB = ezHashHelper<const void*>::Hash(value.m_szOutputName);
    return ezHashingUtils::CombineHashValues32(hashA, hashB);
  }

  EZ_ALWAYS_INLINE static bool Equal(const DocObjAndOutput& a, const DocObjAndOutput& b)
  {
    return a.m_pObject == b.m_pObject && a.m_szOutputName == b.m_szOutputName;
  }
};

struct ezProcGenGraphAssetDocument::GenerateContext
{
  GenerateContext(const ezDocumentObjectManager* pManager)
    : m_ObjectWriter(&m_AbstractObjectGraph, pManager)
    , m_RttiConverter(&m_AbstractObjectGraph, &m_RttiConverterContext)
  {
  }

  ~GenerateContext()
  {
    for (auto it = m_DocObjToProcGenNodeTable.GetIterator(); it.IsValid(); ++it)
    {
      m_RttiConverterContext.DeleteObject(it.Key()->GetGuid());
    }
  }

  ezAbstractObjectGraph m_AbstractObjectGraph;
  ezDocumentObjectConverterWriter m_ObjectWriter;
  ezRttiConverterContext m_RttiConverterContext;
  ezRttiConverterReader m_RttiConverter;
  ezHashTable<const ezDocumentObject*, ezProcGenNodeBase*> m_DocObjToProcGenNodeTable;
  ezHashTable<DocObjAndOutput, ezExpressionAST::Node*> m_DocObjAndOutputToASTNodeTable;
  ezProcGenNodeBase::GraphContext m_GraphContext;
};

////////////////////////////////////////////////////////////////

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezProcGenGraphAssetDocument, 5, ezRTTINoAllocator)
EZ_END_DYNAMIC_REFLECTED_TYPE;

ezProcGenGraphAssetDocument::ezProcGenGraphAssetDocument(const char* szDocumentPath)
  : ezAssetDocument(szDocumentPath, EZ_DEFAULT_NEW(ezProcGenNodeManager), ezAssetDocEngineConnection::None)
{
}

void ezProcGenGraphAssetDocument::SetDebugPin(const ezPin* pDebugPin)
{
  m_pDebugPin = pDebugPin;

  if (m_pDebugPin != nullptr)
  {
    CreateDebugNode();
  }

  ezDocumentObjectPropertyEvent e;
  e.m_EventType = ezDocumentObjectPropertyEvent::Type::PropertySet;
  e.m_sProperty = "DebugPin";

  GetObjectManager()->m_PropertyEvents.Broadcast(e);
}

ezStatus ezProcGenGraphAssetDocument::WriteAsset(ezStreamWriter& stream, const ezPlatformProfile* pAssetProfile, bool bAllowDebug) const
{
  GenerateContext context(GetObjectManager());

  ezDynamicArray<const ezDocumentObject*> placementNodes;
  ezDynamicArray<const ezDocumentObject*> vertexColorNodes;
  GetAllOutputNodes(placementNodes, vertexColorNodes);

  const bool bDebug = bAllowDebug && (m_pDebugPin != nullptr);

  ezStringDeduplicationWriteContext stringDedupContext(stream);

  ezChunkStreamWriter chunk(stringDedupContext.Begin());
  chunk.BeginStream(1);

  ezExpressionCompiler compiler;

  auto WriteByteCode = [&](const ezDocumentObject* pOutputNode) {
    context.m_GraphContext.m_VolumeTagSetIndices.Clear();

    ezExpressionAST ast;
    GenerateExpressionAST(pOutputNode, "", context, ast);
    context.m_DocObjAndOutputToASTNodeTable.Clear();

    if (false)
    {
      ezStringBuilder sDocumentPath = GetDocumentPath();
      ezStringView sAssetName = sDocumentPath.GetFileNameAndExtension();
      ezStringView sOutputName = pOutputNode->GetTypeAccessor().GetValue("Name").ConvertTo<ezString>();

      DumpAST(ast, sAssetName, sOutputName);
    }

    ezExpressionByteCode byteCode;
    if (compiler.Compile(ast, byteCode).Failed())
    {
      return ezStatus("Compilation failed");
    }

    byteCode.Save(chunk);

    return ezStatus(EZ_SUCCESS);
  };

  {
    chunk.BeginChunk("PlacementOutputs", 6);

    if (!bDebug)
    {
      chunk << placementNodes.GetCount();

      for (auto pPlacementNode : placementNodes)
      {
        EZ_SUCCEED_OR_RETURN(WriteByteCode(pPlacementNode));

        ezProcGenNodeBase* pPGNode;
        EZ_VERIFY(context.m_DocObjToProcGenNodeTable.TryGetValue(pPlacementNode, pPGNode), "Implementation error");
        auto pPlacementOutput = ezStaticCast<ezProcGen_PlacementOutput*>(pPGNode);

        pPlacementOutput->m_VolumeTagSetIndices = context.m_GraphContext.m_VolumeTagSetIndices;
        pPlacementOutput->Save(chunk);
      }
    }
    else
    {
      ezUInt32 uiNumNodes = 1;
      chunk << uiNumNodes;

      context.m_GraphContext.m_VolumeTagSetIndices.Clear();

      ezExpressionAST ast;
      GenerateDebugExpressionAST(context, ast);
      context.m_DocObjAndOutputToASTNodeTable.Clear();

      ezExpressionByteCode byteCode;
      if (compiler.Compile(ast, byteCode).Failed())
      {
        return ezStatus("Debug Compilation failed");
      }

      byteCode.Save(chunk);

      m_pDebugNode->m_VolumeTagSetIndices = context.m_GraphContext.m_VolumeTagSetIndices;
      m_pDebugNode->Save(chunk);
    }

    chunk.EndChunk();
  }

  {
    chunk.BeginChunk("VertexColorOutputs", 2);

    chunk << vertexColorNodes.GetCount();

    for (auto pVertexColorNode : vertexColorNodes)
    {
      EZ_SUCCEED_OR_RETURN(WriteByteCode(pVertexColorNode));

      ezProcGenNodeBase* pPGNode;
      EZ_VERIFY(context.m_DocObjToProcGenNodeTable.TryGetValue(pVertexColorNode, pPGNode), "Implementation error");
      auto pVertexColorOutput = ezStaticCast<ezProcGen_VertexColorOutput*>(pPGNode);

      pVertexColorOutput->m_VolumeTagSetIndices = context.m_GraphContext.m_VolumeTagSetIndices;
      pVertexColorOutput->Save(chunk);
    }

    chunk.EndChunk();
  }

  {
    chunk.BeginChunk("SharedData", 1);

    context.m_GraphContext.m_SharedData.Save(chunk);

    chunk.EndChunk();
  }

  chunk.EndStream();
  EZ_SUCCEED_OR_RETURN(stringDedupContext.End());

  return ezStatus(EZ_SUCCESS);
}

void ezProcGenGraphAssetDocument::UpdateAssetDocumentInfo(ezAssetDocumentInfo* pInfo) const
{
  SUPER::UpdateAssetDocumentInfo(pInfo);

  if (m_pDebugPin == nullptr)
  {
    ezDynamicArray<const ezDocumentObject*> placementNodes;
    ezDynamicArray<const ezDocumentObject*> vertexColorNodes;
    GetAllOutputNodes(placementNodes, vertexColorNodes);

    for (auto pPlacementNode : placementNodes)
    {
      auto& typeAccessor = pPlacementNode->GetTypeAccessor();

      ezUInt32 uiNumObjects = typeAccessor.GetCount("Objects");
      for (ezUInt32 i = 0; i < uiNumObjects; ++i)
      {
        ezVariant prefab = typeAccessor.GetValue("Objects", i);
        if (prefab.IsA<ezString>())
        {
          pInfo->m_RuntimeDependencies.Insert(prefab.Get<ezString>());
        }
      }

      ezVariant colorGradient = typeAccessor.GetValue("ColorGradient");
      if (colorGradient.IsA<ezString>())
      {
        pInfo->m_RuntimeDependencies.Insert(colorGradient.Get<ezString>());
      }
    }
  }
  else
  {
    pInfo->m_RuntimeDependencies.Insert(s_szSphereAssetId);
    pInfo->m_RuntimeDependencies.Insert(s_szBWGradientAssetId);
  }
}

ezStatus ezProcGenGraphAssetDocument::InternalTransformAsset(ezStreamWriter& stream, const char* szOutputTag, const ezPlatformProfile* pAssetProfile, const ezAssetFileHeader& AssetHeader, ezBitflags<ezTransformFlags> transformFlags)
{
  EZ_ASSERT_DEV(ezStringUtils::IsNullOrEmpty(szOutputTag), "Additional output '{0}' not implemented!", szOutputTag);

  return WriteAsset(stream, pAssetProfile, false);
}

void ezProcGenGraphAssetDocument::GetSupportedMimeTypesForPasting(ezHybridArray<ezString, 4>& out_MimeTypes) const
{
  out_MimeTypes.PushBack("application/ezEditor.ProcGenGraph");
}

bool ezProcGenGraphAssetDocument::CopySelectedObjects(ezAbstractObjectGraph& out_objectGraph, ezStringBuilder& out_MimeType) const
{
  out_MimeType = "application/ezEditor.ProcGenGraph";

  const auto& selection = GetSelectionManager()->GetSelection();

  if (selection.IsEmpty())
    return false;

  const ezDocumentNodeManager* pManager = static_cast<const ezDocumentNodeManager*>(GetObjectManager());

  ezDocumentObjectConverterWriter writer(&out_objectGraph, pManager);

  for (const ezDocumentObject* pNode : selection)
  {
    // objects are required to be named root but this is not enforced or obvious by the interface.
    writer.AddObjectToGraph(pNode, "root");
  }

  pManager->AttachMetaDataBeforeSaving(out_objectGraph);

  return true;
}

bool ezProcGenGraphAssetDocument::Paste(const ezArrayPtr<PasteInfo>& info, const ezAbstractObjectGraph& objectGraph, bool bAllowPickedPosition, const char* szMimeType)
{
  bool bAddedAll = true;

  ezDeque<const ezDocumentObject*> AddedNodes;

  for (const PasteInfo& pi : info)
  {
    // only add nodes that are allowed to be added
    if (GetObjectManager()->CanAdd(pi.m_pObject->GetTypeAccessor().GetType(), nullptr, "Children", pi.m_Index).m_Result.Succeeded())
    {
      AddedNodes.PushBack(pi.m_pObject);
      GetObjectManager()->AddObject(pi.m_pObject, nullptr, "Children", pi.m_Index);
    }
    else
    {
      bAddedAll = false;
    }
  }

  m_DocumentObjectMetaData->RestoreMetaDataFromAbstractGraph(objectGraph);

  RestoreMetaDataAfterLoading(objectGraph, true);

  if (!AddedNodes.IsEmpty() && bAllowPickedPosition)
  {
    ezDocumentNodeManager* pManager = static_cast<ezDocumentNodeManager*>(GetObjectManager());

    ezVec2 vAvgPos(0);
    for (const ezDocumentObject* pNode : AddedNodes)
    {
      vAvgPos += pManager->GetNodePos(pNode);
    }

    vAvgPos /= AddedNodes.GetCount();

    const ezVec2 vMoveNode = -vAvgPos + ezQtNodeScene::GetLastMouseInteractionPos();

    for (const ezDocumentObject* pNode : AddedNodes)
    {
      ezMoveNodeCommand move;
      move.m_Object = pNode->GetGuid();
      move.m_NewPos = pManager->GetNodePos(pNode) + vMoveNode;
      GetCommandHistory()->AddCommand(move);
    }

    if (!bAddedAll)
    {
      ezLog::Info("[EditorStatus]Not all nodes were allowed to be added to the document");
    }
  }

  GetSelectionManager()->SetSelection(AddedNodes);
  return true;
}

void ezProcGenGraphAssetDocument::AttachMetaDataBeforeSaving(ezAbstractObjectGraph& graph) const
{
  SUPER::AttachMetaDataBeforeSaving(graph);
  const ezDocumentNodeManager* pManager = static_cast<const ezDocumentNodeManager*>(GetObjectManager());
  pManager->AttachMetaDataBeforeSaving(graph);
}

void ezProcGenGraphAssetDocument::RestoreMetaDataAfterLoading(const ezAbstractObjectGraph& graph, bool bUndoable)
{
  SUPER::RestoreMetaDataAfterLoading(graph, bUndoable);
  ezDocumentNodeManager* pManager = static_cast<ezDocumentNodeManager*>(GetObjectManager());
  pManager->RestoreMetaDataAfterLoading(graph, bUndoable);
}

void ezProcGenGraphAssetDocument::GetAllOutputNodes(ezDynamicArray<const ezDocumentObject*>& placementNodes, ezDynamicArray<const ezDocumentObject*>& vertexColorNodes) const
{
  const ezRTTI* pPlacementOutputRtti = ezGetStaticRTTI<ezProcGen_PlacementOutput>();
  const ezRTTI* pVertexColorOutputRtti = ezGetStaticRTTI<ezProcGen_VertexColorOutput>();

  placementNodes.Clear();
  vertexColorNodes.Clear();

  const auto& children = GetObjectManager()->GetRootObject()->GetChildren();
  for (const ezDocumentObject* pObject : children)
  {
    if (pObject->GetTypeAccessor().GetValue("Active").ConvertTo<bool>())
    {
      const ezRTTI* pRtti = pObject->GetTypeAccessor().GetType();
      if (pRtti->IsDerivedFrom(pPlacementOutputRtti))
      {
        placementNodes.PushBack(pObject);
      }
      else if (pRtti->IsDerivedFrom(pVertexColorOutputRtti))
      {
        vertexColorNodes.PushBack(pObject);
      }
    }
  }
}

void ezProcGenGraphAssetDocument::InternalGetMetaDataHash(const ezDocumentObject* pObject, ezUInt64& inout_uiHash) const
{
  const ezDocumentNodeManager* pManager = static_cast<const ezDocumentNodeManager*>(GetObjectManager());
  if (pManager->IsNode(pObject))
  {
    auto outputs = pManager->GetOutputPins(pObject);
    for (const ezPin* pPinSource : outputs)
    {
      auto inputs = pPinSource->GetConnections();
      for (const ezConnection* pConnection : inputs)
      {
        const ezPin* pPinTarget = pConnection->GetTargetPin();

        inout_uiHash = ezHashingUtils::xxHash64(&pPinSource->GetParent()->GetGuid(), sizeof(ezUuid), inout_uiHash);
        inout_uiHash = ezHashingUtils::xxHash64(&pPinTarget->GetParent()->GetGuid(), sizeof(ezUuid), inout_uiHash);
        inout_uiHash = ezHashingUtils::xxHash64(pPinSource->GetName(), ezStringUtils::GetStringElementCount(pPinSource->GetName()), inout_uiHash);
        inout_uiHash = ezHashingUtils::xxHash64(pPinTarget->GetName(), ezStringUtils::GetStringElementCount(pPinTarget->GetName()), inout_uiHash);
      }
    }
  }
}

ezExpressionAST::Node* ezProcGenGraphAssetDocument::GenerateExpressionAST(const ezDocumentObject* outputNode, const char* szOutputName, GenerateContext& context, ezExpressionAST& out_Ast) const
{
  const ezDocumentNodeManager* pManager = static_cast<const ezDocumentNodeManager*>(GetObjectManager());

  const ezArrayPtr<ezPin* const> inputPins = pManager->GetInputPins(outputNode);

  ezHybridArray<ezExpressionAST::Node*, 8> inputAstNodes;
  inputAstNodes.SetCount(inputPins.GetCount());

  for (ezUInt32 i = 0; i < inputPins.GetCount(); ++i)
  {
    auto connections = inputPins[i]->GetConnections();
    EZ_ASSERT_DEBUG(connections.GetCount() <= 1, "Input pin has {0} connections", connections.GetCount());

    if (connections.IsEmpty())
      continue;

    const ezPin* pPinSource = connections[0]->GetSourcePin();
    EZ_ASSERT_DEBUG(pPinSource != nullptr, "Invalid connection");

    DocObjAndOutput key = {pPinSource->GetParent(), pPinSource->GetName()};
    ezExpressionAST::Node* astNode;
    if (!context.m_DocObjAndOutputToASTNodeTable.TryGetValue(key, astNode))
    {
      // recursively generate all dependent code
      astNode = GenerateExpressionAST(pPinSource->GetParent(), pPinSource->GetName(), context, out_Ast);

      context.m_DocObjAndOutputToASTNodeTable.Insert(key, astNode);
    }

    inputAstNodes[i] = astNode;
  }

  ezProcGenNodeBase* cachedPGNode;
  if (!context.m_DocObjToProcGenNodeTable.TryGetValue(outputNode, cachedPGNode))
  {
    ezAbstractObjectNode* pAbstractNode = context.m_ObjectWriter.AddObjectToGraph(outputNode);
    cachedPGNode = static_cast<ezProcGenNodeBase*>(context.m_RttiConverter.CreateObjectFromNode(pAbstractNode));

    context.m_DocObjToProcGenNodeTable.Insert(outputNode, cachedPGNode);
  }

  return cachedPGNode->GenerateExpressionASTNode(ezTempHashedString(szOutputName), inputAstNodes, out_Ast, context.m_GraphContext);
}

ezExpressionAST::Node* ezProcGenGraphAssetDocument::GenerateDebugExpressionAST(GenerateContext& context, ezExpressionAST& out_Ast) const
{
  EZ_ASSERT_DEV(m_pDebugPin != nullptr, "");

  const ezPin* pPinSource = m_pDebugPin;
  if (pPinSource->GetType() == ezPin::Type::Input)
  {
    auto connections = pPinSource->GetConnections();
    EZ_ASSERT_DEBUG(connections.GetCount() <= 1, "Input pin has {0} connections", connections.GetCount());

    if (connections.IsEmpty())
      return nullptr;

    pPinSource = connections[0]->GetSourcePin();
    EZ_ASSERT_DEBUG(pPinSource != nullptr, "Invalid connection");
  }

  ezHybridArray<ezExpressionAST::Node*, 8> inputAstNodes;
  inputAstNodes.SetCount(4); // placement output node has 4 inputs

  // Recursively generate all dependent code and pretend it is connected to the color index input of the debug placement output node.
  inputAstNodes[2] = GenerateExpressionAST(pPinSource->GetParent(), pPinSource->GetName(), context, out_Ast);

  return m_pDebugNode->GenerateExpressionASTNode("", inputAstNodes, out_Ast, context.m_GraphContext);
}

void ezProcGenGraphAssetDocument::DumpSelectedOutput(bool bAst, bool bDisassembly) const
{
  const ezDocumentObject* pSelectedNode = nullptr;

  auto selection = GetSelectionManager()->GetSelection();
  if (!selection.IsEmpty())
  {
    pSelectedNode = selection[0];
    if (!pSelectedNode->GetType()->IsDerivedFrom(ezGetStaticRTTI<ezProcGenOutput>()))
    {
      pSelectedNode = nullptr;
    }
  }

  if (pSelectedNode == nullptr)
  {
    ezLog::Error("No valid output node selected.");
    return;
  }

  GenerateContext context(GetObjectManager());
  ezExpressionAST ast;
  GenerateExpressionAST(pSelectedNode, "", context, ast);

  ezStringBuilder sDocumentPath = GetDocumentPath();
  ezStringView sAssetName = sDocumentPath.GetFileNameAndExtension();
  ezStringView sOutputName = pSelectedNode->GetTypeAccessor().GetValue("Name").ConvertTo<ezString>();

  if (bAst)
  {
    DumpAST(ast, sAssetName, sOutputName);
  }

  if (bDisassembly)
  {
    ezExpressionByteCode byteCode;

    ezExpressionCompiler compiler;
    if (compiler.Compile(ast, byteCode).Succeeded())
    {
      ezStringBuilder sDisassembly;
      byteCode.Disassemble(sDisassembly);

      ezStringBuilder sFileName;
      sFileName.Format(":appdata/{0}_{1}_ByteCode.txt", sAssetName, sOutputName);

      ezFileWriter fileWriter;
      if (fileWriter.Open(sFileName).Succeeded())
      {
        fileWriter.WriteBytes(sDisassembly.GetData(), sDisassembly.GetElementCount()).IgnoreResult();

        ezLog::Info("Disassembly was dumped to: {0}", sFileName);
      }
      else
      {
        ezLog::Error("Failed to dump Disassembly to: {0}", sFileName);
      }
    }
    else
    {
      ezLog::Error("Compiling expression failed");
    }
  }
}

void ezProcGenGraphAssetDocument::CreateDebugNode()
{
  if (m_pDebugNode != nullptr)
    return;

  m_pDebugNode = EZ_DEFAULT_NEW(ezProcGen_PlacementOutput);
  m_pDebugNode->m_sName = "Debug";
  m_pDebugNode->m_ObjectsToPlace.PushBack(s_szSphereAssetId);
  m_pDebugNode->m_sColorGradient = s_szBWGradientAssetId;
}
