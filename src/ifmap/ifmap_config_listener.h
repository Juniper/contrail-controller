/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 * Copyright (c) 2014 Codilime.
 */

#ifndef SRC_IFMAP_CONFIG_LISTENER_
#define SRC_IFMAP_CONFIG_LISTENER_

#include <map>
#include <string>
#include <vector>

#include <boost/bind.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "base/util.h"
#include "base/task_annotations.h"
#include "db/db.h"
#include "db/db_table.h"
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
class IFMapConfigListener {
 public:
    class ConfigManager {
     public:
        virtual DB* database() = 0;
        virtual DBGraph* graph() = 0;
        virtual void OnChange() = 0;
        virtual ~ConfigManager() {}
    };

    struct ConfigDelta {
        ConfigDelta();
        ConfigDelta(const ConfigDelta &rhs);
        std::string id_type;
        std::string id_name;
        IFMapNodeRef node;
        IFMapObjectRef obj;
    };
    typedef std::vector<ConfigDelta> ChangeList;

    explicit IFMapConfigListener(ConfigManager* manager,
                                  const char* concurrency);
    virtual ~IFMapConfigListener();

    void Initialize();
    void Terminate();

    virtual void GetChangeList(ChangeList* change_list);

 protected:
    IFMapDependencyTracker* get_dependency_tracker();

    // left protected for use with unit tests
    typedef std::map<std::string, DBTable::ListenerId> TableMap;

    virtual void DependencyTrackerInit() = 0;
    virtual void NodeObserver(DBTablePartBase* root, DBEntryBase* db_entry);
    virtual void LinkObserver(DBTablePartBase* root, DBEntryBase* db_entry);
    void ChangeListAdd(IFMapNode* node);

    DB *database();

    ConfigManager* manager_;
    boost::scoped_ptr<IFMapDependencyTracker> tracker_;
    TableMap table_map_;
    ChangeList change_list_;
    const std::string kConcurrency_;
};

#endif /* SRC_IFMAP_CONFIG_LISTENER */
