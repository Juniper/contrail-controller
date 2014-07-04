/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_IFMAP_CONFIG_LISTENER_
#define SRC_IFMAP_CONFIG_LISTENER_

#include <map>
#include <string>
#include <vector>

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "base/util.h"
#include "base/task_annotations.h"
#include "db/db.h"
#include "db/db_table.h"
#include "ifmap/ifmap_dependency_tracker.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_node_proxy.h"
#include "ifmap/ifmap_table.h"

class DB;
class DBGraph;
class DBTablePartBase;
class IFMapDependencyTracker;
class IFMapNode;

typedef boost::shared_ptr<IFMapNodeProxy> IFMapNodeRef;

namespace IFMapConfigListenerTypes {
    struct ConfigDelta {
        ConfigDelta();
        ConfigDelta(const ConfigDelta &rhs);
        std::string id_type;
        std::string id_name;
        IFMapNodeRef node;
        IFMapObjectRef obj;
    };
    typedef std::vector<ConfigDelta> ChangeList;
}  // namespace IFMapConfigListenerTypes

//
// This class implements an observer for events on the IFMapTables associated
// with configuration items. It listens to the IFMapTables in question and
// puts ConfigDeltas on the change list.  TableMap is a list of IFMapTable
// names and corresponding DBTable::ListenerIds that this class has registered.
//
// The DependencyTracker recursively evaluates dependencies as specified via a
// policy and pushes additional ConfigDeltas to the change list. This takes
// the burden of dependency tracking away from the consumers and automates it
// instead of being individually hand coded for each type of object.
//
// The ChangeList of ConfigDeltas is processed by the ConfigManager with
// which this IFMapConfigListener is associated.
//
template <class ConfigManager>
class IFMapConfigListener {
 public:
    typedef IFMapConfigListenerTypes::ConfigDelta ConfigDelta;
    typedef IFMapConfigListenerTypes::ChangeList ChangeList;

    explicit IFMapConfigListener(ConfigManager *manager, const char *concurrency);
    virtual ~IFMapConfigListener();

    void Initialize();
    void Terminate();

    virtual void GetChangeList(ChangeList *change_list);

 protected:
    IFMapDependencyTracker *get_dependency_tracker();

 private:
    typedef std::map<std::string, DBTable::ListenerId> TableMap;

    virtual void DependencyTrackerInit() = 0;
    virtual void NodeObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    virtual void LinkObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void ChangeListAdd(IFMapNode *node);

    DB *database();

    ConfigManager *manager_;
    boost::scoped_ptr<IFMapDependencyTracker> tracker_;
    TableMap table_map_;
    ChangeList change_list_;
    const std::string kConcurrency_;
};

template <class ConfigManager>
IFMapConfigListener<ConfigManager>::IFMapConfigListener(ConfigManager *manager,
  const char *concurrency)
    : manager_(manager), kConcurrency_(concurrency) {
}

template <class ConfigManager>
IFMapConfigListener<ConfigManager>::~IFMapConfigListener() {
}

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
template <class ConfigManager>
void IFMapConfigListener<ConfigManager>::Initialize() {
    DB *database = manager_->database();

    tracker_.reset(
        new IFMapDependencyTracker(
            database, manager_->graph(),
            boost::bind(&IFMapConfigListener<ConfigManager>
              ::ChangeListAdd, this, _1)));
    DependencyTrackerInit();

    DBTable *link_table = static_cast<DBTable *>(
        database->FindTable("__ifmap_metadata__.0"));
    assert(link_table != NULL);

    DBTable::ListenerId id = link_table->Register(
        boost::bind(&IFMapConfigListener<ConfigManager>
          ::LinkObserver, this, _1, _2));
    table_map_.insert(make_pair(link_table->name(), id));

    BOOST_FOREACH(
      const IFMapDependencyTracker::NodeEventPolicy::value_type &policy,
        *tracker_->policy_map()) {
        const char *schema_typename= policy.first.c_str();
        IFMapTable *table = IFMapTable::FindTable(database, schema_typename);
        assert(table);
        DBTable::ListenerId id = table->Register(
                boost::bind(&IFMapConfigListener<ConfigManager>
                  ::NodeObserver, this, _1, _2));
        table_map_.insert(make_pair(table->name(), id));
    }
}

//
// Unregister listeners for all the IFMapTables.
//
template <class ConfigManager>
void IFMapConfigListener<ConfigManager>::Terminate() {
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
template <class ConfigManager>
DB *IFMapConfigListener<ConfigManager>::database() {
    return manager_->database();
}

//
// Ask the DependencyTracker to build up the ChangeList.
//
template <class ConfigManager>
void IFMapConfigListener<ConfigManager>
  ::GetChangeList(ChangeList
    *change_list) {
    CHECK_CONCURRENCY(kConcurrency_.c_str());

    tracker_->PropagateChanges();
    tracker_->Clear();
    change_list->swap(change_list_);
}

template <class ConfigManager>
IFMapDependencyTracker *IFMapConfigListener<ConfigManager>
  ::get_dependency_tracker() {
  return tracker_.get();
}

//
// Add an IFMapNode to the ChangeList by creating a ConfigDelta.
//
// We take references on the IFMapNode and the IFMapObject.
//
template <class ConfigManager>
void IFMapConfigListener<ConfigManager>::ChangeListAdd(IFMapNode *node) {
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
template <class ConfigManager>
void IFMapConfigListener<ConfigManager>::NodeObserver(
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
template <class ConfigManager>
void IFMapConfigListener<ConfigManager>::LinkObserver(
    DBTablePartBase *root, DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    IFMapLink *link = static_cast<IFMapLink *>(db_entry);
    IFMapNode *left = link->LeftNode(database());
    IFMapNode *right = link->RightNode(database());
    if (tracker_->LinkEvent(link->metadata(), left, right)) {
        manager_->OnChange();
    }
}
#endif /* SRC_IFMAP_CONFIG_LISTENER */
