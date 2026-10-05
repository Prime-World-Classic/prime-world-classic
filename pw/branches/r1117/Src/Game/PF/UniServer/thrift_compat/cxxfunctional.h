// Compatibility shim for Thrift 0.9.1 cxxfunctional.h (C++14)
#ifndef _THRIFT_CXXFUNCTIONAL_H_
#define _THRIFT_CXXFUNCTIONAL_H_ 1

#include <functional>

namespace apache { namespace thrift { namespace stdcxx {
    using ::std::function;
    using ::std::bind;

    namespace placeholders {
      using ::std::placeholders::_1;
      using ::std::placeholders::_2;
      using ::std::placeholders::_3;
      using ::std::placeholders::_4;
      using ::std::placeholders::_5;
      using ::std::placeholders::_6;
    }
  }}}

namespace tcxx = apache::thrift::stdcxx;

#endif
