#include <Foundation/FoundationPCH.h>

#include <Foundation/CodeUtils/Expression/ExpressionByteCode.h>
#include <Foundation/CodeUtils/Expression/ExpressionCompiler.h>

namespace
{
  static ezExpressionByteCode::OpCode::Enum NodeTypeToOpCode(ezExpressionAST::NodeType::Enum nodeType)
  {
    switch (nodeType)
    {
      case ezExpressionAST::NodeType::Absolute:
        return ezExpressionByteCode::OpCode::Abs_R;
      case ezExpressionAST::NodeType::Sqrt:
        return ezExpressionByteCode::OpCode::Sqrt_R;

      case ezExpressionAST::NodeType::Sin:
        return ezExpressionByteCode::OpCode::Sin_R;
      case ezExpressionAST::NodeType::Cos:
        return ezExpressionByteCode::OpCode::Cos_R;
      case ezExpressionAST::NodeType::Tan:
        return ezExpressionByteCode::OpCode::Tan_R;

      case ezExpressionAST::NodeType::ASin:
        return ezExpressionByteCode::OpCode::ASin_R;
      case ezExpressionAST::NodeType::ACos:
        return ezExpressionByteCode::OpCode::ACos_R;
      case ezExpressionAST::NodeType::ATan:
        return ezExpressionByteCode::OpCode::ATan_R;

      case ezExpressionAST::NodeType::Add:
        return ezExpressionByteCode::OpCode::Add_RR;
      case ezExpressionAST::NodeType::Subtract:
        return ezExpressionByteCode::OpCode::Sub_RR;
      case ezExpressionAST::NodeType::Multiply:
        return ezExpressionByteCode::OpCode::Mul_RR;
      case ezExpressionAST::NodeType::Divide:
        return ezExpressionByteCode::OpCode::Div_RR;
      case ezExpressionAST::NodeType::Min:
        return ezExpressionByteCode::OpCode::Min_RR;
      case ezExpressionAST::NodeType::Max:
        return ezExpressionByteCode::OpCode::Max_RR;
      default:
        EZ_ASSERT_NOT_IMPLEMENTED;
        return ezExpressionByteCode::OpCode::Nop;
    }
  }
} // namespace

ezExpressionCompiler::ezExpressionCompiler() = default;
ezExpressionCompiler::~ezExpressionCompiler() = default;

ezResult ezExpressionCompiler::Compile(ezExpressionAST& ast, ezExpressionByteCode& out_byteCode)
{
  out_byteCode.Clear();

  EZ_SUCCEED_OR_RETURN(TransformAndOptimizeAST(ast));
  EZ_SUCCEED_OR_RETURN(BuildNodeInstructions(ast));
  EZ_SUCCEED_OR_RETURN(UpdateRegisterLifetime(ast));
  EZ_SUCCEED_OR_RETURN(AssignRegisters());
  EZ_SUCCEED_OR_RETURN(GenerateByteCode(ast, out_byteCode));

  return EZ_SUCCESS;
}

ezResult ezExpressionCompiler::TransformAndOptimizeAST(ezExpressionAST& ast)
{
  EZ_SUCCEED_OR_RETURN(TransformASTPreOrder(ast, ezMakeDelegate(&ezExpressionAST::ReplaceUnsupportedInstructions, &ast)));
  EZ_SUCCEED_OR_RETURN(TransformASTPostOrder(ast, ezMakeDelegate(&ezExpressionAST::FoldConstants, &ast)));

  return EZ_SUCCESS;
}

