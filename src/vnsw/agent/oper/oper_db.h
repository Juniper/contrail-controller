/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OPER_OPER_DB_H_
#define SRC_VNSW_AGENT_OPER_OPER_DB_H_

/////////////////////////////////////////////////////////////////////////////
// This file defines set of common agent oper-db class with following
// - Support IFMap dependency tracking.
//      - The IFMapNode for a request is passed in the AgentOperDBData object
//      - AgentOperDBTable::Add allocates a AgentOperDBEntry and sets the
//        AgentOperDBEntry allocated as "state" for IFMapNode passed
//      - AgentOperDBTable::OnChange and AgentOperDBTable::Resync will check
//        if there is change in IFMapNode for the AgentOperDBEntry. If so,
//        it will release state from the previous IFMapNode and set state to
//        the new IFMapNode
//      - AgentOperDBTable::Delete will release the state added for IFMapNode
/////////////////////////////////////////////////////////////////////////////
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
    AgentOperDBData(Agent *agent, IFMapNode *node) :
        AgentData(), ifmap_node_(NULL) {
        SetIFMapNode(agent, node);
    }
    virtual ~AgentOperDBData() { }

    void SetIFMapNode(Agent *agent, IFMapNode *node) {
        if (node == NULL)
            return;
        IFMapDependencyManager *dep = agent->oper_db()->dependency_manager();
        dep->SetState(node);
        ifmap_node_ = node;
    }

    IFMapNode *ifmap_node() const { return ifmap_node_; }
private:
    // IFMap Node pointer for the object
    IFMapNode *ifmap_node_;
};

class AgentOperDBEntry : public AgentDBEntry {
public:
    AgentOperDBEntry() : AgentDBEntry(), ifmap_node_(NULL) { }
    virtual ~AgentOperDBEntry() { }

    IFMapNode *ifmap_node() const { return ifmap_node_; }
private:
    friend class AgentOperDBTable;
    // IFMapNode for the DBEntry
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

    // AgentOperDBTable implements Add/OnChange/Delete/Resync APIs for
    // AgentDBTable. They will in-turn invoke
    // OperDBAdd/OperDBOnChange/OperDBDelete/OperDBResync correspondingly
    virtual DBEntry *OperDBAdd(const DBRequest *req) = 0;
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req) = 0;
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req) = 0;
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req) {
        return false;
    }

    // Default callback handler from IFMap Dependency tracker. We invoke
    // IFNodeToReq to keep all IFMap handling at one place
    virtual void ConfigEventHandler(DBEntry *entry) {
        if (entry == NULL)
            return;

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
    // Handle change in IFMapNode for a DBEntry
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
    virtual DBEntry *Add(const DBRequest *req) {
        AgentOperDBEntry *entry = static_cast<AgentOperDBEntry *>
            (OperDBAdd(req));

        AgentOperDBData *data = static_cast<AgentOperDBData *>(req->data.get());
        if (data && data->ifmap_node())
            UpdateIfMapNode(entry, data->ifmap_node());
        return entry;
    }

    virtual bool OnChange(DBEntry *entry, const DBRequest *req) {
        AgentOperDBEntry *oper_entry = static_cast<AgentOperDBEntry *>(entry);
        bool ret = OperDBOnChange(entry, req);

        AgentOperDBData *data = static_cast<AgentOperDBData *>(req->data.get());
        if (data && data->ifmap_node())
            UpdateIfMapNode(oper_entry, data->ifmap_node());
        return ret;
    }

    virtual bool Resync(DBEntry *entry, const DBRequest *req) {
        AgentOperDBEntry *oper_entry = static_cast<AgentOperDBEntry *>(entry);
        bool ret = OperDBResync(entry, req);

        AgentOperDBData *data = static_cast<AgentOperDBData *>(req->data.get());
        if (data && data->ifmap_node())
            UpdateIfMapNode(oper_entry, data->ifmap_node());
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
