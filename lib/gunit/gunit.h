#ifndef __TESTING_GUNIT_H__
#define __TESTING_GUNIT_H__

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#pragma clang diagnostic ignored "-Wlogical-op-parentheses"
#endif

#if defined(__GNUC__)
#include "base/compiler.h"
#if __GCC_HAS_PRAGMA > 0
#if __GCC_HAS_DIAGNOSTIC_PUSH > 0
#pragma GCC diagnostic push
#endif
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#endif

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#if defined(__GNUC__) && (__GNUC_HAS_PRAGMA > 0) && \
    (__GNUC_HAS_DIAGNOSTIC_PUSH > 0)
#pragma GCC diagnostic pop
#endif

#endif
