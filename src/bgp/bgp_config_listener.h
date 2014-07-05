/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__bgp_config_listener__
#define __ctrlplane__bgp_config_listener__

#include <map>
#include <set>
#include <vector>

#include <boost/scoped_ptr.hpp>
#include "base/util.h"
#include "db/db_table.h"

struct BgpConfigDelta;
class BgpConfigManager;
class DB;
class DBGraph;
class IFMapDependencyTracker;
class IFMapNode;

//
// This class implements an observer for events on the IFMapTables associated
// with BGP configuration items. It listens to the IFMapTables in question and
// puts BgpConfigDeltas on the change list.  TableMap is a list of IFMapTable
// names and corresponding DBTable::ListenerIds that this class has registered.
//
// The DependencyTracker recursively evaluates dependencies as specified via a
// policy and pushes additional BgpConfigDeltas to the change list. This takes
// the burden of dependency tracking away from the consumers and automates it
// instead of being individually hand coded for each type of object.
//
// The ChangeList of BgpConfigDeltas is processed by the BgpConfigManager with
// which this BgpConfigListener is associated.
//
class BgpConfigListener {
public:
    typedef std::vector<BgpConfigDelta> ChangeList;

    explicit BgpConfigListener(BgpConfigManager *manager);
    virtual ~BgpConfigListener();

    void Initialize(DB *database);
    void Terminate(DB *database);

    virtual void GetChangeList(ChangeList *change_list);

private:
    friend class BgpConfigListenerTest;

    typedef std::map<std::string, DBTable::ListenerId> TableMap;

    void DependencyTrackerInit();
    void NodeObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void LinkObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void ChangeListAdd(IFMapNode *node);

    DB *database();

    BgpConfigManager *manager_;
    boost::scoped_ptr<IFMapDependencyTracker> tracker_;
    TableMap table_map_;
    ChangeList change_list_;

    DISALLOW_COPY_AND_ASSIGN(BgpConfigListener);
};


#endif /* defined(__ctrlplane__bgp_config_listener__) */
