#pragma once

// ���������� ���� �������� � �������� ��� ����������: ���� �������� �� ����
#include "LuaMetaData.h"
#include "LuaWrappers.h"

// lua-�������� ���������� ����� ������: �������� ��� ��������
#include "LuaValues.h"
// lua-�������� ���������������� ����� ������: ����� � ����� ��������, � ���������
#include "LuaComplexTypes.h"

// ��������� ������: ���� �������� �� ����
#include "LuaCommon.h"


// ���������� lua-�������� �������� enum`� EnumType
// �������� �������������� � ������� �������� ���� int, �������� ������������ �������� �� ������ �� ��������������
// ����� �������� � .h - ���� enum ������������ ����������� ��������, ��� � � .cpp - ���� ������ �����.
#define LUA_ENUM_TRANSFER(EnumType)                                                                                      \
  template<> struct Lua::lua_values<EnumType>                                                                                 \
  {                                                                                                                      \
  static EnumType get(lua_State* L, int idx) { return static_cast<EnumType>(lua_values<int>::get(L, idx)); }           \
  static int      put(lua_State* L, EnumType value) { return lua_values<int>::put(L, static_cast<int>(value)); }       \
  };

// ������ ������ ����������� ���������� ������ � ������, ����������� ��� ������������ ������ Class � LUA
// ���������� ��������� � public ������ ������
#define DECLARE_LUA_TYPEINFO(Class)                                                            \
  static  Lua::LuaTypeInfo const* GetLuaTypeInfo();                                            \
  virtual Lua::LuaTypeInfo const* QueryLuaTypeInfo() const { return Class::GetLuaTypeInfo(); } \
  virtual void*                   QueryNativeLuaObject() const { return (void*)this; }         \
          void                    Lua_DestructorImplementation() { delete this; }

// ������ ������ ��������� ��������/����������� ��� �������� Name ���� Type.
// ����� ��������� � public ����� ���������� ������
#define DEFINE_LUA_PROPERTY(Name, Type, Expr)                              \
  Type Lua_Property_##Name##_Get() const { return Expr;}                   \
  void Lua_Property_##Name##_Set(Type value) { Expr = value;} 

// ������ ������ ��������� �������� ��� �������� Name ���� Type.
// ����� ��������� � public ����� ���������� ������
#define DEFINE_LUA_READONLY_PROPERTY(Name, Type, Expr)                     \
  Type Lua_Property_##Name##_Get() const { return Expr;}

// ������ ������ ��������� ����������� ��� �������� Name ���� Type.
// ����� ��������� � public ����� ���������� ������
#define DEFINE_LUA_WRITEONLY_PROPERTY(Name, Type, Expr)                    \
  void Lua_Property_##Name##_Set(Type value) { Expr = value;}

// �������� ������� ������� � �������, ������� �� LUA ��� ������ Class � ������� ������� Base, ������� 
// ����� ����� ���������������� � LUA
// �����������: �� �������������� ������������� ������������ �������, ����������������� � LUA: 
// ����� ������ ����������� ������ ���� ����
// 
#define BEGIN_LUA_TYPEINFO(Class, Base)                                                    \
struct LuaTypeInfoWrapperFor##Class                                                        \
  {                                                                                        \
  typedef Class TheClass;                                                                  \
  typedef Base  TheBase;                                                                   \
  static Lua::LuaTypeInfo const* pBase;                                                    \
  static Lua::LuaEntryBase* entries[];                                                     \
  static Lua::LuaTypeInfo typeInfo;                                                        \
  };                                                                                       \
  Lua::LuaTypeInfo const* LuaTypeInfoWrapperFor##Class::pBase = TheBase::GetLuaTypeInfo(); \
  Lua::LuaEntryBase* LuaTypeInfoWrapperFor##Class::entries[] = {

// �������� ������� ������� � �������, ������� �� LUA ��� ������ Class, ������� 
// �� ����� �������� ������, ������� ����� ���������������� � LUA. ���� ���� �� ������� ������� 
// ������ ��-���� ����� ���������������� � LUA, �� ����� ������������ BEGIN_LUA_TYPEINFO
#define BEGIN_LUA_TYPEINFO_NOBASE(Class)                                   \
struct LuaTypeInfoWrapperFor##Class                                        \
  {                                                                        \
  typedef Class TheClass;                                                  \
  static Lua::LuaTypeInfo const* pBase;                                    \
  static Lua::LuaEntryBase* entries[];                                     \
  static Lua::LuaTypeInfo typeInfo;                                        \
  };                                                                       \
  Lua::LuaTypeInfo const* LuaTypeInfoWrapperFor##Class::pBase = NULL;      \
  Lua::LuaEntryBase* LuaTypeInfoWrapperFor##Class::entries[] = {

