/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_QUERY_ENGINE_UTILS_H_
#define SRC_QUERY_ENGINE_UTILS_H_

#include <stdint.h>

bool parse_time(const std::string& relative_time, uint64_t *usec_time);


#endif //SRC_QUERY_ENGINE_UTILS_H_

