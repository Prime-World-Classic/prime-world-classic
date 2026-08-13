// Boost.Thread xtime forward definition
// Lightweight header — just the struct definition

#ifndef BOOST_XTIME_FWD_HPP
#define BOOST_XTIME_FWD_HPP

#include <stdint.h>

namespace boost {

struct xtime
{
    typedef int_fast64_t xtime_sec_t;
    typedef int_fast32_t xtime_nsec_t;

    xtime_sec_t sec;
    xtime_nsec_t nsec;
};

} // namespace boost

#endif // BOOST_XTIME_FWD_HPP
