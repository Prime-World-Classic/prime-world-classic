#pragma once

#include <stdarg.h>
#include <System/nvector.h>
#include <System/nstring.h>
#include <System/Basic.h>
#include "RpcDataProcessor.h"
#include "RpcThreadPool.h"
#include "RpcNode.h"


namespace rpc
{

enum IdType
{
  OriginalType,
  RemoteType,
  GeneratedType,
  BaseRemoteType,
};

_interface IPipeProcessor;
_interface IPacketPipe;
_interface ILocalEntityFactory;
class Node;

/// <summary>
/// ��������� ��������� ������� RPC � ������������ ������� ��������� ��������������. 
/// ������������ ����� ���������� ����� �������� ���������� ���������� ����������. 
/// ������ Replicate, IsReplicated, Dereplicate ��������� ��������� �������. 
/// ������ Block, Unblock, SetExclusive, ClearExclusive ��������� ��������� ������� �� R-������� 
/// �� ��� ��������� ������ (�-��������).
/// ����� ���� ������������� ��������� ��� ����������� �������� �� ������� �������, 
/// � ����� � ��� �������������� ������� �������, ������� ������� ����� ����� ���� ������� 
/// ����� ���� ������� �����. 
/// ����� RegisterObject ������������ ������, ������ RegisterFactory � UnregisterFactory ��������� ������������ ������ �������.
/// <summary>
class Gate : public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_1( Gate, BaseObjectMT );
public:
  Gate(IPipeProcessor* _processor=0, int _threadCount=0);

  ~Gate();

  const GUID& GetID() const { return _server.GetID(); }

  /// ������ ���� �� ����������� ������.
  StrongMT<Node> AddPipe(IPacketPipe * pipe, const GUID* id=0);

  /// ������� ����.
  void RemovePipe(Node* node)
  {
    _server.RemoveNode(node);
  }

  /// ������������ �������� ������ � ����� ������� �����.
  void Poll();

  /// ������ ������� ������� ������� � �������� ����.
  /// 
  /// <param name="entity">R-������ ��� ����������</param>
  /// <param name="node">����, �� ������� �������������</param>
  /// <returns>���������� ���������� ����������</returns>
  template <typename T>
  bool Replicate(T& entity, Node& node)
  {
    EntityHandler* handler = entity.GetHandler();
    threading::MutexLock lock(dataProcessing);
    return handler->Replicate(entity, node);
  }

  /// ����������, �������������� �� ������ ������ � �������� ����.
  ///
  /// Method:    IsReplicated
  /// FullName:  rpc::Gate<T>::IsReplicated
  /// Access:    public 
  /// Returns:   bool
  /// Qualifier:
  /// Parameter: T & entity - R-������, ��� �������� ���� ����������, �������������� �� ��
  /// Parameter: rpc::Node & node - ����, ��� ������� ���� ����������
  //************************************
  template <typename T>
  bool IsReplicated(T& entity, Node& node)
  {
    threading::MutexLock lock(dataProcessing);
    return entity.GetHandler()->IsReplicated(node);
  }

  /// ��������� �������� ������� �� ��������� R-������� �� �������� ����.
  template <typename T>
  void Block(T& entity, Node& node)
  {
    threading::MutexLock lock(dataProcessing);
    entity.GetHandler()->Block(node);
  }

  /// ��������� �������� ������� �� ��������� R-������� �� �������� ����.
  template <typename T>
  void Unblock(T& entity, Node& node)
  {
    threading::MutexLock lock(dataProcessing);
    EntityHandler* handler = entity.GetHandler();
    if (!handler->Unblock(node))
    {
      handler->Replicate(entity, node);
    }
  }

  template <typename T>
  bool UnblockWithoutReplicate(T& entity, Node& node)
  {
    threading::MutexLock lock(dataProcessing);
    return entity.GetHandler()->Unblock(node);
  }

  /// ������������� ����� ������������� �������� ������� �� ��������� R-������� ������ �� �������� ����.
  template <typename T>
  void SetExclusive(T& entity, Node& node)
  {
    threading::MutexLock lock(dataProcessing);
    entity.GetHandler()->SetExclusive(node);
  }

  /// �������� ������������ ����� ��� ���� ������-����.
  template <typename T>
  void ClearExclusive(T& entity)
  {
    threading::MutexLock lock(dataProcessing);
    entity.GetHandler()->ClearExclusive();
  }

  /// ������� ������� ������� ������� � �������� ����.
  template <typename T>
  void Dereplicate(T& entity, Node& node)
  {
    EntityHandler* handler = entity.GetHandler();
    threading::MutexLock lock(dataProcessing);
    int index = handler->Dereplicate(node);
    node.RemoveEntity(handler, index);
  }

  /// ������������ ������ �� ������� ��� ������������ ��������� � ���� �� ��������.
  void DereplicateAll(Node& node)
  {
    threading::MutexLock lock(dataProcessing);
    node.DereplicateAll();
  }

  /// ������������ ������ �� ������� ��� ������������ ��������� � ���� �� ��������.
  template <typename T>
  void RegisterObject(StrongMT<T> value, const char* path, const char* password=0)
  {
    threading::MutexLock lock(dataProcessing);
    _server.RegisterObject(value, path, password);
  }

  template <typename T>
  bool UnregisterObject(StrongMT<T> value)
  {
    threading::MutexLock lock(dataProcessing);
    T* object = value.Get();
    return _server.UnregisterObject(GetId<T>(), object);
  }

  bool RegisterObject(uint classId, void* instance, const char* path, const char* password=0)
  {
    threading::MutexLock lock(dataProcessing);
    return _server.RegisterObject(classId, instance, path, password);
  }

  /// ������������ ������� ������ �� �������
  template<class T>
  void RegisterFactory( ILocalEntityFactory& factory)
  {
    threading::MutexLock lock(dataProcessing);
    _server.RegisterFactory( GetId<T>(0, BaseRemoteType), factory);
  }

  /// ������� ���������� ������� �������
  template<class T>
  void UnregisterFactory()
  {
    threading::MutexLock lock(dataProcessing);
    _server.UnregisterFactory( GetId<T>(0, BaseRemoteType) );
  }
private:
  threading::Mutex dataProcessing;
  ServerDataProcessor _server;
  ThreadPool pool;
};

} // rpc
