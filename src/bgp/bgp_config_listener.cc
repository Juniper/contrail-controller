/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_listener.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_config.h"
#include "db/db.h"
#include "ifmap/ifmap_dependency_tracker.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

class DBTablePartBase;

using namespace boost::assign;
using namespace std;

//
// Populate the policy with entries for each interesting identifier type.
//
// Note that identifier types and metadata types will have the same name
// when there's a link with attributes.  This happens because we represent
// such links with a "middle node" which stores all the attributes and add
// plain links between the original nodes and the middle node. An example
// is "bgp-peering".
//
// Additional unit tests should be added to bgp_config_listener_test.cc as
// and when this policy is modified.
//
void BgpConfigListener::DependencyTrackerInit() {
    typedef IFMapDependencyTracker::PropagateList PropagateList;
    typedef IFMapDependencyTracker::ReactionMap ReactionMap;

    IFMapDependencyTracker::NodeEventPolicy *policy = tracker_->policy_map();

    ReactionMap bgp_peering_react = map_list_of<string, PropagateList>
        ("bgp-peering", list_of("self"));
    policy->insert(make_pair("bgp-peering", bgp_peering_react));

    ReactionMap bgp_router_react = map_list_of<string, PropagateList>
        ("self", list_of("bgp-peering"));
    policy->insert(make_pair("bgp-router", bgp_router_react));

    ReactionMap rt_instance_react = map_list_of<string, PropagateList>
        ("instance-target", list_of("self")("connection"))
        ("connection", list_of("self"))
        ("virtual-network-routing-instance", list_of("self"));
    policy->insert(make_pair("routing-instance", rt_instance_react));

    ReactionMap virtual_network_react = map_list_of<string, PropagateList>
        ("self", list_of("virtual-network-routing-instance"));
    policy->insert(make_pair("virtual-network", virtual_network_react));
}

//
// Constructor.
//
BgpConfigListener::BgpConfigListener(BgpConfigManager *manager)
    : manager_(manager) {
}

//
// Destructor, empty for now.
//
BgpConfigListener::~BgpConfigListener() {
}

//
// List of identifier types that are relevant for BGP configuration.
//
// VirtualNetworks are relevant because we generate the OriginVn extended
// community based on the virtual network index in the VirtualNetwork. The
// OriginVn extended community is used for service chaining.
//
static const char *bgp_config_types[] = {
    "bgp-peering",
    "bgp-router",
    "routing-instance",
    "virtual-network",
};

//
// Initialize the BgpConfigListener.
//
// Create and initialize the DependencyTracker.
// We register one listener for the IFMapLinkTable and a listener for each of
// the relevant IFMapTables.
//
void BgpConfigListener::Initialize(DB *database) {
    tracker_.reset(
        new IFMapDependencyTracker(
            database, manager_->graph(),
            boost::bind(&BgpConfigListener::ChangeListAdd, this, _1)));
    DependencyTrackerInit();

    DBTable *link_table = static_cast<DBTable *>(
        database->FindTable("__ifmap_metadata__.0"));
    assert(link_table != NULL);

    DBTable::ListenerId id = link_table->Register(
        boost::bind(&BgpConfigListener::LinkObserver, this, _1, _2));
    table_map_.insert(make_pair(link_table->name(), id));

    const int n_types = sizeof(bgp_config_types) / sizeof(const char *);
    for (int i = 0; i < n_types; i++) {
        const char *bgp_typename = bgp_config_types[i];
        IFMapTable *table = IFMapTable::FindTable(database, bgp_typename);
        assert(table);
        DBTable::ListenerId id = table->Register(
                boost::bind(&BgpConfigListener::NodeObserver, this, _1, _2));
        table_map_.insert(make_pair(table->name(), id));
    }
}

//
// Unregister listeners for all the IFMapTables.
//
void BgpConfigListener::Terminate(DB *database) {
    for (TableMap::iterator iter = table_map_.begin();
         iter != table_map_.end(); ++iter) {
        IFMapTable *table =
            static_cast<IFMapTable *>(database->FindTable(iter->first));
        assert(table);
        table->Unregister(iter->second);
    }
    table_map_.clear();
}

//
// Get the the DB in the BgpConfigManager.
//
DB *BgpConfigListener::database() {
    return manager_->database();
}

//
// Ask the DependencyTracker to build up the ChangeList.
//
void BgpConfigListener::GetChangeList(ChangeList *change_list) {
    CHECK_CONCURRENCY("bgp::Config");

    tracker_->PropagateChanges();
    tracker_->Clear();
    change_list->swap(change_list_);
}

//
// Add an IFMapNode to the ChangeList by creating a BgpConfigDelta.
//
// We take references on the IFMapNode and the IFMapObject.
//
void BgpConfigListener::ChangeListAdd(IFMapNode *node) {
    CHECK_CONCURRENCY("bgp::Config", "db::DBTable");

    IFMapTable *table = node->table();
    TableMap::const_iterator tid = table_map_.find(table->name());
    if (tid == table_map_.end()) {
        return;
    }

    BgpConfigDelta delta;
    delta.id_type = table->Typename();
    delta.id_name = node->name();
    if (!node->IsDeleted()) {
        DBState *current = node->GetState(table, tid->second);
        if (current == NULL) {
            delta.node = IFMapNodeRef(new IFMapNodeProxy(node, tid->second));
        }
        delta.obj = IFMapObjectRef(node->GetObject());
    }
    change_list_.push_back(delta);
}

//
// Callback to handle node events in the IFMapTables.
// Adds the node itself to the ChangeList and informs the DependencyTracker
// about the node event.
//
void BgpConfigListener::NodeObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    // Ignore deleted nodes for which the configuration code doesn't hold
    // state. This is the case with BgpRouter objects other than the local
    // node.
    IFMapNode *node = static_cast<IFMapNode *>(db_entry);
    if (node->IsDeleted()) {
        IFMapTable *table = node->table();
        TableMap::const_iterator tid = table_map_.find(table->name());
        assert(tid != table_map_.end());
        if (node->GetState(table, tid->second) == NULL) {
            return;
        }
    }

    tracker_->NodeEvent(node);
    manager_->OnChange();
}

//
// Callback to handle link events in the IFMapLinkTable.
// Informs the DependencyTracker about the link event.
//
void BgpConfigListener::LinkObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    IFMapLink *link = static_cast<IFMapLink *>(db_entry);
    IFMapNode *left = link->LeftNode(database());
    IFMapNode *right = link->RightNode(database());
    if (tracker_->LinkEvent(link->metadata(), left, right)) {
        manager_->OnChange();
    }
}
