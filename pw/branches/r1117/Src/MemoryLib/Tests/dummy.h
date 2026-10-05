#pragma once

#include "MemoryLib/NewDelete.h"

class NativeCl
{
public:
  NativeCl()
  {
    int* a = new int();
    delete a;
  }
};
