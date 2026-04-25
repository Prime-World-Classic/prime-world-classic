///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct lua_State;
typedef int (*lua_CFunction) (lua_State *L);

#define SCRIPT_TOKEN_CONCAT_IMPL(a, b) a##b
#define SCRIPT_TOKEN_CONCAT(a, b) SCRIPT_TOKEN_CONCAT_IMPL(a, b)
#define SCRIPT_TOKEN_CONCAT3(a, b, c) SCRIPT_TOKEN_CONCAT(SCRIPT_TOKEN_CONCAT(a, b), c)
#define SCRIPT_TOKEN_CONCAT4(a, b, c, d) SCRIPT_TOKEN_CONCAT(SCRIPT_TOKEN_CONCAT3(a, b, c), d)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global functions description
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define REGISTER_LUA_GLOBAL_SFUNCTION( fname )																																								\
static int _##fname##_cb( lua_State *L )																																											\
{																																																															\
	return LuaCallBack( #fname, L, &##fname##);																																									\
}																																																															\
static struct SRegisterScriptFunction_##fname																																									\
{																																																															\
	SRegisterScriptFunction_##fname()																																													\
	{																																																														\
		NScript::AddSFunctionToGlobals( #fname, &_##fname##_cb );																																	\
	}																																																														\
} registerScriptFunction_##fname##;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define REGISTER_LUA_SFUNCTION( ScriptEngine, fname )																																					\
static int _##fname##_cb( lua_State *L )																																											\
{																																																															\
	return LuaCallBack( #fname, L, &##fname##);																																									\
}																																																															\
static struct SRegisterScriptFunction_##fname																																									\
{																																																															\
	SRegisterScriptFunction_##fname()																																													\
	{																																																														\
		ScriptEngine.AddFunctionToRegList( #fname, &_##fname##_cb );																															\
	}																																																														\
} registerScriptFunction_##fname##;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Регистрация глобальной скриптовой функции без биндинга, вызывается функция такого типа:
// int CallbackFunction(lua_State * L)
#define REGISTER_LUA_SFUNCTION_IMMEDIATE( ScriptEngine, fname )																																\
static struct SCRIPT_TOKEN_CONCAT4( SRegisterScriptFunction_, __LINE__, _, fname )																																		\
{																																																															\
	SCRIPT_TOKEN_CONCAT4( SRegisterScriptFunction_, __LINE__, _, fname )()																																						\
	{																																																														\
		ScriptEngine.AddFunctionToRegList( #fname, &fname );																																			\
	}																																																														\
} SCRIPT_TOKEN_CONCAT4( registerScriptFunction_, __LINE__, _, fname );
