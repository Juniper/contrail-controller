/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <list>
#include <map>

#include <boost/uuid/string_generator.hpp>
#include <boost/function.hpp>

#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <ifmap/ifmap_agent_table.h>
#include <vnc_cfg_types.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>

#include <cfg/cfg_listener.h>
#include <cfg/cfg_init.h>

using namespace std;
using namespace autogen;

CfgListener::CfgListener(AgentConfig *cfg) : agent_cfg_(cfg) 
{ 
}

//  Config listener module. 
//
//  For a IFMapNode notification, there are 2 kinds of listeners
//      DBTable Listener : There is a DBTable in agent for a IFNode. 
//                     This is represented by CfgTableListenerInfo structure
//      Non-DBTable Listener : Used when there is no DBTable for the IFNode.
//                         This is represented by CfgListenerInfo structure
//
//  On IFMapLink notification, the registered callback is invoked for both
//  left and right nodes

void CfgListener::Init() {
    DBTableBase *link_db = agent_cfg_->agent()->GetDB()->FindTable(IFMAP_AGENT_LINK_DB_NAME);
    assert(link_db);

    DBTableBase::ListenerId id = 
        link_db->Register(boost::bind(&CfgListener::LinkListener, this, _1, _2));

    pair<CfgListenerIdMap::iterator, bool> result_id =
            cfg_listener_id_map_.insert(make_pair(link_db, id));
    assert(result_id.second);
}

void CfgListener::Shutdown() {

    for (CfgListenerIdMap::iterator iter = cfg_listener_id_map_.begin();
         iter != cfg_listener_id_map_.end(); ++iter) {

        DBTableBase *table = iter->first;
        table->Unregister(iter->second);
    }

    cfg_listener_map_.clear();
    cfg_listener_cb_map_.clear();
    cfg_listener_id_map_.clear();
}

// Get CfgDBState set for an IFNode
CfgDBState *CfgListener::GetCfgDBState(IFMapTable *table, DBEntryBase *dbe,
                                       DBTableBase::ListenerId &id) {
    CfgListener *listener = agent_cfg_->cfg_listener();
    CfgListenerIdMap::iterator it = listener->cfg_listener_id_map_.find(table);
    if (it == listener->cfg_listener_id_map_.end()) {
        return NULL;
    }

    id = it->second;
    return static_cast<CfgDBState *>(dbe->GetState(table, id));
}

// When traversing graph, check if an IFMapNode can be used. Conditions are,
// - The node is not in deleted state
// - The node was notified earlier
bool CfgListener::CanUseNode(IFMapNode *node) {
    if (node->IsDeleted()) {
        return false;
    }

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    CfgListenerIdMap::iterator it = cfg_listener_id_map_.find(table);
    if (it == cfg_listener_id_map_.end()) {
        return true;
    }

    DBTableBase::ListenerId id = it->second;
    CfgDBState *state = static_cast<CfgDBState *>(node->GetState(table, id));
    if (state == NULL) {
        return false;
    }

    return true;
}

// When traversing graph, check if an IFMapNode can be used. Conditions are,
// - The node is not in deleted state
// - The node was notified earlier
// - The node is an entry in IFMapAgentTable specified
bool CfgListener::CanUseNode(IFMapNode *node, IFMapAgentTable *table) {
    if (table != static_cast<IFMapAgentTable *>(node->table())) {
        return false;
    }

    return CanUseNode(node);
}

bool CfgListener::SkipNode(IFMapNode *node) {
    return !CanUseNode(node);
}

bool CfgListener::SkipNode(IFMapNode *node, IFMapAgentTable *table) {
    return !CanUseNode(node, table);
}

// Set seen-state for a DBEntry
void CfgListener::UpdateSeenState(DBTableBase *table, DBEntryBase *dbe,
                                  CfgDBState *s, DBTableBase::ListenerId id) {
    CfgDBState *state = static_cast<CfgDBState *>(s);
    if (state == NULL) {
        state = new CfgDBState();
        dbe->SetState(table, id, state);
    }
    state->notify_count_++;
}

