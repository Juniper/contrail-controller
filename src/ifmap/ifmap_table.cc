/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_table.h"

#include <boost/algorithm/string.hpp>
#include "db/db.h"
#include "db/db_table.h"
#include "ifmap/autogen.h"
#include "ifmap/ifmap_node.h"

using namespace std;

IFMapTable::IFMapTable(DB *db, const std::string &name) : DBTable(db, name) {
}

IFMapNode *IFMapTable::FindNode(const std::string &name) {
    IFMapTable::RequestKey reqkey;
    reqkey.id_name = name;
    auto_ptr<DBEntry> key(AllocEntry(&reqkey));
    return static_cast<IFMapNode *>(Find(key.get()));
}

IFMapTable *IFMapTable::FindTable(DB *db, const std::string &element_type) {
    string idtype = element_type;
    boost::replace_all(idtype, "-", "_");
    string name = "__ifmap__." + idtype + ".0";
    return static_cast<IFMapTable *>(db->FindTable(name));
}

void IFMapTable::ClearTables(DB *db) {
    for (DB::iterator iter = db->lower_bound("__ifmap__.");
         iter != db->end(); ++iter) {
        if (iter->first.find("__ifmap__.") != 0) {
            break;
        }
        IFMapTable *table = static_cast<IFMapTable *>(iter->second);
        table->Clear();
    }
}
