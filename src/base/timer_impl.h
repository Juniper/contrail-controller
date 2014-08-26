/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BASE_TIMER_IMPL_H_
#define BASE_TIMER_IMPL_H__


#include <boost/version.hpp>
#if BOOST_VERSION >= 104900
#include <boost/asio/steady_timer.hpp>
#else
#include <boost/asio/monotonic_deadline_timer.hpp>
#endif

class TimerImpl {
public:
#if BOOST_VERSION >= 104900
    typedef boost::asio::steady_timer TimerType;
#else
    typedef boost::asio::monotonic_deadline_timer TimerType;
#endif

    TimerImpl(boost::asio::io_service &io_service)
            : timer_(io_service) {
    }

#if BOOST_VERSION >= 104900
    void expires_from_now(int ms, boost::system::error_code &ec) {
        timer_.expires_from_now(boost::chrono::milliseconds(ms), ec);
    }
#else
    void expires_from_now(int ms, boost::system::error_code &ec) {
        timer_.expires_from_now(boost::posix_time::milliseconds(ms), ec);
    }
#endif

    template <typename WaitHandler>
    void async_wait(WaitHandler handler) {
        timer_.async_wait(handler);
    }

    void cancel(boost::system::error_code &ec) {
        timer_.cancel(ec);
    }

private:
    TimerType timer_;
};

#endif  // BASE_TIMER_IMPL_H__
