/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VIZD_TABLE_DESC_H__
#define __VIZD_TABLE_DESC_H__

#include "gendb_if.h"

extern std::vector<GenDb::NewCf> vizd_tables;
extern std::vector<GenDb::NewCf> vizd_flow_tables;

void init_vizd_tables();

#endif // __VIZD_TABLE_DESC_H__
