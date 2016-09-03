/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_graph_table_h
#define ctrlplane_db_graph_table_h

#include "db/db_graph.h"
#include "db/db_table.h"

class DBGraphTable : public DBTable {
public:
    DBGraphTable(DB *db, const std::string &name, DBGraph *graph) :
        DBTable(db, name), graph_(graph) {
    }
    const DBGraph *graph() const { return graph_; }
    DBGraph *graph() { return graph_; }

private:

    DBGraph *graph_;
};

#endif