ezResult ezExpressionCompiler::BuildNodeInstructions(const ezExpressionAST& ast)
{
  m_NodeStack.Clear();
  m_NodeInstructions.Clear();
  auto& nodeStackTemp = m_NodeInstructions;

  // Build node instruction order aka post order tree traversal
  for (ezExpressionAST::Node* pOutputNode : ast.m_OutputNodes)
  {
    if (pOutputNode == nullptr)
      continue;

    EZ_ASSERT_DEV(nodeStackTemp.IsEmpty(), "Implementation error");

    nodeStackTemp.PushBack(pOutputNode);

    while (!nodeStackTemp.IsEmpty())
    {
      auto pCurrentNode = nodeStackTemp.PeekBack();
      nodeStackTemp.PopBack();

      if (pCurrentNode == nullptr)
      {
        return EZ_FAILURE;
      }

      m_NodeStack.PushBack(pCurrentNode);

      if (ezExpressionAST::NodeType::IsBinary(pCurrentNode->m_Type))
      {
        // Do not push the left operand if it is a constant, we don't want a separate mov instruction for it
        // since all binary operators can take a constant as left operand in place.
        auto pBinary = static_cast<const ezExpressionAST::BinaryOperator*>(pCurrentNode);
        bool bLeftIsConstant = ezExpressionAST::NodeType::IsConstant(pBinary->m_pLeftOperand->m_Type);
        if (!bLeftIsConstant)
        {
          nodeStackTemp.PushBack(pBinary->m_pLeftOperand);
        }

        nodeStackTemp.PushBack(pBinary->m_pRightOperand);
      }
      else
      {
        auto children = ezExpressionAST::GetChildren(pCurrentNode);
        for (auto pChild : children)
        {
          nodeStackTemp.PushBack(pChild);
        }
      }
    }
  }

  if (m_NodeStack.IsEmpty())
  {
    // Nothing to compile
    return EZ_FAILURE;
  }

  EZ_ASSERT_DEV(m_NodeInstructions.IsEmpty(), "Implementation error");

  m_NodeToRegisterIndex.Clear();
  m_LiveIntervals.Clear();
  ezUInt32 uiNextRegisterIndex = 0;

  // De-duplicate nodes, build final instruction list and assign virtual register indices. Also determine their lifetime start.
  while (!m_NodeStack.IsEmpty())
  {
    auto pCurrentNode = m_NodeStack.PeekBack();
    m_NodeStack.PopBack();

    if (!m_NodeToRegisterIndex.Contains(pCurrentNode))
    {
      m_NodeInstructions.PushBack(pCurrentNode);

      m_NodeToRegisterIndex.Insert(pCurrentNode, uiNextRegisterIndex);
      ++uiNextRegisterIndex;

      ezUInt32 uiCurrentInstructionIndex = m_NodeInstructions.GetCount() - 1;
      m_LiveIntervals.PushBack({uiCurrentInstructionIndex, uiCurrentInstructionIndex, pCurrentNode});
      EZ_ASSERT_DEV(m_LiveIntervals.GetCount() == uiNextRegisterIndex, "Implementation error");
    }
  }

  return EZ_SUCCESS;
}

ezResult ezExpressionCompiler::UpdateRegisterLifetime(const ezExpressionAST& ast)
{
  ezUInt32 uiNumInstructions = m_NodeInstructions.GetCount();
  for (ezUInt32 uiInstructionIndex = 0; uiInstructionIndex < uiNumInstructions; ++uiInstructionIndex)
  {
    auto pCurrentNode = m_NodeInstructions[uiInstructionIndex];

    auto children = ezExpressionAST::GetChildren(pCurrentNode);
    for (auto pChild : children)
    {
      ezUInt32 uiRegisterIndex = ezInvalidIndex;
      if (m_NodeToRegisterIndex.TryGetValue(pChild, uiRegisterIndex))
      {
        auto& liveRegister = m_LiveIntervals[uiRegisterIndex];

        EZ_ASSERT_DEV(liveRegister.m_uiEnd <= uiInstructionIndex, "Implementation error");
        liveRegister.m_uiEnd = uiInstructionIndex;
      }
      else
      {
        EZ_ASSERT_DEV(ezExpressionAST::NodeType::IsConstant(pChild->m_Type), "Must have a valid register for nodes that are not constants");
      }
    }
  }

  return EZ_SUCCESS;
}

