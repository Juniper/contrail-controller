/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_table_h
#define ctrlplane_table_h

#include "db/db_table.h"

// Table interface.
// A Routing table is part of a database.
class RouteTable : public DBTable {
public:
    RouteTable(DB *db, const std::string &name) : DBTable(db, name) { }

private:
    DISALLOW_COPY_AND_ASSIGN(RouteTable);
};

#endif
