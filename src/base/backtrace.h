/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BASE_BACKTRACE_H__
#define BASE_BACKTRACE_H__

#include <string>
#include <unistd.h>

//
// Utility routines to retrieve and log back trace from stack at run time in
// human readable form
//
class BackTrace {
private:
    static ssize_t ToString(void * const* callstack, int frames, char *buf,
                            size_t buf_len);

public:
    static void Log(const std::string &msg);
    static void Log(void * const* callstack, int frames,
                    const std::string &msg);
    static int Get(void * const* &callstack);
};

#endif  // BASE_BACKTRACE_H__
