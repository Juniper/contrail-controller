/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_CONNECTION_STATE_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_CONNECTION_STATE_H_

#include <tbb/atomic.h>
#include <base/connection_info.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

#include <cmn/agent.h>
#include <oper/physical_device.h>

#include "ovsdb_client_idl.h"

namespace OVSDB {
class ConnectionStateEntry;
class ConnectionStateTable {
public:
    typedef std::map<std::string, ConnectionStateEntry *> EntryMap;
    explicit ConnectionStateTable(Agent *agent);
    virtual ~ConnectionStateTable();

    void AddIdlToConnectionState(const std::string &device_name,
                                 OvsdbClientIdl *idl);
    void DelIdlToConnectionState(const std::string &device_name,
                                 OvsdbClientIdl *idl);
    void PhysicalDeviceNotify(DBTablePartBase *part, DBEntryBase *e);

private:
    void UpdateConnectionInfo(ConnectionStateEntry *entry, bool deleted);

    Agent *agent_;
    DBTableBase *table_;
    DBTableBase::ListenerId id_;
    EntryMap entry_map_;
    DISALLOW_COPY_AND_ASSIGN(ConnectionStateTable);
};

class ConnectionStateEntry : public DBState {
public:
    typedef std::set<OvsdbClientIdl *> IdlList;
    explicit ConnectionStateEntry(const std::string &device_name);
    virtual ~ConnectionStateEntry();

private:
    friend class ConnectionStateTable;
    std::string device_name_;
    process::ConnectionStatus::type status_;
    PhysicalDeviceRef device_entry_;
    IdlList idl_list_;
    DISALLOW_COPY_AND_ASSIGN(ConnectionStateEntry);
};

};

#endif  //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_CONNECTION_STATE_H_

