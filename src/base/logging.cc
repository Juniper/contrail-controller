/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"

#include <log4cplus/helpers/pointer.h>
#include <log4cplus/configurator.h>
#include <log4cplus/fileappender.h>

using namespace log4cplus;

static bool disabled_;

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
    std::auto_ptr<Layout> layout_ptr(new PatternLayout("%D{%Y-%m-%d %a %H:%M:%S:%Q %Z} %h [Thread %t, Pid %i]: %m%n"));
    logger.getAllAppenders().at(0)->setLayout(layout_ptr);
    CheckEnvironmentAndUpdate();
}

void LoggingInit(std::string filename, long maxFileSize, int maxBackupIndex) {
    helpers::Properties props;
    props.setProperty(LOG4CPLUS_TEXT("rootLogger"),
            LOG4CPLUS_TEXT("DEBUG"));
    PropertyConfigurator config(props);

    SharedAppenderPtr fileappender(new RollingFileAppender(filename,
                                           maxFileSize, maxBackupIndex));
    std::auto_ptr<Layout> layout_ptr(new PatternLayout("%D{%Y-%m-%d %a %H:%M:%S:%Q %Z} %h [Thread %t, Pid %i]: %m%n"));
    fileappender->setLayout(layout_ptr);

    Logger logger = Logger::getRoot();
    logger.addAppender(fileappender);
    CheckEnvironmentAndUpdate();
}