// Find the AgentDBTable registered for an IFMapNode.
// When configured, will skip notification of IFMapNode as long as ID_PERMS
// is not got
AgentDBTable* CfgListener::GetOperDBTable(IFMapNode *node) {
    IFMapTable *cfg_table = node->table();
    std::string table_name = cfg_table->Typename();

    CfgListenerMap::const_iterator loc = cfg_listener_map_.find(table_name);
    if (loc == cfg_listener_map_.end()) {
        return NULL;
    }
    const CfgTableListenerInfo *info = &loc->second;
    // Check for presence of ID_PERMS if configured
    if (info->need_property_id_ > 0) {
        const IFMapObject *obj = node->GetObject();
        const IFMapIdentifier *id = static_cast<const IFMapIdentifier *>(obj);
        if (id->IsPropertySet(info->need_property_id_) == false) {
            return NULL;
        }
    }

    return info->table_;
}

// Find the Callback function registered for an IFMapNode.
// When configured, will skip notification of IFMapNode as long as ID_PERMS
// is not got
CfgListener::NodeListenerCb CfgListener::GetCallback(IFMapNode *node) {
    IFMapTable *cfg_table = node->table();
    std::string table_name = cfg_table->Typename();

    CfgListenerCbMap::const_iterator it = cfg_listener_cb_map_.find(table_name);
    if (it == cfg_listener_cb_map_.end()) {
        return NULL;
    }

    const CfgListenerInfo *info = &it->second;
    // Check for presence of ID_PERMS if configured
    if (info->need_property_id_ > 0) {
        const IFMapObject *obj = node->GetObject();
        const IFMapIdentifier *id = static_cast<const IFMapIdentifier *>(obj);
        if (id->IsPropertySet(info->need_property_id_) == false) {
            return NULL;
        }
    }
    return info->cb_;
}

void CfgListener::LinkNotify(IFMapNode *node, CfgDBState *state,
                             DBTableBase::ListenerId id) {
    if (node == NULL) {
        return;
    }

    if (node->IsDeleted()) {
        return;
    }

    AgentDBTable *oper_table = GetOperDBTable(node);
    if (oper_table && oper_table->CanNotify(node)) {
        UpdateSeenState(node->table(), node, state, id);
        NodeNotify(oper_table, node);
        return;
    } 
    
    NodeListenerCb cb = GetCallback(node);
    if (cb != NULL) {
        UpdateSeenState(node->table(), node, state, id);
        cb(node);
        return;
    } 

    return;
}

void CfgListener::LinkListener(DBTablePartBase *partition, DBEntryBase *dbe) {
    IFMapLink *link = static_cast<IFMapLink *>(dbe);
    DBTableBase::ListenerId lid = 0;
    DBTableBase::ListenerId rid = 0;
    CfgDBState *lstate = NULL;
    CfgDBState *rstate = NULL;

    IFMapNode *lnode = link->LeftNode(agent_cfg_->agent()->GetDB());
    if (lnode) {
        lstate = GetCfgDBState(lnode->table(), lnode, lid);
    }

    IFMapNode *rnode = link->RightNode(agent_cfg_->agent()->GetDB());
    if (rnode) {
        rstate = GetCfgDBState(rnode->table(), rnode, rid);
    }

    // DB Table does not guarantee ordering of client notifications. So, we may
    // end-up in following sequence
    // (1)Node-A, (2)Link <Node-A, Node-B>, (3) Node-B
    //
    // In the above sequence, graph-walk of Node-AA will always skip Node-B
    // since Node-B was not notified and CanUseNode(Node-B) will return false.
    //
    // A solution here is to Node-B before Node-A.
    //
    // Summary: 
    //     If left node is not notified, notify left followed by right.
    //     If right node is not notified, notify right followed by left.
    //     Else, notify left followed by right
    if (lstate != NULL && rstate != NULL) {
        LinkNotify(lnode, lstate, lid);
        LinkNotify(rnode, rstate, rid);
    } else if (lstate == NULL) {
        LinkNotify(lnode, lstate, lid);
        LinkNotify(rnode, rstate, rid);
    } else if (rstate == NULL) {
        LinkNotify(rnode, rstate, rid);
        LinkNotify(lnode, lstate, lid);
    }
}

void CfgListener::NodeNotify(AgentDBTable *oper_table, IFMapNode *node) {
    DBRequest req;
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    }

    //Convert to RequestKey and RequestData
    if (oper_table->IFNodeToReq(node, req)) {
        // Enque it to oper db
        oper_table->Enqueue(&req);
    }
}

