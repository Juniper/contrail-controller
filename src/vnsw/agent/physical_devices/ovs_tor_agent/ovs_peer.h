/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_OVS_PEER_H_
#define SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_OVS_PEER_H_

#include <cmn/agent_cmn.h>
#include <oper/peer.h>

// Peer for routes added by OVS
class OvsPeer : public Peer {
 public:
    OvsPeer(const IpAddress &peer_ip, uint64_t gen_id);
    virtual ~OvsPeer();

    bool Compare(const Peer *rhs) const;
 private:
    IpAddress peer_ip_;
    uint64_t gen_id_;
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

    OvsPeerManager();
    virtual ~OvsPeerManager();

    OvsPeer *Allocate(const IpAddress &peer_ip);
    void Free(OvsPeer *peer);

 private:
    uint64_t gen_id_;
    OvsPeerTable table_;
    DISALLOW_COPY_AND_ASSIGN(OvsPeerManager);
};

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_OVS_TOR_AGENT_OVS_PEER_H_
