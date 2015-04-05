/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/ifmap_dependency_manager.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>

#include "base/task.h"
#include "base/task_trigger.h"
#include "db/db.h"
#include "db/db_table_partition.h"
#include "db/db_entry.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_dependency_tracker.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace boost::assign;
using namespace std;

void intrusive_ptr_add_ref(IFMapNodeState *state) {
    ++state->refcount_;
}

void intrusive_ptr_release(IFMapNodeState *state) {
    if (--state->refcount_ ==  0) {
        assert(state->object_ == NULL);
        state->manager_->IFMapNodeReset(state->node_);
        delete state;
    }
}

IFMapDependencyManager::IFMapDependencyManager(DB *database, DBGraph *graph)
        : database_(database),
          graph_(graph) {
    tracker_.reset(
        new IFMapDependencyTracker(
            database, graph,
            boost::bind(&IFMapDependencyManager::ChangeListAdd, this, _1)));
    int task_id = TaskScheduler::GetInstance()->GetTaskId("db::DBTable");
    trigger_.reset(
        new TaskTrigger(
            boost::bind(&IFMapDependencyManager::ProcessChangeList, this),
            task_id, 0));
}

IFMapDependencyManager::~IFMapDependencyManager() {
    // TODO: Unregister from all tables.
}

void IFMapDependencyManager::Initialize() {
    typedef IFMapDependencyTracker::PropagateList PropagateList;
    typedef IFMapDependencyTracker::ReactionMap ReactionMap;

    static const char *ifmap_types[] = {
        "instance-ip",
        "loadbalancer-healthmonitor",
        "loadbalancer-member",
        "loadbalancer-pool",
        "service-instance",
        "service-template",
        "virtual-ip",
        "virtual-machine",
        "virtual-machine-interface",
        "virtual-network-network-ipam",
    };

    // Link table
    DBTable *link_table = static_cast<DBTable *>(
        database_->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(link_table != NULL);

    DBTable::ListenerId id = link_table->Register(
        boost::bind(&IFMapDependencyManager::LinkObserver, this, _1, _2));
    table_map_.insert(make_pair(link_table->name(), id));

    // Identifier tables
    const int n_types = sizeof(ifmap_types) / sizeof(const char *);
    for (int i = 0; i < n_types; i++) {
        const char *id_typename = ifmap_types[i];
        IFMapTable *table = IFMapTable::FindTable(database_, id_typename);
        assert(table);
        DBTable::ListenerId id = table->Register(
            boost::bind(&IFMapDependencyManager::NodeObserver, this, _1, _2));
        table_map_.insert(make_pair(table->name(), id));
    }

    // Policy definition
    IFMapDependencyTracker::NodeEventPolicy *policy = tracker_->policy_map();

    ReactionMap react_si = map_list_of<string, PropagateList>
            ("loadbalancer-pool-service-instance", list_of("self"))
            ("service-instance-service-template", list_of("self"))
            ("virtual-machine-service-instance", list_of("self"))
            ("self", list_of("self"));
    policy->insert(make_pair("service-instance", react_si));

    ReactionMap react_tmpl = map_list_of<string, PropagateList>
            ("self", list_of("service-instance-service-template"));
    policy->insert(make_pair("service-template", react_tmpl));

    ReactionMap react_vm = map_list_of<string, PropagateList>
            ("self", list_of("virtual-machine-service-instance"))
            ("virtual-machine-virtual-machine-interface",
             list_of("virtual-machine-service-instance"))
            ("virtual-machine-interface-virtual-machine",
             list_of("virtual-machine-service-instance"));
    policy->insert(make_pair("virtual-machine", react_vm));

    ReactionMap react_vmi = map_list_of<string, PropagateList>
            ("self", list_of("virtual-machine-interface-virtual-machine")
                            ("logical-interface-virtual-machine-interface"))
            ("instance-ip-virtual-machine-interface",
             list_of("virtual-machine-interface-virtual-machine"))
            ("virtual-machine-interface-virtual-network",
             list_of("virtual-machine-interface-virtual-machine")
                    ("logical-interface-virtual-machine-interface"));
    policy->insert(make_pair("virtual-machine-interface", react_vmi));

    ReactionMap react_ip = map_list_of<string, PropagateList>
            ("self", list_of("instance-ip-virtual-machine-interface"));
    policy->insert(make_pair("instance-ip", react_ip));

    ReactionMap react_subnet = map_list_of<string, PropagateList>
            ("self", list_of("virtual-network-network-ipam"))
            ("virtual-network-network-ipam", list_of("nil"));
    policy->insert(make_pair("virtual-network-network-ipam", react_subnet));

    ReactionMap react_ipam = map_list_of<string, PropagateList>
            ("virtual-network-network-ipam", list_of("nil"));
    policy->insert(make_pair("network-ipam", react_ipam));

    ReactionMap react_lb_pool = map_list_of<string, PropagateList>
            ("loadbalancer-pool-loadbalancer-healthmonitor",
             list_of("self")("loadbalancer-pool-service-instance"))
            ("loadbalancer-pool-loadbalancer-member",
             list_of("self")("loadbalancer-pool-service-instance"))
            ("self", list_of("self")("loadbalancer-pool-service-instance"))
            ("virtual-ip-loadbalancer-pool", list_of("self"));
    policy->insert(make_pair("loadbalancer-pool", react_lb_pool));

    ReactionMap react_lb_vip = map_list_of<string, PropagateList>
            ("self", list_of("virtual-ip-loadbalancer-pool"));
    policy->insert(make_pair("virtual-ip", react_lb_vip));

    ReactionMap react_lb_member = map_list_of<string, PropagateList>
            ("self", list_of("loadbalancer-pool-loadbalancer-member"));
    policy->insert(make_pair("loadbalancer-member", react_lb_member));

    ReactionMap react_lb_healthmon = map_list_of<string, PropagateList>
            ("self", list_of("loadbalancer-pool-loadbalancer-healthmonitor"));
    policy->insert(make_pair("loadbalancer-healthmonitor", react_lb_healthmon));
}

void IFMapDependencyManager::RegisterReactionMap
(const char *node_name, const IFMapDependencyTracker::ReactionMap &react) {
    // Register to the IFMap table
    IFMapTable *table = IFMapTable::FindTable(database_, node_name);
    assert(table);
    DBTable::ListenerId id = table->Register
        (boost::bind(&IFMapDependencyManager::NodeObserver, this, _1, _2));
    table_map_.insert(make_pair(table->name(), id));

    // Add Policy
    tracker_->policy_map()->insert(make_pair(node_name, react));
}

void IFMapDependencyManager::Terminate() {
    for (TableMap::iterator iter = table_map_.begin();
         iter != table_map_.end(); ++iter) {
        DBTable *table = static_cast<DBTable *>(
            database_->FindTable(iter->first));
        table->Unregister(iter->second);
    }
    table_map_.clear();
    event_map_.clear();
}

bool IFMapDependencyManager::ProcessChangeList() {
    tracker_->PropagateChanges();
    tracker_->Clear();

    for (ChangeList::iterator iter = change_list_.begin();
         iter != change_list_.end(); ++iter) {
        IFMapNodeState *state = iter->get();
        IFMapTable *table = state->node()->table();
        EventMap::iterator loc = event_map_.find(table->Typename());
        if (loc == event_map_.end()) {
            continue;
        }
        if (state->object()) {
            loc->second(state->object());
        }
    }
    change_list_.clear();
    return true;
}

void IFMapDependencyManager::NodeObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {

    IFMapNode *node = static_cast<IFMapNode *>(db_entry);
    tracker_->NodeEvent(node);
    trigger_->Set();
}

void IFMapDependencyManager::LinkObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    IFMapLink *link = static_cast<IFMapLink *>(db_entry);
    IFMapNode *left = link->LeftNode(database_);
    IFMapNode *right = link->RightNode(database_);
    if (tracker_->LinkEvent(link->metadata(), left, right)) {
        trigger_->Set();
    }
}

