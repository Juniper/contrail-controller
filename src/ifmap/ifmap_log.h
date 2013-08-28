/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_LOG_H__
#define __IFMAP_LOG_H__

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"

#define IFMAP_DEBUG(obj, ...) \
if (!LoggingDisabled()) \
    obj::Send(g_vns_constants.CategoryNames.find(Category::IFMAP)->second, \
              SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__)

#define IFMAP_WARN(obj, ...) \
if (!LoggingDisabled()) \
    obj::Send(g_vns_constants.CategoryNames.find(Category::IFMAP)->second, \
              SandeshLevel::SYS_WARN, __FILE__, __LINE__, ##__VA_ARGS__)

#endif // __IFMAP_LOG_H__
