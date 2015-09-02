/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DNS_CONFIG_LISTENER_H__
#define __DNS_CONFIG_LISTENER_H__

#include <map>
#include <set>
#include <vector>
#include <boost/scoped_ptr.hpp>
#include "base/util.h"
#include "db/db_table.h"

struct ConfigDelta;
class DnsConfigManager;
class DB;
class DBGraph;
class IFMapNode;

// Observes events on the DBTables associated with configuration items
class ConfigListener {
public:
    typedef std::vector<ConfigDelta> ChangeList;

    explicit ConfigListener(DnsConfigManager *manager);
    virtual ~ConfigListener();

    void Initialize(DB *database, int ntypes, const char *config_types[]);
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

    DnsConfigManager *manager_;
    boost::scoped_ptr<DependencyTracker> tracker_;
    TableMap table_map_;
    ChangeList change_list_;
    ChangeSet change_set_;
    DISALLOW_COPY_AND_ASSIGN(ConfigListener);
};

#endif // __DNS_CONFIG_LISTENER_H__
