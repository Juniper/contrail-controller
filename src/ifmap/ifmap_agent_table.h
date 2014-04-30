/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ifmap_agent_table_h
#define ctrlplane_ifmap_agent_table_h

#include "db/db_graph_base.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_object.h"
#include "boost/asio/io_service.hpp"
#include "base/timer.h"
#include "base/task.h"
#include "sandesh/sandesh_trace.h"

#define IFMAP_AGENT_LINK_DB_NAME "__ifmap_agentlinkdata__.0"
extern SandeshTraceBufferPtr IFMapAgentTraceBuf; 

#define IFMAP_AGENT_TRACE(obj, ...) do {                                      \
    if (LoggingDisabled()) break;                                              \
    IFMapAgent##obj::TraceMsg(IFMapAgentTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (0);


class DBGraph;
class IFMapNode;


class IFMapAgentTable : public IFMapTable {
public:
    struct IFMapAgentData : DBRequestData {
        std::auto_ptr<IFMapObject>content;
    };
    typedef boost::function<bool(DBTable *table, IFMapNode *node, DBRequest *req)> PreFilterFn;

    IFMapAgentTable(DB *db, const std::string &name, DBGraph *graph);

    virtual void Input(DBTablePartition *partition, DBClient *client,
                       DBRequest *req);
    virtual void Clear();

     // Allocate an IFMapNode.
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    
    // Allocate an IFMapObject 
    virtual IFMapObject *AllocObject() = 0;
    static IFMapNode *TableEntryLookup(DB *db, RequestKey *key);
    void NotifyNode(IFMapNode *node);
    DBGraph *GetGraph() const {return graph_;};
    void DeleteNode(IFMapNode *node);
    void RegisterPreFilter(PreFilterFn fn) {pre_filter_ = fn;};

private:
    IFMapNode *EntryLocate(IFMapNode *node, RequestKey *key);
    IFMapNode *EntryLookup(RequestKey *key);
    IFMapAgentTable* TableFind(const std::string &node_name);
    void HandlePendingLinks(IFMapNode *);
    DBGraph *graph_; 
    PreFilterFn pre_filter_;
};

class IFMapAgentLinkTable : public IFMapLinkTable {
public:
    struct RequestKey : DBRequestKey {
        IFMapTable::RequestKey left_key;
        IFMapTable::RequestKey right_key;
    };

    class comp {
        public:
            bool operator()(const IFMapTable::RequestKey &left, 
                            const IFMapTable::RequestKey &right) const {
                if (left.id_type != right.id_type) 
                    return left.id_type < right.id_type;

                return left.id_name < right.id_name;
            }
    };

    IFMapAgentLinkTable(DB *db, const std::string &name, DBGraph *graph);
    typedef std::map<IFMapTable::RequestKey, std::list<IFMapTable::RequestKey> *, comp> LinkDefMap;
    virtual void Input(DBTablePartition *partition, DBClient *client,
                       DBRequest *req);
    void IFMapAgentLinkTable_Init(DB *db, DBGraph *graph);
    static DBTable *CreateTable(DB *db, const std::string &name,
                                     DBGraph *graph);
    void EvalDefLink(IFMapTable::RequestKey *key);
    void RemoveDefListEntry(LinkDefMap *map, LinkDefMap::iterator &map_it,
                            std::list<IFMapTable::RequestKey>::iterator *list_it);
    void DestroyDefLink();
    const LinkDefMap &GetLinkDefMap() const {
        return link_def_map_;
    }
    void DelLink(IFMapNode *first, IFMapNode *second, DBGraphEdge *edge);
    void LinkDefAdd(DBRequest *request);
private:
    void AddLink(DBGraphBase::edge_descriptor edge,
                                   IFMapNode *left, IFMapNode *right, uint64_t seq);
    DBGraph *graph_; 
    LinkDefMap link_def_map_;
};


class IFMapAgentStaleCleaner {
public:
    IFMapAgentStaleCleaner(DB *db, DBGraph *graph, boost::asio::io_service &io_service);
    ~IFMapAgentStaleCleaner();
    class IFMapAgentStaleCleanerWorker;
    void Clear();
    bool StaleTimeout();

private:
    DB *db_;
    DBGraph *graph_;
    uint64_t seq_;
};

extern void IFMapAgentLinkTable_Init(DB *db, DBGraph *graph);
#endif
