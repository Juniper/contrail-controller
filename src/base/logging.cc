/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"

#include <sys/types.h>
#include <unistd.h>
#include <log4cplus/helpers/pointer.h>
#include <log4cplus/configurator.h>
#include <log4cplus/fileappender.h>
#include <log4cplus/syslogappender.h>

#include <boost/format.hpp>
#include <boost/algorithm/string/predicate.hpp>

using namespace log4cplus;

static bool disabled_;
static const char *loggingPattern = "%D{%Y-%m-%d %a %H:%M:%S:%Q %Z} "
                                    " %h [Thread %t, Pid %i]: %m%n";

bool LoggingDisabled() {
    return disabled_;
}

void SetLoggingDisabled(bool flag) {
    disabled_ = flag;
}

void CheckEnvironmentAndUpdate() {
    if (getenv("LOG_DISABLE") != NULL) {
        SetLoggingDisabled(true);
    }
}

void LoggingInit() {
    BasicConfigurator config;
    config.configure();
    Logger logger = Logger::getRoot();
    std::auto_ptr<Layout> layout_ptr(new PatternLayout(loggingPattern));
    logger.getAllAppenders().at(0)->setLayout(layout_ptr);
    CheckEnvironmentAndUpdate();
}

void LoggingInit(const std::string &filename, long maxFileSize, int maxBackupIndex,
                 bool useSyslog, const std::string &syslogFacility,
                 const std::string &ident) {
    helpers::Properties props;
    props.setProperty(LOG4CPLUS_TEXT("rootLogger"),
            LOG4CPLUS_TEXT("DEBUG"));
    PropertyConfigurator config(props);
    Logger logger = Logger::getRoot();

    if (filename == "<stdout>" || filename.length() == 0) {
        BasicConfigurator config;
        config.configure();
    } else {
        SharedAppenderPtr fileappender(new RollingFileAppender(filename,
                                           maxFileSize, maxBackupIndex));
        logger.addAppender(fileappender);
    }

    std::auto_ptr<Layout> layout_ptr(new PatternLayout(loggingPattern));
    logger.getAllAppenders().at(0)->setLayout(layout_ptr);

    if (useSyslog) {
        std::string syslogident = boost::str(
            boost::format("%1%[%2%]") % ident % getpid());
        props.setProperty(LOG4CPLUS_TEXT("facility"),
                          boost::starts_with(syslogFacility, "LOG_")
                        ? syslogFacility.substr(4)
                        : syslogFacility);
        props.setProperty(LOG4CPLUS_TEXT("ident"), syslogident);
        SharedAppenderPtr syslogappender(new SysLogAppender(props));
        std::auto_ptr<Layout> syslog_layout_ptr(new PatternLayout(
                                                    loggingPattern));
        syslogappender->setLayout(syslog_layout_ptr);
        logger.addAppender(syslogappender);
    }

    CheckEnvironmentAndUpdate();
}