ezResult ezExpressionCompiler::AssignRegisters()
{
  // This is an implementation of the linear scan register allocation algorithm without spilling
  // https://www2.seas.gwu.edu/~hchoi/teaching/cs160d/linearscan.pdf

  // Sort register lifetime by start index
  m_LiveIntervals.Sort([](const LiveInterval& a, const LiveInterval& b) { return a.m_uiStart < b.m_uiStart; });

  // Assign registers
  ezHybridArray<LiveInterval, 64> activeIntervals;
  ezHybridArray<ezUInt32, 64> freeRegisters;

  for (auto& liveInterval : m_LiveIntervals)
  {
    // Expire old intervals
    for (ezUInt32 uiActiveIndex = activeIntervals.GetCount(); uiActiveIndex-- > 0;)
    {
      auto& activeInterval = activeIntervals[uiActiveIndex];
      if (activeInterval.m_uiEnd <= liveInterval.m_uiStart)
      {
        ezUInt32 uiRegisterIndex = m_NodeToRegisterIndex[activeInterval.m_pNode];
        freeRegisters.PushBack(uiRegisterIndex);

        activeIntervals.RemoveAtAndCopy(uiActiveIndex);
      }
    }

    // Allocate register
    ezUInt32 uiNewRegister = 0;
    if (!freeRegisters.IsEmpty())
    {
      uiNewRegister = freeRegisters.PeekBack();
      freeRegisters.PopBack();
    }
    else
    {
      uiNewRegister = activeIntervals.GetCount();
    }
    m_NodeToRegisterIndex[liveInterval.m_pNode] = uiNewRegister;

    activeIntervals.PushBack(liveInterval);
  }

  return EZ_SUCCESS;
}

