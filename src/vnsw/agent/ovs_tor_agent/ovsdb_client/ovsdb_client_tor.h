/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_TOR_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_TOR_H_

#include <boost/uuid/uuid_io.hpp>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

namespace OVSDB {
class TorTable {
public:
    typedef std::pair<Ip4Address, boost::uuids::uuid> EndPointKey;
    typedef std::set<EndPointKey> EndPointList;
    typedef boost::function<void(Ip4Address)> TorDeleteCallback;

    struct TorEntryState : public DBState {
        Ip4Address ip;
        boost::uuids::uuid uuid;
    };

    TorTable(Agent *agent, TorDeleteCallback cb);
    ~TorTable();

    // DB Notify callback for Physical Device table.
    void PhyiscalDeviceNotify(DBTablePartBase *partition, DBEntryBase *e);

    bool isTorAvailable(Ip4Address ip);

private:
    void HandleIpDelete(TorEntryState *state);

    TorDeleteCallback cb_;
    DBTableBase *table_;
    DBTableBase::ListenerId id_;
    EndPointList end_point_list_;
    DISALLOW_COPY_AND_ASSIGN(TorTable);
};

};

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_TOR_H_

