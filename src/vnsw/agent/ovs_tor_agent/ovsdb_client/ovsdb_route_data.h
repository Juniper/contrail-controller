/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVS_ROUTE_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVS_ROUTE_H_

#include <cmn/agent_cmn.h>
#include <oper/route_common.h>

class Peer;

class OvsdbRouteData : public AgentRouteData {
public:
    OvsdbRouteData(const Peer *peer, uint32_t vxlan_id,
                   const Ip4Address &tor_ip, const Ip4Address &router_id,
                   const std::string &tor_vrf, const std::string &dest_vn_name,
                   const SecurityGroupList &sg_list,
                   bool ha_stale_export,
                   uint32_t sequence);
    OvsdbRouteData(const Peer *peer);
    virtual ~OvsdbRouteData();

    virtual std::string ToString() const;
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *data);

private:
    const Peer *peer_;
    uint32_t vxlan_id_;
    Ip4Address tor_ip_;
    std::string tor_vrf_;
    Ip4Address router_id_;
    std::string dest_vn_name_;
    SecurityGroupList sg_list_;
    bool ha_stale_export_;
    uint32_t sequence_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbRouteData);
};

class OvsdbRouteResyncData : public AgentRouteData {
public:
    OvsdbRouteResyncData(const SecurityGroupList &sg_list);
    virtual ~OvsdbRouteResyncData();

    virtual std::string ToString() const;
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *data);

private:
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbRouteResyncData);
};

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVS_ROUTE_H_