ezResult ezExpressionCompiler::GenerateByteCode(const ezExpressionAST& ast, ezExpressionByteCode& out_byteCode)
{
  auto& byteCode = out_byteCode.m_ByteCode;

  ezUInt32 uiMaxRegisterIndex = 0;

  m_InputToIndex.Clear();
  m_OutputToIndex.Clear();
  m_FunctionToIndex.Clear();
  ezUInt32 uiNextInputIndex = 0;
  ezUInt32 uiNextOutputIndex = 0;
  ezUInt32 uiNextFunctionIndex = 0;

  for (auto pCurrentNode : m_NodeInstructions)
  {
    ezUInt32 uiTargetRegister = m_NodeToRegisterIndex[pCurrentNode];
    uiMaxRegisterIndex = ezMath::Max(uiMaxRegisterIndex, uiTargetRegister);

    ezExpressionAST::NodeType::Enum nodeType = pCurrentNode->m_Type;
    if (ezExpressionAST::NodeType::IsUnary(nodeType))
    {
      auto pUnary = static_cast<const ezExpressionAST::UnaryOperator*>(pCurrentNode);
      auto opCode = NodeTypeToOpCode(nodeType);
      if (opCode == ezExpressionByteCode::OpCode::Nop)
        return EZ_FAILURE;

      byteCode.PushBack(opCode);
      byteCode.PushBack(uiTargetRegister);
      byteCode.PushBack(m_NodeToRegisterIndex[pUnary->m_pOperand]);
    }
    else if (ezExpressionAST::NodeType::IsBinary(nodeType))
    {
      auto pBinary = static_cast<const ezExpressionAST::BinaryOperator*>(pCurrentNode);
      bool bLeftIsConstant = ezExpressionAST::NodeType::IsConstant(pBinary->m_pLeftOperand->m_Type);
      auto opCode = NodeTypeToOpCode(nodeType);
      if (opCode == ezExpressionByteCode::OpCode::Nop)
        return EZ_FAILURE;

      ezUInt32 uiConstantValue = 0;

      if (bLeftIsConstant)
      {
        // Op code for constant register combination is always +1 of regular op code.
        opCode = static_cast<ezExpressionByteCode::OpCode::Enum>(opCode + 1);

        auto pConstant = static_cast<const ezExpressionAST::Constant*>(pBinary->m_pLeftOperand);
        uiConstantValue = *reinterpret_cast<const ezUInt32*>(&pConstant->m_Value.Get<float>());
      }

      byteCode.PushBack(opCode);
      byteCode.PushBack(uiTargetRegister);
      byteCode.PushBack(bLeftIsConstant ? uiConstantValue : m_NodeToRegisterIndex[pBinary->m_pLeftOperand]);
      byteCode.PushBack(m_NodeToRegisterIndex[pBinary->m_pRightOperand]);
    }
    else if (ezExpressionAST::NodeType::IsConstant(nodeType))
    {
      auto pConstant = static_cast<const ezExpressionAST::Constant*>(pCurrentNode);
      EZ_ASSERT_DEV(pConstant->m_Value.IsA<float>(), "Only floats are supported");
      float fValue = pConstant->m_Value.Get<float>();

      byteCode.PushBack(ezExpressionByteCode::OpCode::Mov_C);
      byteCode.PushBack(uiTargetRegister);
      byteCode.PushBack(*reinterpret_cast<ezUInt32*>(&fValue));
    }
    else if (ezExpressionAST::NodeType::IsInput(nodeType))
    {
      const ezHashedString& sName = static_cast<const ezExpressionAST::Input*>(pCurrentNode)->m_sName;
      ezUInt32 uiInputIndex = 0;
      if (!m_InputToIndex.TryGetValue(sName, uiInputIndex))
      {
        uiInputIndex = uiNextInputIndex;
        m_InputToIndex.Insert(sName, uiInputIndex);

        ++uiNextInputIndex;
      }

      byteCode.PushBack(ezExpressionByteCode::OpCode::Load);
      byteCode.PushBack(uiTargetRegister);
      byteCode.PushBack(uiInputIndex);
    }
    else if (ezExpressionAST::NodeType::IsOutput(nodeType))
    {
      auto pOutput = static_cast<const ezExpressionAST::Output*>(pCurrentNode);
      const ezHashedString& sName = pOutput->m_sName;
      ezUInt32 uiOutputIndex = 0;
      if (!m_OutputToIndex.TryGetValue(sName, uiOutputIndex))
      {
        uiOutputIndex = uiNextOutputIndex;
        m_OutputToIndex.Insert(sName, uiOutputIndex);

        ++uiNextOutputIndex;
      }

      byteCode.PushBack(ezExpressionByteCode::OpCode::Store);
      byteCode.PushBack(uiOutputIndex);
      byteCode.PushBack(m_NodeToRegisterIndex[pOutput->m_pExpression]);
    }
    else if (nodeType == ezExpressionAST::NodeType::FunctionCall)
    {
      auto pFunctionCall = static_cast<const ezExpressionAST::FunctionCall*>(pCurrentNode);
      const ezHashedString& sName = pFunctionCall->m_sName;
      ezUInt32 uiFunctionIndex = 0;
      if (!m_FunctionToIndex.TryGetValue(sName, uiFunctionIndex))
      {
        uiFunctionIndex = uiNextFunctionIndex;
        m_FunctionToIndex.Insert(sName, uiFunctionIndex);

        ++uiNextFunctionIndex;
      }

      byteCode.PushBack(ezExpressionByteCode::OpCode::Call);
      byteCode.PushBack(uiFunctionIndex);
      byteCode.PushBack(uiTargetRegister);

      byteCode.PushBack(pFunctionCall->m_Arguments.GetCount());
      for (auto pArg : pFunctionCall->m_Arguments)
      {
        ezUInt32 uiArgRegister = m_NodeToRegisterIndex[pArg];
        byteCode.PushBack(uiArgRegister);
      }
    }
    else
    {
      EZ_ASSERT_NOT_IMPLEMENTED;
    }
  }

  out_byteCode.m_uiNumInstructions = m_NodeInstructions.GetCount();
  out_byteCode.m_uiNumTempRegisters = uiMaxRegisterIndex + 1;

  out_byteCode.m_Inputs.SetCount(m_InputToIndex.GetCount());
  for (auto it = m_InputToIndex.GetIterator(); it.IsValid(); ++it)
  {
    out_byteCode.m_Inputs[it.Value()] = it.Key();
  }

  out_byteCode.m_Outputs.SetCount(m_OutputToIndex.GetCount());
  for (auto it = m_OutputToIndex.GetIterator(); it.IsValid(); ++it)
  {
    out_byteCode.m_Outputs[it.Value()] = it.Key();
  }

  out_byteCode.m_Functions.SetCount(m_FunctionToIndex.GetCount());
  for (auto it = m_FunctionToIndex.GetIterator(); it.IsValid(); ++it)
  {
    out_byteCode.m_Functions[it.Value()] = it.Key();
  }

  return EZ_SUCCESS;
}

