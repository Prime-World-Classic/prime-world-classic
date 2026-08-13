// Boost.Thread xtime helper functions
// Separated from xtime.hpp to avoid circular include:
//   xtime.hpp -> thread_time.hpp -> ... -> xtime.hpp

#ifndef BOOST_XTIME_HELPERS_HPP
#define BOOST_XTIME_HELPERS_HPP

#include <boost/thread/thread_time.hpp>
#include <boost/date_time/posix_time/conversion.hpp>

namespace boost {

inline system_time xtime_to_system_time(xtime const& xt)
{
    return boost::posix_time::from_time_t(0)+
        boost::posix_time::seconds(static_cast<long>(xt.sec))+
#ifdef BOOST_DATE_TIME_HAS_NANOSECONDS
        boost::posix_time::nanoseconds(xt.nsec);
#else
    boost::posix_time::microseconds((xt.nsec+500)/1000);
#endif
}

inline xtime get_xtime(boost::system_time const& abs_time)
{
    xtime res;
    boost::posix_time::time_duration const time_since_epoch=abs_time-boost::posix_time::from_time_t(0);
    res.sec=static_cast<xtime::xtime_sec_t>(time_since_epoch.total_seconds());
    res.nsec=static_cast<xtime::xtime_nsec_t>(time_since_epoch.fractional_seconds()*(1000000000/time_since_epoch.ticks_per_second()));
    return res;
}

inline int xtime_get(xtime* xtp, int clock_type)
{
    if (clock_type == TIME_UTC)
    {
        *xtp=get_xtime(get_system_time());
        return clock_type;
    }
    return 0;
}

inline int xtime_cmp(xtime const& xt1, xtime const& xt2)
{
    if (xt1.sec == xt2.sec)
        return (int)(xt1.nsec - xt2.nsec);
    else
        return (xt1.sec > xt2.sec) ? 1 : -1;
}

} // namespace boost

#endif // BOOST_XTIME_HELPERS_HPP