void CfgListener::NodeListener(DBTablePartBase *partition, DBEntryBase *dbe) {
    IFMapNode *node = static_cast <IFMapNode *> (dbe);
    AgentDBTable *oper_table = GetOperDBTable(node);

    if (oper_table == NULL)
        return;

    DBTableBase::ListenerId id = 0;
    IFMapTable *table = static_cast<IFMapTable *>(partition->parent());
    CfgDBState *state = GetCfgDBState(table, dbe, id);
    if (dbe->IsDeleted()) {
        if (state) {
            NodeNotify(oper_table, node);
            dbe->ClearState(table, id);
            delete state;
        }
    } else {
        if (oper_table->CanNotify(node)) {
            UpdateSeenState(partition->parent(), dbe, state, id);
            NodeNotify(oper_table, node);
        }
    }
}

void CfgListener::NodeCallback(DBTablePartBase *partition, DBEntryBase *dbe) {
    IFMapNode *node = static_cast <IFMapNode *> (dbe);
    NodeListenerCb cb = GetCallback(node);

    if (cb == NULL) {
        return;
    }

    DBTableBase::ListenerId id = 0;
    IFMapTable *table = static_cast<IFMapTable *>(partition->parent());
    CfgDBState *state = GetCfgDBState(table, dbe, id);
    if (dbe->IsDeleted()) {
        if (state) {
            cb(node);
            dbe->ClearState(table, id);
            delete state;
        }
    } else {
        UpdateSeenState(partition->parent(), dbe, state, id);
        cb(node);
    }
}

void CfgListener::NodeReSync(IFMapNode *node) {
    AgentDBTable *oper_table = GetOperDBTable(node);

    if (oper_table != NULL && node->IsDeleted() == false) {
        //First notify the node
        IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
        CfgListenerIdMap::iterator it = cfg_listener_id_map_.find(table);
        if (it == cfg_listener_id_map_.end()) {
            return;
        }
        DBTableBase::ListenerId id = it->second;
        CfgDBState *state = GetCfgDBState(table, node, id);

        UpdateSeenState(node->table(), node, state, id);
        NodeNotify(oper_table, node);
    }

    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table()); 
    for (DBGraphVertex::adjacency_iterator iter = 
            node->begin(table->GetGraph());  
            iter != node->end(table->GetGraph()); ++iter) { 
        if (iter->IsDeleted()) { 
            continue; 
        } 

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        oper_table = GetOperDBTable(adj_node);
        if (SkipNode(adj_node)) {
            continue;
        }

        if (oper_table != NULL){
            NodeNotify(oper_table, adj_node);
        } else {
            NodeListenerCb cb = GetCallback(adj_node);
            if (cb != NULL) {
                cb(adj_node);
            }
        }
    }
}

void CfgListener::Register(std::string type, AgentDBTable *table,
                           int need_property_id) {
    CfgTableListenerInfo info = {table, need_property_id};
    pair<CfgListenerMap::iterator, bool> result =
            cfg_listener_map_.insert(make_pair(type, info));
    assert(result.second);

    DBTableBase *cfg_db = IFMapTable::FindTable(agent_cfg_->agent()->GetDB(), type);
    assert(cfg_db);
    DBTableBase::ListenerId id = cfg_db->Register
        (boost::bind(&CfgListener::NodeListener, this, _1, _2));
    pair<CfgListenerIdMap::iterator, bool> result_id =
            cfg_listener_id_map_.insert(make_pair(cfg_db, id));
    assert(result_id.second);
}

void CfgListener::Register(std::string type, NodeListenerCb callback,
                           int need_property_id) {
    CfgListenerInfo info = {callback, need_property_id};
    pair<CfgListenerCbMap::iterator, bool> result =
            cfg_listener_cb_map_.insert(make_pair(type, info));
    assert(result.second);

    DBTableBase *cfg_db = IFMapTable::FindTable(agent_cfg_->agent()->GetDB(), type);
    assert(cfg_db);
    DBTableBase::ListenerId id = cfg_db->Register
        (boost::bind(&CfgListener::NodeCallback, this, _1, _2));
    pair<CfgListenerIdMap::iterator, bool> result_id =
            cfg_listener_id_map_.insert(make_pair(cfg_db, id));
    assert(result_id.second);
}

void CfgListener::Unregister(std::string type) {
    cfg_listener_map_.erase(type);
    cfg_listener_cb_map_.erase(type);

    DBTableBase *cfg_db = IFMapTable::FindTable(agent_cfg_->agent()->GetDB(), type);
    CfgListenerIdMap::iterator iter = cfg_listener_id_map_.find(cfg_db);
    if (iter != cfg_listener_id_map_.end()) {
        cfg_db->Unregister(iter->second);
        cfg_listener_id_map_.erase(iter);
    }
}
