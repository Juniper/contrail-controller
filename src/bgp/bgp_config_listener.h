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
class IFMapNode;

// Observes events on the DBTables associated with BGP configuration items.
class BgpConfigListener {
public:
    typedef std::vector<BgpConfigDelta> ChangeList;

    explicit BgpConfigListener(BgpConfigManager *manager);
    ~BgpConfigListener();

    void Initialize(DB *database);
    void Terminate(DB *database);

    void GetChangeList(ChangeList *change_list);

private:
    typedef std::map<std::string, DBTable::ListenerId> TableMap;
    typedef std::set<std::string> ChangeSet;
    class DependencyTracker;
    
    void NodeObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void LinkObserver(DBTablePartBase *root, DBEntryBase *db_entry);

    void ChangeListAdd(ChangeList *change_list, IFMapNode *node) const;

    DB *database();
    DBGraph *graph();

    BgpConfigManager *manager_;
    boost::scoped_ptr<DependencyTracker> tracker_;
    TableMap table_map_;
    ChangeList change_list_;
    ChangeSet change_set_;
    DISALLOW_COPY_AND_ASSIGN(BgpConfigListener);
};


#endif /* defined(__ctrlplane__bgp_config_listener__) */
