/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_H_

class TorAgentParam;
class SandeshOvsdbClient;

class OvsPeerManager;
class KSyncObjectManager;

namespace OVSDB {
class OvsdbClientSession;
class ConnectionStateTable;
class OvsdbClient {
public:
    typedef boost::function<void (OvsdbClientSession *)> SessionEventCb;

    static const uint32_t OVSDBKeepAliveTimer = 10000; // in millisecond

    // minimum value of keep alive interval
    static const int OVSDBMinKeepAliveTimer = 2000; // in millisecond

    static const uint32_t OVSDBHaStaleRouteTimer = 300000; // in millisecond

    // minimum value of keep alive interval
    static const int OVSDBMinHaStaleRouteTimer = 60000; // in millisecond

    OvsdbClient(OvsPeerManager *manager, int keepalive_interval,
                int ha_stale_route_interval);
    virtual ~OvsdbClient();
    virtual void RegisterClients() = 0;
    virtual const std::string protocol() = 0;
    virtual const std::string server() = 0;
    virtual uint16_t port() = 0;
    virtual Ip4Address tsn_ip() = 0;

    // API to find session given ip and port, if port is zero
    // it should give the next available session for the ip
    virtual OvsdbClientSession *FindSession(Ip4Address ip, uint16_t port) = 0;

    // API to get the next session, return first session if
    // argument provided is NULL
    virtual OvsdbClientSession *NextSession(OvsdbClientSession *session) = 0;
    virtual void AddSessionInfo(SandeshOvsdbClient &client) = 0;
    virtual void shutdown() = 0;

    // fucntions to be used by UT for event generations
    void set_connect_complete_cb(SessionEventCb cb);
    void set_pre_connect_complete_cb(SessionEventCb cb);

    void RegisterConnectionTable(Agent *agent);
    ConnectionStateTable *connection_table();
    KSyncObjectManager *ksync_obj_manager();
    int keepalive_interval() const;
    int ha_stale_route_interval() const;
    void Init();
    static OvsdbClient* Allocate(Agent *agent, TorAgentParam *params,
            OvsPeerManager *manager);

protected:
    OvsPeerManager *peer_manager_;

    SessionEventCb connect_complete_cb_;
    SessionEventCb pre_connect_complete_cb_;

private:
    boost::scoped_ptr<ConnectionStateTable> connection_table_;
    KSyncObjectManager *ksync_obj_manager_;
    int keepalive_interval_;
    int ha_stale_route_interval_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClient);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_H_
