/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __LOGGING_H__
#define __LOGGING_H__

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

#define LOG(_Level, _Msg)                                        \
    do {                                                         \
        if (LoggingDisabled()) break;                            \
        log4cplus::Logger logger = log4cplus::Logger::getRoot(); \
        LOG4CPLUS_##_Level(logger, _Msg);                        \
    } while (0)

void LoggingInit();
void LoggingInit(const std::string &filename,
                 long maxFileSize,
                 int maxBackupIndex,
                 bool useSyslog,
                 const std::string &syslogFacility,
                 const std::string &ident);

//
// Disable logging - For testing purposes only
//
bool LoggingDisabled();
void SetLoggingDisabled(bool flag);

#endif /* __LOGGING_H__ */
