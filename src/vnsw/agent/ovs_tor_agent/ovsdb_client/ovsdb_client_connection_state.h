/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_CONNECTION_STATE_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_CONNECTION_STATE_H_

#include <boost/intrusive_ptr.hpp>
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
class HaStaleDevVnTable;
class ConnectionStateEntry;
typedef boost::intrusive_ptr<ConnectionStateEntry> ConnectionStateEntryPtr;

// Table to maintain Connection State for Physical Switches/Devices
class ConnectionStateTable {
public:
    typedef std::map<std::string, ConnectionStateEntry *> EntryMap;
    ConnectionStateTable(Agent *agent,  OvsPeerManager *manager);
    virtual ~ConnectionStateTable();

    // Adding first IDL to ConnectionState Entry marks session Up
    void AddIdlToConnectionState(const std::string &device_name,
                                 OvsdbClientIdl *idl);

    // Deleting last IDL from ConnectionState Entry marks session Down
    void DelIdlToConnectionState(const std::string &device_name,
                                 OvsdbClientIdl *idl);

private:
    friend void intrusive_ptr_release(ConnectionStateEntry *p);

    void PhysicalDeviceNotify(DBTablePartBase *part, DBEntryBase *e);

    // API to update connection info state
    void UpdateConnectionInfo(ConnectionStateEntry *entry, bool deleted);

    Agent *agent_;
    DBTableBase *table_;
    DBTableBase::ListenerId id_;
    EntryMap entry_map_;
    OvsPeerManager *manager_;
    DISALLOW_COPY_AND_ASSIGN(ConnectionStateTable);
};

// Connection State Entry represent a physical router, reports connection
// state Up as long as config and idl for Physical Switch exists
class ConnectionStateEntry : public DBState {
public:
    typedef std::set<OvsdbClientIdl *> IdlList;
    ConnectionStateEntry(ConnectionStateTable *table,
                         const std::string &device_name);
    virtual ~ConnectionStateEntry();

    bool IsConnectionActive();

private:
    friend class ConnectionStateTable;
    friend void intrusive_ptr_add_ref(ConnectionStateEntry *p);
    friend void intrusive_ptr_release(ConnectionStateEntry *p);

    ConnectionStateTable *table_;
    std::string device_name_;
    PhysicalDevice *device_entry_;
    IdlList idl_list_;
    HaStaleDevVnTable *replicator_table_;
    tbb::atomic<int> refcount_;
    DISALLOW_COPY_AND_ASSIGN(ConnectionStateEntry);
};

};

#endif  //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_CONNECTION_STATE_H_

