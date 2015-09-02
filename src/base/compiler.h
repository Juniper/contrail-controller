/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE_COMPILER_H__
#define __BASE_COMPILER_H__

#ifdef __GNUC__

#ifdef __linux__
#include <features.h>
#define __GCC_HAS_PRAGMA        __GNUC_PREREQ(4, 4)
#define __GCC_HAS_DIAGNOSTIC_PUSH	__GNUC_PREREQ(4, 6)

#elif __APPLE__
#define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#define __GCC_HAS_PRAGMA        1

#elif __FreeBSD__
#include <sys/cdefs.h>
#define __GNUC_PREREQ    __GNUC_PREREQ__
#define __GCC_HAS_PRAGMA __GNUC_PREREQ__(4, 4)
#endif  // OS

#endif  // __GNUC__

#endif
