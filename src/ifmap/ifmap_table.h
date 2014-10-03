/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ifmap_table_h
#define ctrlplane_ifmap_table_h

#include "db/db_table.h"

class IFMapNode;
class IFMapNodeTableListShowEntry;
class IFMapObject;

class IFMapTable : public DBTable {
public:
    static const int kPartitionCount = 1;
    struct RequestKey : DBRequestKey {
        std::string id_type;
        std::string id_name;
        uint64_t id_seq_num;
    };

    IFMapTable(DB *db, const std::string &name);

    virtual int PartitionCount() const {
        return kPartitionCount;
    }

    virtual const char *Typename() const = 0;

    // Allocate an IFMapObject
    virtual IFMapObject *AllocObject() = 0;

    IFMapNode *FindNode(const std::string &name);

    virtual void Clear() = 0;

    static IFMapTable *FindTable(DB  *db, const std::string &element_type);
    static void ClearTables(DB *db);
    static void FillNodeTableList(DB *db,
        std::vector<IFMapNodeTableListShowEntry> *table_list);
};
#endif
