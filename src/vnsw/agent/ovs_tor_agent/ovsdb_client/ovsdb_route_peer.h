/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVS_PEER_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVS_PEER_H_

#include <cmn/agent_cmn.h>
#include <oper/peer.h>

class OvsPeerManager;

// Peer for routes added by OVS
class OvsPeer : public DynamicPeer {
 public:
    OvsPeer(const IpAddress &peer_ip, uint64_t gen_id, OvsPeerManager *manager);
    virtual ~OvsPeer();

    bool Compare(const Peer *rhs) const;
    const Ip4Address *NexthopIp(Agent *agent, const AgentPath *path) const;
    bool AddOvsRoute(const VrfEntry *vrf, uint32_t vxlan_id,
                     const std::string &dest_vn, const MacAddress &mac,
                     Ip4Address &tor_ip);
    void DeleteOvsRoute(VrfEntry *vrf, uint32_t vxlan, const MacAddress &mac);
    void AddOvsPeerMulticastRoute(const VrfEntry *vrf,
                                  uint32_t vxlan_id,
                                  const std::string &vn_name_,
                                  const Ip4Address &tsn_ip,
                                  const Ip4Address &tor_ip);
    void DeleteOvsPeerMulticastRoute(const VrfEntry *vrf,
                                     uint32_t vxlan_id,
                                     const Ip4Address &tor_ip);
    const IpAddress &peer_ip() const {return peer_ip_;}

    void set_ha_stale_export(bool ha_stale_export) {
        ha_stale_export_ = ha_stale_export;
    }
    bool ha_stale_export() const { return ha_stale_export_; }

 private:
    IpAddress peer_ip_;
    uint64_t gen_id_;
    OvsPeerManager *peer_manager_;
    bool ha_stale_export_;
    DISALLOW_COPY_AND_ASSIGN(OvsPeer);
};

class OvsPeerManager {
 public:
    struct cmp {
        bool operator()(const OvsPeer *lhs, const OvsPeer *rhs) {
            return lhs->Compare(rhs);
        }
    };
    typedef std::set<OvsPeer *, cmp> OvsPeerTable;

    OvsPeerManager(Agent *agent);
    virtual ~OvsPeerManager();

    OvsPeer *Allocate(const IpAddress &peer_ip);
    void Free(OvsPeer *peer);
    uint32_t Size() const;
    Agent *agent() const;

 private:
    uint64_t gen_id_;
    Agent *agent_;
    OvsPeerTable table_;
    DISALLOW_COPY_AND_ASSIGN(OvsPeerManager);
};

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVS_PEER_H_
