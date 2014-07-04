/*
 * Copyright (c) 2014 Codilime.
 */

#include "ifmap/ifmap_config_listener.h"

#include <boost/assign/list_of.hpp>

#include "ifmap/ifmap_dependency_tracker.h"

IFMapConfigListener::ConfigDelta::ConfigDelta() {
}

IFMapConfigListener::ConfigDelta::ConfigDelta(
  const IFMapConfigListener::ConfigDelta &rhs)
    : id_type(rhs.id_type), id_name(rhs.id_name),
      node(rhs.node), obj(rhs.obj) {
}

IFMapConfigListener::IFMapConfigListener(ConfigManager *manager,
                                          const char *concurrency)
    : manager_(manager), kConcurrency_(concurrency) {}

IFMapConfigListener::~IFMapConfigListener() {}

//
// Initialize the IFMapConfigListener.
//
// Create and initialize the DependencyTracker.
// We register one listener for the IFMapLinkTable and a listener for each of
// the relevant IFMapTables.
//
// Pure virtual DependencyTrackerInit() should initialize dependency tracker's
// policy_map, which is then used to register listeners for specified tables.
//
void IFMapConfigListener::Initialize() {
    DB *database = manager_->database();

    tracker_.reset(
        new IFMapDependencyTracker(
            database, manager_->graph(),
            boost::bind(&IFMapConfigListener::ChangeListAdd, this, _1)));
    DependencyTrackerInit();

    DBTable *link_table = static_cast<DBTable *>(
        database->FindTable("__ifmap_metadata__.0"));
    assert(link_table != NULL);

    DBTable::ListenerId id = link_table->Register(
        boost::bind(&IFMapConfigListener::LinkObserver, this, _1, _2));
    table_map_.insert(make_pair(link_table->name(), id));

    BOOST_FOREACH(
      const IFMapDependencyTracker::NodeEventPolicy::value_type &policy,
        *tracker_->policy_map()) {
        const char *schema_typename= policy.first.c_str();
        IFMapTable *table = IFMapTable::FindTable(database, schema_typename);
        assert(table);
        DBTable::ListenerId id = table->Register(
                boost::bind(&IFMapConfigListener::NodeObserver, this, _1, _2));
        table_map_.insert(make_pair(table->name(), id));
    }
}

//
// Unregister listeners for all the IFMapTables.
//
void IFMapConfigListener::Terminate() {
    DB *database = manager_->database();

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
// Get the the DB in the ConfigManager.
//
DB *IFMapConfigListener::database() {
    return manager_->database();
}

//
// Ask the DependencyTracker to build up the ChangeList.
//
void IFMapConfigListener::GetChangeList(ChangeList *change_list) {
    CHECK_CONCURRENCY(kConcurrency_.c_str());

    tracker_->PropagateChanges();
    tracker_->Clear();
    change_list->swap(change_list_);
}

IFMapDependencyTracker *IFMapConfigListener::get_dependency_tracker() {
    return tracker_.get();
}

//
// Add an IFMapNode to the ChangeList by creating a ConfigDelta.
//
// We take references on the IFMapNode and the IFMapObject.
//
void IFMapConfigListener::ChangeListAdd(IFMapNode *node) {
    CHECK_CONCURRENCY(kConcurrency_.c_str(), "db::DBTable");

    IFMapTable *table = node->table();
    TableMap::const_iterator tid = table_map_.find(table->name());
    if (tid == table_map_.end()) {
        return;
    }

    ConfigDelta delta;
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
void IFMapConfigListener::NodeObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    // Ignore deleted nodes for which the configuration code doesn't hold
    // state. This is the case with ie. BgpRouter objects other than the local
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
void IFMapConfigListener::LinkObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    IFMapLink *link = static_cast<IFMapLink *>(db_entry);
    IFMapNode *left = link->LeftNode(database());
    IFMapNode *right = link->RightNode(database());
    if (tracker_->LinkEvent(link->metadata(), left, right)) {
        manager_->OnChange();
    }
}