void IFMapDependencyManager::ChangeListAdd(IFMapNode *node) {
    IFMapNodeState *state = IFMapNodeGet(node);
    if (state == NULL) {
        return;
    }
    change_list_.push_back(IFMapNodePtr(state));
}

IFMapNodeState *
IFMapDependencyManager::IFMapNodeGet(IFMapNode *node) {
    IFMapTable *table = node->table();
    TableMap::const_iterator loc = table_map_.find(table->name());
    if (loc == table_map_.end()) {
        return NULL;
    }
    IFMapNodeState *state =
            static_cast<IFMapNodeState *>(node->GetState(table, loc->second));
    return state;
}

void IFMapDependencyManager::IFMapNodeReset(IFMapNode *node) {
    IFMapTable *table = node->table();
    TableMap::const_iterator loc = table_map_.find(table->name());
    assert(loc != table_map_.end());
    node->ClearState(node->table(), loc->second);
}

/*
 * Associate an IFMapNode with an object in the operational database.
 *
 * IFMapNodes that do not have an object mapping do not receive notifications.
 * This method checks whether the DBState exists and delegates the
 * responsibility to create a new mapping to IFMapNodeSet (private).
 */
void IFMapDependencyManager::SetObject(IFMapNode *node, DBEntry *entry) {

    IFMapNodeState *state = IFMapNodeGet(node);
    assert(state);

    DBEntry *old_entry = state->object();

    if (old_entry)
        state->clear_object();

    if (entry)
        state->set_object(entry);

    tracker_->NodeEvent(node);
    trigger_->Set();
}

IFMapDependencyManager::IFMapNodePtr
IFMapDependencyManager::SetState(IFMapNode *node) {
    IFMapTable *table = node->table();
    TableMap::const_iterator loc = table_map_.find(table->name());
    if (loc == table_map_.end())
        return NULL;

    IFMapNodeState *state =
            static_cast<IFMapNodeState *>(node->GetState(table, loc->second));

    if (!state) {
        state = new IFMapNodeState(this, node);
        node->SetState(table, loc->second, state);
    }
    return IFMapNodePtr(state);
}

/*
 * Register a notification callback.
 */
void IFMapDependencyManager::Register(
    const string &type, ChangeEventHandler handler) {
    event_map_.insert(std::make_pair(type, handler));
}

/*
 * Unregister a notification callback.
 */
void IFMapDependencyManager::Unregister(const string &type) {
    event_map_.erase(type);
}
