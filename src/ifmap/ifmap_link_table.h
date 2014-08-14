/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_link_table__
#define __ctrlplane__ifmap_link_table__

#include "db/db_graph_base.h"
#include "db/db_table.h"

struct IFMapOrigin;
class DBGraph;
class IFMapLink;
class IFMapNode;
class IFMapTable;

class IFMapLinkTable : public DBTable {
public:
    struct RequestKey : DBRequestKey {
        DBGraphBase::edge_descriptor edge;
    };
    struct RequestData : DBRequestData {
        // Intentionally left blank.
    };

    IFMapLinkTable(DB *db, const std::string &name, DBGraph *graph);

    // The Link table is modified from the IFMapTable code directly. Its input
    // method should never be called.
    virtual void Input(DBTablePartition *partition, DBClient *client,
                       DBRequest *req);

    void AddLink(DBGraphBase::edge_descriptor edge, IFMapNode *left,
                 IFMapNode *right, const std::string &metadata,
                 uint64_t sequence_number, const IFMapOrigin &origin);
    void DeleteLink(DBGraphEdge *edge, IFMapNode *lhs, IFMapNode *rhs);
    void DeleteLink(IFMapNode *lhs, IFMapNode *rhs, const IFMapOrigin &origin);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    void Clear();

    static DBTable *CreateTable(DB *db, const std::string &name,
                                DBGraph *graph);
    IFMapLink *FindLink(DBGraphBase::edge_descriptor edge);

protected:
    void DeleteLink(DBGraphEdge *edge);

private:
    DBGraph *graph_;
};

extern void IFMapLinkTable_Init(DB *db, DBGraph *graph);
extern void IFMapLinkTable_Clear(DB *db);

#endif /* defined(__ctrlplane__ifmap_link_table__) */
