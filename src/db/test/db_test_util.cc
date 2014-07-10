/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/test/db_test_util.h"

#include "base/test/task_test_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_table.h"

namespace db_util {

void Clear(DB *db) {
    IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
                db->FindTable("__ifmap_metadata__.0"));
    if (ltable) {
        ltable->Clear();
    }
    IFMapTable::ClearTables(db);

    task_util::WaitForIdle();
    db->Clear();
}

}
