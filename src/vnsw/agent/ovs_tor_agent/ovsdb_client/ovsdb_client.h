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
class OvsdbClient {
public:
    OvsdbClient(OvsPeerManager *manager);
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
    KSyncObjectManager *ksync_obj_manager();
    void Init();
    static OvsdbClient* Allocate(Agent *agent, TorAgentParam *params,
            OvsPeerManager *manager);
protected:
    OvsPeerManager *peer_manager_;
private:
    KSyncObjectManager *ksync_obj_manager_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClient);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_H_
