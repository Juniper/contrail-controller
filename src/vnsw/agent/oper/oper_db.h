/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OPER_OPER_DB_H_
#define SRC_VNSW_AGENT_OPER_OPER_DB_H_

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>
#include <operdb_init.h>
#include <ifmap_dependency_manager.h>

struct AgentOperDBKey : public AgentKey {
    AgentOperDBKey() : AgentKey() { }
    AgentOperDBKey(DBSubOperation sub_op) : AgentKey(sub_op) { }
    virtual ~AgentOperDBKey() { }
};

struct AgentOperDBData : public AgentData {
    AgentOperDBData(IFMapNode *node) : AgentData(), ifmap_node_(node) { }
    virtual ~AgentOperDBData() { }

    IFMapNode *ifmap_node_;
};

class AgentOperDBEntry : public AgentDBEntry {
public:
    AgentOperDBEntry() : AgentDBEntry(), ifmap_node_(NULL) { }
    virtual ~AgentOperDBEntry() { }

    IFMapNode *ifmap_node() const { return ifmap_node_; }
private:
    friend class AgentOperDBTable;
    IFMapNode *ifmap_node_;
    DISALLOW_COPY_AND_ASSIGN(AgentOperDBEntry);
};

class AgentOperDBTable : public AgentDBTable {
public:
    AgentOperDBTable(DB *db, const std::string &name) :
        AgentDBTable(db, name) {
    }

    AgentOperDBTable(DB *db, const std::string &name,
                     bool del_on_zero_refcount) :
        AgentDBTable(db, name, del_on_zero_refcount) {
    }
    virtual ~AgentOperDBTable() { }

    virtual DBEntry *OperDBAdd(const DBRequest *req) = 0;
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req) = 0;
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req) = 0;
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req) {
        return false;
    }

    void ConfigEventHandler(DBEntry *entry) {
        AgentOperDBEntry *agent_entry = static_cast<AgentOperDBEntry *>(entry);
        if (agent_entry->ifmap_node_) {
            DBRequest req;
            if (IFNodeToReq(agent_entry->ifmap_node_, req) == true) {
                Enqueue(&req);
            }
        }
        return;
    }

protected:
    void UpdateIfMapNode(AgentOperDBEntry *entry, IFMapNode *node) {
        if (entry == NULL)
            return;

        if (entry->ifmap_node_ == node)
            return;

        IFMapDependencyManager *dep = agent()->oper_db()->dependency_manager();
        if (entry->ifmap_node_ != NULL)
            dep->ResetObject(entry->ifmap_node_);

        entry->ifmap_node_ = node;
        if (entry->ifmap_node_)
            dep->SetObject(node, entry);
    }

    // Implement Add, Delete, OnChange to provide common agent functionality
    // including,
    // - Set IFMap node for a DBEntry
    virtual DBEntry *Add(const DBRequest *req) {
        AgentOperDBEntry *entry = static_cast<AgentOperDBEntry *>
            (OperDBAdd(req));

        AgentOperDBData *data = static_cast<AgentOperDBData *>(req->data.get());
        if (data && data->ifmap_node_)
            UpdateIfMapNode(entry, data->ifmap_node_);
        return entry;
    }

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        AgentOperDBEntry *oper_entry = static_cast<AgentOperDBEntry *>(entry);
        bool ret = OperDBOnChange(entry, req);

        AgentOperDBData *data = static_cast<AgentOperDBData *>(req->data.get());
        if (data && data->ifmap_node_)
            UpdateIfMapNode(oper_entry, data->ifmap_node_);
        return ret;
    }

    virtual bool Resync(DBEntry *entry, DBRequest *req) {
        AgentOperDBEntry *oper_entry = static_cast<AgentOperDBEntry *>(entry);
        bool ret = OperDBResync(entry, req);

        AgentOperDBData *data = static_cast<AgentOperDBData *>(req->data.get());
        if (data && data->ifmap_node_)
            UpdateIfMapNode(oper_entry, data->ifmap_node_);
        return ret;
    }

    virtual bool Delete(DBEntry *entry, const DBRequest *req) {
        AgentOperDBEntry *oper_entry = static_cast<AgentOperDBEntry *>(entry);
        bool ret = OperDBDelete(entry, req);
        UpdateIfMapNode(oper_entry, NULL);
        return ret;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(AgentOperDBTable);
};

#endif  // SRC_VNSW_AGENT_OPER_OPER_DB_H_
