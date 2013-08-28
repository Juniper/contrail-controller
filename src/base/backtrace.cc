/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string.hpp>
#include <execinfo.h>
#include <stdio.h>

#include "base/util.h"
#include "base/logging.h"

ssize_t BackTrace::ToString(void * const* callstack, int frames, char *buf,
                            size_t buf_len) {
    buf[0] = '\0';

    char *str = buf;
    char **strs = backtrace_symbols(callstack, frames);
    int line_pos;
    size_t len = 0;

    for (int i = 0; i < frames; ++i) {
        int status;
        std::vector<std::string> SplitVec;

#ifdef __APPLE__
        boost::split(SplitVec, strs[i], boost::is_any_of(" \t"),
                     boost::token_compress_on);
        char *demangledName =
            abi::__cxa_demangle(SplitVec[3].c_str(), NULL, NULL, &status);
        line_pos = 5;
#else
        if (i == frames - 1) continue;
        boost::split(SplitVec, strs[i], boost::is_any_of("()"),
                     boost::token_compress_on);
        boost::split(SplitVec, SplitVec[1], boost::is_any_of("+"),
                     boost::token_compress_on);
        char *demangledName =
            abi::__cxa_demangle(SplitVec[0].c_str(), NULL, NULL, &status);
        line_pos = 1;
#endif

        if (status == 0) {
            if (!strstr(demangledName, "boost::") &&
                !strstr(demangledName, "tbb::") &&
                !strstr(demangledName, "BackTrace::") &&
                !strstr(demangledName, "BgpDebug::") &&
                !strstr(demangledName, "testing::")) {
                len = snprintf(str, buf_len - (str - buf),
                               "\t%s+%s\n", demangledName,
                               SplitVec[line_pos].c_str());
                if (len > buf_len - (str - buf)) {

                    // Overflow
                    free(demangledName);
                    str += buf_len - (str - buf);
                    assert((size_t) (str - buf) == buf_len);
                    break;
                }
                str += len;
            }
            free(demangledName);
        }
    }
    free(strs);

    return (str - buf);
}

int BackTrace::Get(void * const* &callstack) {
    callstack = (void * const *) calloc(1024, sizeof(void *));
    return backtrace((void **) callstack, 1024);
}

void BackTrace::Log(void * const* callstack, int frames, std::string msg) {
    char buf[10240];

    ToString(callstack, frames, buf, sizeof(buf));
    std::string s(buf, strlen(buf));
    LOG(DEBUG, msg << ":BackTrace\n" << s);
    free((void *) callstack);
}

void BackTrace::Log(std::string msg) {
    void * const*callstack;

    int frames = Get(callstack);
    Log(callstack, frames, msg);
}
