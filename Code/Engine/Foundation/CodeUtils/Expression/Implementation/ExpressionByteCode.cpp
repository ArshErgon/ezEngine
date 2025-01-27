#include <Foundation/FoundationPCH.h>

#include <Foundation/CodeUtils/Expression/ExpressionByteCode.h>
#include <Foundation/CodeUtils/Expression/ExpressionFunctions.h>
#include <Foundation/IO/ChunkStream.h>
#include <Foundation/Logging/Log.h>

namespace
{
  static const char* s_szOpCodeNames[] = {
    // Unary
    "",

    "Abs_R",
    "Sqrt_R",
    "Sin_R",
    "Cos_R",
    "Tan_R",
    "ASin_R",
    "ACos_R",
    "ATan_R",

    "Mov_R",
    "Mov_C",
    "Load",
    "Store",

    "",

    // Binary
    "",

    "Add_RR",
    "Add_CR",

    "Sub_RR",
    "Sub_CR",

    "Mul_RR",
    "Mul_CR",

    "Div_RR",
    "Div_CR",

    "Min_RR",
    "Min_CR",

    "Max_RR",
    "Max_CR",

    "",

    "Call",

    "Nop",
  };

  EZ_CHECK_AT_COMPILETIME_MSG(EZ_ARRAY_SIZE(s_szOpCodeNames) == ezExpressionByteCode::OpCode::Count, "OpCode name array size does not match OpCode type count");

  static bool FirstArgIsConstant(ezExpressionByteCode::OpCode::Enum opCode)
  {
    return opCode == ezExpressionByteCode::OpCode::Mov_C || opCode == ezExpressionByteCode::OpCode::Add_CR || opCode == ezExpressionByteCode::OpCode::Sub_CR || opCode == ezExpressionByteCode::OpCode::Mul_CR || opCode == ezExpressionByteCode::OpCode::Div_CR ||
           opCode == ezExpressionByteCode::OpCode::Min_CR || opCode == ezExpressionByteCode::OpCode::Max_CR;
  }
} // namespace

ezExpressionByteCode::ezExpressionByteCode() = default;
ezExpressionByteCode::~ezExpressionByteCode() = default;

bool ezExpressionByteCode::operator==(const ezExpressionByteCode& other) const
{
  return m_ByteCode == other.m_ByteCode &&
         m_Inputs == other.m_Inputs &&
         m_Outputs == other.m_Outputs &&
         m_Functions == other.m_Functions;
}

void ezExpressionByteCode::Clear()
{
  m_ByteCode.Clear();
  m_Inputs.Clear();
  m_Outputs.Clear();
  m_Functions.Clear();

  m_uiNumInstructions = 0;
  m_uiNumTempRegisters = 0;
}

void ezExpressionByteCode::Disassemble(ezStringBuilder& out_sDisassembly) const
{
  out_sDisassembly.Append("// Inputs:\n");
  for (ezUInt32 i = 0; i < m_Inputs.GetCount(); ++i)
  {
    out_sDisassembly.AppendFormat("//  {0}: {1}\n", i, m_Inputs[i]);
  }

  out_sDisassembly.Append("\n// Outputs:\n");
  for (ezUInt32 i = 0; i < m_Outputs.GetCount(); ++i)
  {
    out_sDisassembly.AppendFormat("//  {0}: {1}\n", i, m_Outputs[i]);
  }

  out_sDisassembly.Append("\n// Functions:\n");
  for (ezUInt32 i = 0; i < m_Functions.GetCount(); ++i)
  {
    out_sDisassembly.AppendFormat("//  {0}: {1}\n", i, m_Functions[i]);
  }

  out_sDisassembly.AppendFormat("\n// Temp Registers: {0}\n", m_uiNumTempRegisters);
  out_sDisassembly.AppendFormat("// Instructions: {0}\n\n", m_uiNumInstructions);


  const StorageType* pByteCode = GetByteCode();
  const StorageType* pByteCodeEnd = GetByteCodeEnd();

  while (pByteCode < pByteCodeEnd)
  {
    OpCode::Enum opCode = GetOpCode(pByteCode);
    const char* szOpCode = s_szOpCodeNames[opCode];

    if (opCode > OpCode::FirstUnary && opCode < OpCode::LastUnary)
    {
      ezUInt32 r = GetRegisterIndex(pByteCode, 1);
      ezUInt32 x = GetRegisterIndex(pByteCode, 1);

      if (FirstArgIsConstant(opCode))
      {
        out_sDisassembly.AppendFormat("{0} r{1} {2}\n", szOpCode, r, ezArgF(*reinterpret_cast<float*>(&x), 6));
      }
      else
      {
        if (opCode == OpCode::Load)
        {
          out_sDisassembly.AppendFormat("{0} r{1} i{2}({3})\n", szOpCode, r, x, m_Inputs[x]);
        }
        else if (opCode == OpCode::Store)
        {
          out_sDisassembly.AppendFormat("{0} o{1}({3}) r{2}\n", szOpCode, r, x, m_Outputs[r]);
        }
        else
        {
          out_sDisassembly.AppendFormat("{0} r{1} r{2}\n", szOpCode, r, x);
        }
      }
    }
    else if (opCode > OpCode::FirstBinary && opCode < OpCode::LastBinary)
    {
      ezUInt32 r = GetRegisterIndex(pByteCode, 1);
      ezUInt32 a = GetRegisterIndex(pByteCode, 1);
      ezUInt32 b = GetRegisterIndex(pByteCode, 1);

      if (FirstArgIsConstant(opCode))
      {
        out_sDisassembly.AppendFormat("{0} r{1} {2} r{3}\n", szOpCode, r, ezArgF(*reinterpret_cast<float*>(&a), 6), b);
      }
      else
      {
        out_sDisassembly.AppendFormat("{0} r{1} r{2} r{3}\n", szOpCode, r, a, b);
      }
    }
    else if (opCode == OpCode::Call)
    {
      ezUInt32 uiIndex = GetFunctionIndex(pByteCode);
      const char* szName = m_Functions[uiIndex];

      ezStringBuilder sName;
      if (ezStringUtils::IsNullOrEmpty(szName))
      {
        sName.Format("Unknown_{0}", uiIndex);
      }
      else
      {
        sName = szName;
      }

      ezUInt32 r = GetRegisterIndex(pByteCode, 1);

      out_sDisassembly.AppendFormat("{0} {1} r{2}", szOpCode, sName, r);

      ezUInt32 uiNumArgs = GetFunctionArgCount(pByteCode);
      for (ezUInt32 uiArgIndex = 0; uiArgIndex < uiNumArgs; ++uiArgIndex)
      {
        ezUInt32 x = GetRegisterIndex(pByteCode, 1);
        out_sDisassembly.AppendFormat(" r{0}", x);
      }

      out_sDisassembly.Append("\n");
    }
    else
    {
      EZ_ASSERT_NOT_IMPLEMENTED;
    }
  }
}

