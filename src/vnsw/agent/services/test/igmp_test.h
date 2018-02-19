/*
 *  * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 *   */

#ifndef __IGMP_TEST_H__
#define __IGMP_TEST_H__

#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>
#include <log4cplus/configurator.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#endif

#include <boost/units/detail/utility.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define TYPE_NAME(_type) boost::units::detail::demangle(typeid(_type).name())
#define TEST_LOG(_Level, _Msg)                                          \
    // cout << _Msg << endl;        // Not printing for now.

#endif /* __IGMP_TEST_H__ */
