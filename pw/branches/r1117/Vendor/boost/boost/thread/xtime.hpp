// Copyright (C) 2001-2003
// William E. Kempf
// Copyright (C) 2007-8 Anthony Williams
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//  Linux/GCC-15 patch: use TIME_UTC_ to avoid conflict with <time.h> TIME_UTC macro

#ifndef BOOST_XTIME_WEK070601_HPP
#define BOOST_XTIME_WEK070601_HPP

#include <boost/thread/xtime_fwd.hpp>   // struct xtime (always processed)
#include <boost/thread/xtime_helpers.hpp>  // helper functions

namespace boost {

enum xtime_clock_types
{
    TIME_UTC_=1
};

// Backwards compat: some code may reference TIME_UTC (without underscore)
#define TIME_UTC TIME_UTC_

} // namespace boost

#endif //BOOST_XTIME_WEK070601_HPP
