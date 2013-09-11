/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "ifmap/ifmap_table.h"

#include <boost/algorithm/string.hpp>
#include "db/db.h"
#include "db/db_table.h"
#include "ifmap/autogen.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_show_types.h"

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

void IFMapTable::FillNodeTableList(DB *db,
        std::vector<IFMapNodeTableListShowEntry> *table_list) {
    for (DB::const_iterator iter = db->const_lower_bound("__ifmap__");
         iter != db->end(); ++iter) {
        DBTable *table = static_cast<DBTable *>(iter->second);
        if (table->name().compare("__ifmap_metadata__.0") == 0) {
            // Ignore the link-table in this api
            continue;
        }
        // Create a name that can be passed to IFMapTable::FindTable()
        size_t first = table->name().find_first_of(".");
        size_t second = table->name().find_first_of(".", first + 1);
        std::string name = table->name().substr(first + 1, second - first - 1);
        boost::replace_all(name, "_", "-");

        IFMapNodeTableListShowEntry entry;
        entry.table_name = name;

        table_list->push_back(entry);
        if (table->name().find("__ifmap__") != 0) {
            break;
        }
    }
}
