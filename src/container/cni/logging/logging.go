// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

package cniLogging

import (
    "fmt"
    "github.com/natefinch/lumberjack"
    "log"
    "os"
)

var (
    infoLogger  *log.Logger
    errorLogger *log.Logger
)

func Init(fileName string, fileSize int, backupCount int) {
    writer := &lumberjack.Logger{
        Filename:   fileName,
        MaxSize:    fileSize,
        MaxBackups: backupCount,
    }

    prefix := fmt.Sprintf("I : %d : ", os.Getpid())
    infoLogger = log.New(writer, prefix, log.LstdFlags|log.Lshortfile)
    prefix = fmt.Sprintf("E : %d : ", os.Getpid())
    errorLogger = log.New(writer, prefix, log.LstdFlags|log.Lshortfile)
}

func Info(format string, a ...interface{}) {
    infoLogger.Output(2, fmt.Sprintf(format, a...))
}

func Infof(format string, a ...interface{}) {
    infoLogger.Output(2, fmt.Sprintf(format, a...))
}

func Error(format string, a ...interface{}) {
    errorLogger.Output(2, fmt.Sprintf(format, a...))
}

func Errorf(format string, a ...interface{}) {
    errorLogger.Output(2, fmt.Sprintf(format, a...))
}
