/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_link_table__
#define __ctrlplane__ifmap_link_table__

#include "db/db_graph_base.h"
#include "db/db_graph_table.h"

struct IFMapOrigin;
class DBGraph;
class IFMapLink;
class IFMapNode;
class IFMapTable;

class IFMapLinkTable : public DBGraphTable {
public:
    static const int kPartitionCount = 1;
    struct RequestKey : DBRequestKey {
        std::string name;
    };
    struct RequestData : DBRequestData {
        // Intentionally left blank.
    };

    IFMapLinkTable(DB *db, const std::string &name, DBGraph *graph);

    virtual int PartitionCount() const {
        return kPartitionCount;
    }

    // The Link table is modified from the IFMapTable code directly. Its input
    // method should never be called.
    virtual void Input(DBTablePartition *partition, DBClient *client,
                       DBRequest *req);

    std::string LinkKey(const std::string &metadata, IFMapNode *left,
                        IFMapNode *right);
    IFMapLink *AddLink(IFMapNode *left, IFMapNode *right,
                       const std::string &metadata, uint64_t sequence_number,
                       const IFMapOrigin &origin);
    void DeleteLink(IFMapLink *link, const IFMapOrigin &origin);
    void DeleteLink(IFMapLink *link);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    void Clear();

    static DBTable *CreateTable(DB *db, const std::string &name,
                                DBGraph *graph);
    IFMapLink *FindLink(const std::string &metadata, IFMapNode *left, IFMapNode *right);
    IFMapLink *FindLink(const std::string &name);
    IFMapLink *FindNextLink(const std::string &name);
};

extern void IFMapLinkTable_Init(DB *db, DBGraph *graph);
extern void IFMapLinkTable_Clear(DB *db);

#endif /* defined(__ctrlplane__ifmap_link_table__) */
