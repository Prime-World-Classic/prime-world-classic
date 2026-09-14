#include "stdafx.h"
#include "RPC.h"
#include "CppWrapper.h"
#include <System/fixedvector.h>
#include <System/ThreadLocal.h>

namespace rpc
{

struct _Byte2 { byte v[2]; };
struct _Byte4 { byte v[4]; };
struct _Byte8 { byte v[8]; };
struct _Byte1 { byte v; };

bool FillStack(Arguments& args, Stack& w, uint paramsCount, const rpc::MethodInfo& minfo, const Transport::MessageMiscInfo & miscInfo, uint& methodCode)
{
  if (minfo.paramsCount-(minfo.hasRecieveTime?1:0) != paramsCount)
  {
    return false;
  }
  bool result = true;
  MethodCodeCalculator calc;
  for (uint i=0;result && i<paramsCount;++i) 
  {
    ParamInfo type = args.PopTypeCode(result);
    if (result)
    {
      switch (type.code)
      {
      case rpc::Byte4: w.Push(args.PopRaw<_Byte4>(result)); break;
      case rpc::Byte8: w.Push(args.PopRaw<_Byte8>(result)); break;
      case rpc::Byte1: w.Push(args.PopRaw<_Byte1>(result)); break;
      case rpc::Byte2: w.Push(args.PopRaw<_Byte2>(result)); break;
      case rpc::String: w.Push(args.PopRaw<const char*>(result)); break;
      case rpc::StlString: w.Push(&args.PopRaw<const nstl::string&>(result)); break;
      case rpc::VectorOfScalars: w.Push(&args.PopVectorWithoutTypeCode<byte>(result, 0)); break;
      case rpc::VectorOfStructs: w.Push(&args.PopVectorWithoutTypeCode<byte>(result, GetGlobalDataFactory())); break;
      case rpc::VectorOfStrings: w.Push(&args.PopVectorOfStrings<nstl::string>(result)); break;
      case rpc::RawStruct: 
        {
          int structSize=0;
          const byte* data = args.PopRawStruct(result, &structSize);
          if (result)
          {
            if (data) // just in case, paranoid check
            {
              if (i < 32 && (minfo.structPtrParams & (1u << i)))
              {
                // C++ parameter is a pointer/reference to a plain struct (the
                // generated VCall pops T* and the callee dereferences it). Pass a
                // POINTER to the struct content living in the packet buffer — the
                // legacy 32-bit contract. (Pushing the content here would make the
                // pop read the first 8 content bytes as a pointer and shift every
                // following argument: IGameServer::AddClient got a garbage
                // IGameClient* and crashed on 64-bit after the 2026-09-05 fix.)
                w.Push(data); // pointer to the content
              }
              else
              {
                // C++ parameter is taken by value (enum / small struct): the
                // generated VCall code value-copies each argument
                // (P0 p0 = _mng_va_arg(...)), so the stack must contain the
                // struct CONTENT, not a pointer. Pushing a pointer only
                // "worked" on 32-bit for by-value params (the callee received the
                // pointer value itself as a garbage-but-tolerated value); on
                // 64-bit it corrupted the argument and every following one, e.g.
                // IOpenSessionCallback::OnOpenSession(Result::Enum rc, u64 sid).
                w.Push(data, structSize); // here we just fill the stack with struct content, hack !
              }
            }
          }
          break;
        } 
      case rpc::RawStructByValue:
        {
          int structSize=0;
          const byte* data = args.PopRawStruct(result, &structSize);
          if (result)
          {
            if (data) // just in case, paranoid check
            {
              w.Push(data, structSize); // here we just fill the stack with struct content, hack !
            }
          }
          break;
        }
      case rpc::Struct: 
        {
          const rpc::Data* data = args.PopRawSerializedData(result, GetGlobalDataFactory());
          if (result)
          {
            w.Push(data); // push pointer
          }
          break;
        }
      case rpc::StructByValue:
        {
          const rpc::Data* data = args.PopRawSerializedData(result, GetGlobalDataFactory());
          if (result)
          {
            if (data) // just in case, paranoid check
            {
              rpc::TypeInfo info = data->GetTypeInfo();
              w.Push(data, info.size); // here we just fill the stack with struct content, hack !
            }
          }
          break;
        }
      case rpc::RemoteEntity:
        {
          IRemoteEntity* entity = args.PopRawEntity(result);
          if (result)
          {
            w.Push(entity);
          }
          break;
        }
      case rpc::FixedVectorOfScalars: 
        w.Push(args._PopFixedVectorWithoutTypeCode(result)); 
        break;
      case rpc::MemoryBlockType:
        w.Push(args.PopMemoryBlockRaw(result));
        break;
      case rpc::VectorOfMemoryBlocks:
        w.Push(args.PopVectorOfMemoryBlocksRaw(result));
        break;
      default:
        result = false;
      }   
    }
    result &= w.IsOk();
    calc.Add(rpc::ParamTypes(type.code));
  }
  methodCode = calc.GetCode();
  if (minfo.hasRecieveTime)
  {
    w.Push(&miscInfo);
  }
  return result & w.IsOk();
}

static ThreadLocal<Stack> stackBuffer;

MethodCallStack FillStack(const MethodCallHeader& call, rpc::Arguments& args, const byte* paramsData, uint paramsSize, const rpc::MethodInfo& minfo)
{
  NI_PROFILE_FUNCTION;
  args.Reset(paramsData, call.GetPreAllocatedSize(), paramsSize);

  Stack& fstack = stackBuffer.GetValue();
  fstack.Reset();

  MethodCallStack stack = {0, 0, false, 0};
  if (FillStack(args, fstack, call.GetParamsCount(), minfo, call.miscInfo, stack.methodCode))
  {
    stack.size = fstack.Stop();
    stack.isValid = true;
    stack.buffer = fstack.GetStack();
  }
  return stack;
}

bool CppCallRoutine::ProcessReturnValue(const MethodCallHeader& methodCall, const byte* paramsData, int _paramsSize, rpc::Arguments& args)
{
  NI_PROFILE_FUNCTION;

  rpc::MethodInfo minfo = { "return", methodCall.GetParamsCount(), false };
  MethodCallStack stack = FillStack(methodCall, args, paramsData, _paramsSize, minfo);
  if (stack.isValid)
  {
    bool check = Process(stack.buffer, stack.size);
    if ( !check )
      LOG_W(0) << "Return value processing failed";
    return true;
  }
  return stack.isValid;
}

ICallRoutine& EntityMap::RegisterSynchronousCall(const MethodCallHeader& header)
{
  SyncCallRoutine* routine = new SyncCallRoutine;
  calls->RegisterCall(header, *routine);
  return *routine;
}

void EntityMap::RegisterAsyncCall(const MethodCallHeader& header, IFunctor* functor, float timeout)
{
  AsyncCallRoutine* routine = new AsyncCallRoutine(functor);
  RegisterAsyncCall(header, functor, routine, timeout);
}

} // rpc