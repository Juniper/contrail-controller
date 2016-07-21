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
#include <ifmap/ifmap_node.h>

struct AgentOperDBKey : public AgentKey {
    AgentOperDBKey() : AgentKey() { }
    AgentOperDBKey(DBSubOperation sub_op) : AgentKey(sub_op) { }
    virtual ~AgentOperDBKey() { }
};

struct AgentOperDBData : public AgentData {

    AgentOperDBData(const Agent *agent, IFMapNode *node) :
        AgentData(), agent_(agent) {
        SetIFMapNode(node);
    }
    virtual ~AgentOperDBData() {
        SetIFMapNode(NULL);
    }


    void SetIFMapNode(IFMapNode *node) {

        if (node == NULL) {
            ifmap_node_state_ = NULL;
            return;
        }

        assert(agent_);

        // We dont allow changing the node
        assert(!ifmap_node_state_);

        IFMapDependencyManager *dep = agent_->oper_db()->dependency_manager();
        ifmap_node_state_ = dep->SetState(node);
    }

    IFMapNode *ifmap_node() const {
        if (!ifmap_node_state_)
            return NULL;
        IFMapNodeState *state = ifmap_node_state_.get();
        return state->node();
    }
    const Agent *agent() const {
        return agent_;
    }
private:
    const Agent *agent_;
    // IFMap Node pointer for the object
    IFMapDependencyManager::IFMapNodePtr ifmap_node_state_;
};

class AgentOperDBEntry : public AgentDBEntry {
public:
    AgentOperDBEntry() : AgentDBEntry()  { }
    virtual ~AgentOperDBEntry() { }

    IFMapNode *ifmap_node() const {
        if (!ifmap_node_state_)
            return NULL;
        IFMapNodeState *state = ifmap_node_state_.get();
        return state->node();
    }

    void SetIFMapNodeState(IFMapDependencyManager::IFMapNodePtr sref) {
        ifmap_node_state_  = sref;
    }
private:
    friend class AgentOperDBTable;
    IFMapDependencyManager::IFMapNodePtr ifmap_node_state_;
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

    virtual void ConfigEventHandler(IFMapNode *node, DBEntry *entry) {
        DBRequest req;
        IFMapDependencyManager *dep = agent()->oper_db()->dependency_manager();
        boost::uuids::uuid new_uuid = boost::uuids::nil_uuid();
        bool uuid_set = IFNodeToUuid(node, new_uuid);
        IFMapNodeState *state = dep->IFMapNodeGet(node);
        boost::uuids::uuid old_uuid = state->uuid();

        if (!node->IsDeleted()) {
            if (entry) {
                if ((old_uuid != new_uuid)) {
                    if (old_uuid != boost::uuids::nil_uuid()) {
                        req.oper = DBRequest::DB_ENTRY_DELETE;
                        if (IFNodeToReq(node, req, old_uuid) == true) {
                            assert(req.oper == DBRequest::DB_ENTRY_DELETE);
                            Enqueue(&req);
                        }
                    }
                }
            }
            if (uuid_set && dep->IsNodeIdentifiedByUuid(node)) {
                assert(new_uuid != boost::uuids::nil_uuid());
            }
            state->set_uuid(new_uuid);
            state->set_oper_db_request_enqueued(true);
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        } else {
            req.oper = DBRequest::DB_ENTRY_DELETE;
            if (uuid_set && (old_uuid == boost::uuids::nil_uuid()) &&
                (dep->IsNodeIdentifiedByUuid(node))) {
                //Node was never added so no point sending delete
                return;
            }
            new_uuid = old_uuid;
            if (state) {
                state->set_oper_db_request_enqueued(false);
            }
        }

        if (IFNodeToReq(node, req, new_uuid) == true) {
            Enqueue(&req);
        }

        return;
    }

protected:
    // Handle change in IFMapNode for a DBEntry
    void UpdateIfMapNode(AgentOperDBEntry *entry, IFMapNode *node) {
        if (entry == NULL)
            return;

        IFMapNode *old_node = entry->ifmap_node();

        if (old_node == node)
            return;

        IFMapDependencyManager *dep = agent()->oper_db()->dependency_manager();
        if (old_node) {
            dep->SetObject(old_node, NULL);
            entry->SetIFMapNodeState(NULL);
        }

        if (node) {
            entry->SetIFMapNodeState(dep->SetState(node));
            dep->SetObject(node, entry);
        }
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
