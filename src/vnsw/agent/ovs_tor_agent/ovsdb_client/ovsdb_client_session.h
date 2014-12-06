/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_

#include <assert.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

class OvsPeer;
class OvsPeerManager;
class KSyncObjectManager;

namespace OVSDB {
class OvsdbClientIdl;
class OvsdbClientSession {
public:
    OvsdbClientSession(Agent *agent, OvsPeerManager *manager);
    virtual ~OvsdbClientSession();

    virtual KSyncObjectManager *ksync_obj_manager() = 0;
    virtual Ip4Address tsn_ip() = 0;
    virtual void SendMsg(u_int8_t *buf, std::size_t len) = 0;
    void MessageProcess(const u_int8_t *buf, std::size_t len);
    void OnEstablish();
    void OnClose();
    OvsdbClientIdl *client_idl();

private:
    friend class OvsdbClientIdl;
    OvsdbClientIdl client_idl_;
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbClientSession);
};
};  // namespace OVSDB

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_CLIENT_SESSION_H_