//////////////////////////////////////////////////////////////////////////
// ��� ��������� ������� ������ �������������� ������ ����� 
// BEGIN_LUA_TYPEINFO/BEGIN_LUA_TYPEINFO_NOBASE � END_LUA_TYPEINFO
//////////////////////////////////////////////////////////////////////////

// ���������� ������ ������� ������ ��� ������ �� �������� �������������� � LUA ����.
// ������ ������ ����� ������������ ������ � ��������� ���������:
// 1. ������ �������� �� new
// 2. ����� ����� ������� ������ ����� �� ��������������
// ������������ ����� ���������. ������ ������������� - lua-�������� ����t`�� (LuaComplexTypes.h)
#define LUA_DESTRUCTOR() \
  new Lua::LuaMethodEntry(LUA_DESTRUCTOR_NAME, &Lua::make_member_wrapper(&TheClass::Lua_DestructorImplementation).Func<&TheClass::Lua_DestructorImplementation, TheClass*(*)(lua_State* L), &Lua::TypedObjectPtrFromMeta< TheClass > >),

// ������� ��������� � LUA ����� Method ������� ������ ��� ������ Method.
// �����������: 
// 1. ����� ������ ���� public
// 2. lua-�������� ����� ������������� �������� � ���������� ������ ���� ��������
// 3. �� �������������� ������������� �������, �.�. int Foo(int arg); int Foo(char const* arg);
#define LUA_METHOD(Method)                                                 \
  new Lua::LuaMethodEntry(#Method, &Lua::make_member_wrapper(&TheClass::Method).Func<&TheClass::Method, TheClass*(*)(lua_State* L), &Lua::TypedObjectPtrFromMeta< TheClass > >),

// ������� ��������� � LUA ����� Method ������� ������ ��� ������ Name.
// �����������: ��. LUA_METHOD
#define LUA_METHOD_EX(Name, Method)                                                 \
  new Lua::LuaMethodEntry(Name, &Lua::make_member_wrapper(&TheClass::Method).Func<&TheClass::Method, TheClass*(*)(lua_State* L), &Lua::TypedObjectPtrFromMeta< TheClass > >),

// ������� ��������� � LUA �������� Name. 
// ��� �������/����������� �������� ������������ ������ Getter/Setter
// �����������: 
// 1. ������ Getter/Setter ������ ���� public
// 2. lua-�������� ���� �������� ������ ���� ��������
#define LUA_PROPERTY_EX(Name, Getter, Setter)                                                                                                  \
  new Lua::LuaFieldEntry(#Name,                                                                                                                \
  &Lua::make_member_wrapper(&TheClass::Getter).Func<&TheClass::Getter, TheClass*(*)(lua_State* L), &Lua::TypedObjectPtrFromMeta< TheClass > >, \
  &Lua::make_member_wrapper(&TheClass::Setter).Func<&TheClass::Setter, TheClass*(*)(lua_State* L), &Lua::TypedObjectPtrFromMeta< TheClass > > ),

// ������� ��������� � LUA �������� ������ ��� ������ Name. 
// ��� ������� � �������� ������������ ���� Getter
// �����������: 
// 1. ����� Getter ������ ���� public
// 2. lua-�������� ���� �������� ������ ���� ��������
#define LUA_READONLY_PROPERTY_EX(Name, Getter)                                                                                                 \
  new Lua::LuaFieldEntry(#Name,                                                                                                                \
  &Lua::make_member_wrapper(&TheClass::Getter).Func<&TheClass::Getter, TheClass*(*)(lua_State* L), &Lua::TypedObjectPtrFromMeta< TheClass > >, \
  NULL),

// ������� ��������� � LUA �������� ������ ��� ������ Name. 
// ��� ������� � �������� ������������ ���� Setter
// �����������: 
// 1. ����� Setter ������ ���� public
// 2. lua-�������� ���� �������� ������ ���� ��������
#define LUA_WRITEONLY_PROPERTY_EX(Name, Setter)                                                                                                 \
  new Lua::LuaFieldEntry(#Name,                                                                                                                 \
  NULL,                                                                                                                                         \
  &Lua::make_member_wrapper(&TheClass::Setter).Func<&TheClass::Getter, TheClass*(*)(lua_State* L), &Lua::TypedObjectPtrFromMeta< TheClass > >),

// ������� ��������� � LUA �������� Name. 
#define LUA_PROPERTY(Name) \
  LUA_PROPERTY_EX(Name, Lua_Property_##Name##_Get, Lua_Property_##Name##_Set)

// ������� ��������� � LUA �������� ������ ��� ������ Name. 
#define LUA_READONLY_PROPERTY(Name) \
  LUA_READONLY_PROPERTY_EX(Name, Lua_Property_##Name##_Get)

// ������� ��������� � LUA �������� ������ ��� ������ Name. 
#define LUA_WRITEONLY_PROPERTY(Name) \
  LUA_WRITEONLY_PROPERTY_EX(Name, Lua_Property_##Name##_Set)

// ��������� ������� ������� � �������, ������� �� LUA ��� ������� ������ 
#define END_LUA_TYPEINFO(Class)                                                                                                         \
  NULL                                                                                                                                  \
  } ;                                                                                                                                   \
  Lua::LuaTypeInfo LuaTypeInfoWrapperFor##Class::typeInfo("Native_"#Class, pBase, entries, sizeof(entries) / sizeof(entries[0]) - 1); \
  Lua::LuaTypeInfo const* Class::GetLuaTypeInfo() { return &LuaTypeInfoWrapperFor##Class::typeInfo; }

// ��������� ������ ��� ������ ��������� �� ������ � LUA_METHOD_IMPL/LUA_METHOD_IMPL_RV
#define LUA_IMPL_ERROR_NOTIFY(NAME) \
  {systemLog( NLogg::LEVEL_DEBUG ) << "Failed to call handler <" << NAME << ">" << endl; }

// ��������� ������ �������� ������������ ��� ����������� �������, ������������� ������ ��������� � lua
// LUA_METHOD_IMPL, LUA_METHOD_IMPL_1 ... - �������, ������� �� ���������� ��������
// ��������� - ��� �������, ��������� (���� ����) 
#define LUA_METHOD_IMPL(NAME)                                          \
  if(!CallHandler(#NAME))                                              \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}

#define LUA_METHOD_IMPL_1(NAME, ARG1)                                  \
  if(!CallHandler(#NAME, ARG1))                                        \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}

#define LUA_METHOD_IMPL_2(NAME, ARG1, ARG2)                            \
  if(!CallHandler(#NAME, ARG1, ARG2))                                  \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}

#define LUA_METHOD_IMPL_3(NAME, ARG1, ARG2, ARG3)                      \
  if(!CallHandler(#NAME, ARG1, ARG2, ARG3))                            \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}

#define LUA_METHOD_IMPL_4(NAME, ARG1, ARG2, ARG3, ARG4)                \
  if(!CallHandler(#NAME, ARG1, ARG2, ARG3, ARG4))                      \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}

#define LUA_METHOD_IMPL_5(NAME, ARG1, ARG2, ARG3, ARG4, ARG5)          \
  if(!CallHandler(#NAME, ARG1, ARG2, ARG3, ARG4, ARG5))                \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}

// LUA_METHOD_IMPL, LUA_METHOD_IMPL_1 ... - �������, ������� ���������� ��������
// ��������� - ��� ������������� ��������, ��� �������, ��������� (���� ����) 
#define LUA_METHOD_IMPL_RV(TYPE, NAME)                                 \
  TYPE result = TYPE();                                                \
  if(!CallMethod(#NAME, result))                                       \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}                                                              \
  return result;

#define LUA_METHOD_IMPL_RV_1(TYPE, NAME, ARG1)                         \
  TYPE result = TYPE();                                                \
  if(!CallMethod(#NAME, result, ARG1))                                 \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}                                                              \
  return result;

#define LUA_METHOD_IMPL_RV_2(TYPE, NAME, ARG1, ARG2)                   \
  TYPE result = TYPE();                                                \
  if(!CallMethod(#NAME, result, ARG1, ARG2))                           \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}                                                              \
  return result;

#define LUA_METHOD_IMPL_RV_3(TYPE, NAME, ARG1, ARG2, ARG3)             \
  TYPE result = TYPE();                                                \
  if(!CallMethod(#NAME, result, ARG1, ARG2, ARG3))                     \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}                                                              \
  return result;

#define LUA_METHOD_IMPL_RV_4(TYPE, NAME, ARG1, ARG2, ARG3, ARG4)       \
  TYPE result = TYPE();                                                \
  if(!CallMethod(#NAME, result, ARG1, ARG2, ARG3, ARG4))               \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}                                                              \
  return result;

#define LUA_METHOD_IMPL_RV_5(TYPE, NAME, ARG1, ARG2, ARG3, ARG4, ARG5) \
  TYPE result = TYPE();                                                \
  if(!CallMethod(#NAME, result, ARG1, ARG2, ARG3, ARG4, ARG5))         \
  {                                                                    \
    LUA_IMPL_ERROR_NOTIFY(#NAME)                                       \
  }                                                                    \
  else {}                                                              \
  return result;
                                                                                   