const char* ezExpressionByteCode::GetOpCodeName(OpCode::Enum opCode)
{
  return s_szOpCodeNames[opCode];
}

void ezExpressionByteCode::Save(ezStreamWriter& stream) const
{
  ezChunkStreamWriter chunk(stream);

  chunk.BeginStream(1);

  {
    chunk.BeginChunk("MetaData", 3);

    chunk << m_uiNumInstructions;
    chunk << m_uiNumTempRegisters;
    chunk.WriteArray(m_Inputs).IgnoreResult();
    chunk.WriteArray(m_Outputs).IgnoreResult();
    chunk.WriteArray(m_Functions).IgnoreResult();

    chunk.EndChunk();
  }

  {
    chunk.BeginChunk("Code", 2);

    chunk << m_ByteCode.GetCount();
    chunk.WriteBytes(m_ByteCode.GetData(), m_ByteCode.GetCount() * sizeof(StorageType)).IgnoreResult();

    chunk.EndChunk();
  }

  chunk.EndStream();
}

ezResult ezExpressionByteCode::Load(ezStreamReader& stream)
{
  ezChunkStreamReader chunk(stream);
  chunk.SetEndChunkFileMode(ezChunkStreamReader::EndChunkFileMode::SkipToEnd);

  chunk.BeginStream();

  while (chunk.GetCurrentChunk().m_bValid)
  {
    if (chunk.GetCurrentChunk().m_sChunkName == "MetaData")
    {
      if (chunk.GetCurrentChunk().m_uiChunkVersion >= 3)
      {
        chunk >> m_uiNumInstructions;
        chunk >> m_uiNumTempRegisters;
        EZ_SUCCEED_OR_RETURN(chunk.ReadArray(m_Inputs));
        EZ_SUCCEED_OR_RETURN(chunk.ReadArray(m_Outputs));
        EZ_SUCCEED_OR_RETURN(chunk.ReadArray(m_Functions));
      }
      else
      {
        ezLog::Error("Invalid MetaData Chunk Version {0}. Expected >= 3", chunk.GetCurrentChunk().m_uiChunkVersion);

        chunk.EndStream();
        return EZ_FAILURE;
      }
    }
    else if (chunk.GetCurrentChunk().m_sChunkName == "Code")
    {
      if (chunk.GetCurrentChunk().m_uiChunkVersion >= 2)
      {
        ezUInt32 uiByteCodeCount = 0;
        chunk >> uiByteCodeCount;

        m_ByteCode.SetCountUninitialized(uiByteCodeCount);
        chunk.ReadBytes(m_ByteCode.GetData(), uiByteCodeCount * sizeof(StorageType));
      }
      else
      {
        ezLog::Error("Invalid Code Chunk Version {0}. Expected >= 2", chunk.GetCurrentChunk().m_uiChunkVersion);

        chunk.EndStream();
        return EZ_FAILURE;
      }
    }

    chunk.NextChunk();
  }

  chunk.EndStream();

  return EZ_SUCCESS;
}