ezResult ezExpressionCompiler::TransformASTPreOrder(ezExpressionAST& ast, TransformFunc func)
{
  m_NodeStack.Clear();
  m_TransformCache.Clear();

  for (ezExpressionAST::Node* pOutputNode : ast.m_OutputNodes)
  {
    if (pOutputNode == nullptr)
      continue;

    m_NodeStack.PushBack(pOutputNode);

    while (!m_NodeStack.IsEmpty())
    {
      auto pParent = m_NodeStack.PeekBack();
      m_NodeStack.PopBack();

      auto children = ezExpressionAST::GetChildren(pParent);
      for (auto& pChild : children)
      {
        if (pChild == nullptr)
          continue;

        ezExpressionAST::Node* pNewChild = nullptr;
        if (m_TransformCache.TryGetValue(pChild, pNewChild) == false)
        {
          pNewChild = func(pChild);
          m_TransformCache.Insert(pChild, pNewChild);
        }

        pChild = pNewChild;
        m_NodeStack.PushBack(pChild);
      }
    }
  }

  return EZ_SUCCESS;
}

ezResult ezExpressionCompiler::TransformASTPostOrder(ezExpressionAST& ast, TransformFunc func)
{
  m_NodeStack.Clear();
  m_NodeInstructions.Clear();
  auto& nodeStackTemp = m_NodeInstructions;

  for (ezExpressionAST::Node* pOutputNode : ast.m_OutputNodes)
  {
    if (pOutputNode == nullptr)
      continue;

    nodeStackTemp.PushBack(pOutputNode);

    while (!nodeStackTemp.IsEmpty())
    {
      auto pParent = nodeStackTemp.PeekBack();
      nodeStackTemp.PopBack();

      m_NodeStack.PushBack(pParent);

      auto children = ezExpressionAST::GetChildren(pParent);
      for (auto pChild : children)
      {
        if (pChild != nullptr)
        {
          nodeStackTemp.PushBack(pChild);
        }
      }
    }
  }

  m_TransformCache.Clear();

  while (!m_NodeStack.IsEmpty())
  {
    auto pParent = m_NodeStack.PeekBack();
    m_NodeStack.PopBack();

    auto children = ezExpressionAST::GetChildren(pParent);
    for (auto& pChild : children)
    {
      if (pChild == nullptr)
        continue;

      ezExpressionAST::Node* pNewChild = nullptr;
      if (m_TransformCache.TryGetValue(pChild, pNewChild) == false)
      {
        pNewChild = func(pChild);
        m_TransformCache.Insert(pChild, pNewChild);
      }

      pChild = pNewChild;
    }
  }

  return EZ_SUCCESS;
}
