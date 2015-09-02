/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ifmap_server_table_h
#define ctrlplane_ifmap_server_table_h

#include "db/db_graph_base.h"
#include "ifmap/autogen.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_origin.h"

class DBGraph;
class IFMapNode;
class IFMapLink;
class IFMapObject;
class IFMapIdentifier;
class IFMapLinkAttr;

class IFMapServerTable : public IFMapTable {
public:
    struct RequestData : DBRequestData {
        RequestData();
        ~RequestData();
        IFMapOrigin origin;
        std::string id_type;
        std::string id_name;
        std::string metadata;
        std::auto_ptr<AutogenProperty> content;
    };

    IFMapServerTable(DB *db, const std::string &name, DBGraph *graph);

    // Allocate an IFMapNode.
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    // Process a DB add/change/delete request. Requests consist of 1 or 2
    // identifiers plus associated metadata.
    virtual void Input(DBTablePartition *partition, DBClient *client,
                       DBRequest *req);

    // Remove all nodes.
    virtual void Clear();

    // When a property or link has been removed, delete the entry if no longer
    // necessary.
    bool DeleteIfEmpty(IFMapNode *node);
    void Notify(IFMapNode *node);

    static std::string LinkAttrKey(IFMapNode *first, IFMapNode *second);

    void IFMapVmSubscribe(const std::string &vr_name,
        const std::string &vm_name, bool subscribe, bool has_vms);
    void IFMapAddVrVmLink(IFMapNode *vr_node, IFMapNode *vm_node);
    void IFMapRemoveVrVmLink(IFMapNode *vr_node, IFMapNode *vm_node);
    static void RemoveObjectAndDeleteNode(IFMapNode *node,
                                          const IFMapOrigin &origin);

private:
    IFMapNode *EntryLocate(RequestKey *key, bool *changep);
    IFMapNode *EntryLookup(RequestKey *key);

    IFMapObject *LocateObject(IFMapNode *node, IFMapOrigin origin);
    IFMapIdentifier *LocateIdentifier(IFMapNode *node, IFMapOrigin origin,
                                      uint64_t sequence_number);
    IFMapLinkAttr *LocateLinkAttr(IFMapNode *node, IFMapOrigin origin,
                                  uint64_t sequence_number);

    void DeleteNode(IFMapNode *node);

    static IFMapNode *TableEntryLookup(IFMapServerTable *table,
                                       const std::string &id_name);
    static IFMapNode *TableEntryLocate(IFMapServerTable *table,
                                       const std::string &id_name,
                                       bool *changep);

    void LinkNodeAdd(DBGraphBase::edge_descriptor edge,
                     IFMapNode *first, IFMapNode *second,
                     const std::string &metadata, uint64_t sequence_number,
                     const IFMapOrigin &origin);
    void LinkNodeDelete(IFMapNode *first, IFMapNode *second,
                        const IFMapOrigin &origin);
    void LinkNodeUpdate(IFMapLink *link, uint64_t sequence_number,
                        const IFMapOrigin &origin);
    void IFMapProcVmSubscribe(const std::string &vr_name,  
                              const std::string &vm_name);
    void IFMapProcVmUnsubscribe(const std::string &vr_name,  
                                const std::string &vm_name, bool has_vms);

    DBGraph *graph_;
};

#endif
