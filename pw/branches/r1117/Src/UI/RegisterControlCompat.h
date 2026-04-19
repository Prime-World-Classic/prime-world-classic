#pragma once

#undef REGISTER_CONTROLTYPE

#define REGISTER_CONTROLTYPE( layoutType, controlType ) \
static Window * ConstructControl_##controlType() \
{ \
  return new controlType; \
} \
static struct SHandlerRegister_##controlType \
{ \
  SHandlerRegister_##controlType() \
  { \
    RegisterControlConstructor( layoutType::GetTypeName(), &ConstructControl_##controlType ); \
  } \
} HandlerRegistrator_##controlType;
