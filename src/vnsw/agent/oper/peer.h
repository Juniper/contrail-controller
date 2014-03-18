/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_peer_h_
#define vnsw_agent_peer_h_

#include <string>
#include <map>
#include <tbb/mutex.h>
#include <db/db_table_walker.h>
#include <net/address.h>

#define LOCAL_PEER_NAME "Local"
#define LOCAL_VM_PEER_NAME "Local_Vm"
#define LOCAL_VM_PORT_PEER_NAME "Local_Vm_Port"
#define NOVA_PEER_NAME "Nova"
#define LINKLOCAL_PEER_NAME "LinkLocal"

class AgentXmppChannel;

class Peer {
public:
    typedef boost::function<void()> DelPeerDone;
    typedef std::map<std::string, Peer *> PeerMap;
    typedef std::pair<std::string, Peer *> PeerPair;
    enum Type {
        ECMP_PEER,
        BGP_PEER,
        LOCAL_PEER,  // higher priority for local peer
        LOCAL_VM_PEER,
        LOCAL_VM_PORT_PEER,
        LINKLOCAL_PEER,
        NOVA_PEER
    };

    Peer(Type type, const std::string &name) : type_(type), name_(name),
        vrf_uc_walkid_(DBTableWalker::kInvalidWalkerId),
        vrf_mc_walkid_(DBTableWalker::kInvalidWalkerId) {
        num_walks_ = -1;
    }
    virtual ~Peer() {
    }
    void DelPeerRoutes(DelPeerDone cb);
    void PeerNotifyRoutes();
    void PeerNotifyMulticastRoutes(bool associate);
    bool IsLess(const Peer *rhs) const {
        if  (type_ != rhs->type_) {
            return type_ < rhs->type_;
        }

        return Compare(rhs);
    }
    virtual bool Compare(const Peer *rhs) const {return false;}

    const std::string &GetName() const { return name_; }
    const Type GetType() const { return type_; }

    DBTableWalker::WalkId GetPeerVrfUCWalkId() { return vrf_uc_walkid_; }
    DBTableWalker::WalkId GetPeerVrfMCWalkId() { 
        return vrf_mc_walkid_; 
    }
    void SetPeerVrfUCWalkId(DBTableWalker::WalkId id) {
        vrf_uc_walkid_ = id;
    }
    void SetPeerVrfMCWalkId(DBTableWalker::WalkId id) {
        vrf_mc_walkid_ = id;
    }
    void ResetPeerVrfUCWalkId() {
        vrf_uc_walkid_ = DBTableWalker::kInvalidWalkerId;
    }
    void ResetPeerVrfMCWalkId() {
        vrf_mc_walkid_ = DBTableWalker::kInvalidWalkerId;
    }
    
    void SetNoOfWalks(int walks) {
        num_walks_ = walks;
    }

    void DecrementWalks() { 
        if (num_walks_ > 0) {
            num_walks_--; 
        }
    }

    tbb::atomic<int> NoOfWalks() { return num_walks_; }

    void ResetWalks() { 
        num_walks_ = 0; 
    }

private:
    Type type_;
    std::string name_;
    DBTableWalker::WalkId vrf_uc_walkid_;
    DBTableWalker::WalkId vrf_mc_walkid_;
    tbb::atomic<int> num_walks_;
    DISALLOW_COPY_AND_ASSIGN(Peer);
};

// Peer used for BGP paths
class BgpPeer : public Peer {
public:
    BgpPeer(const Ip4Address &server_ip, const std::string &name,
            AgentXmppChannel *bgp_xmpp_peer, DBTableBase::ListenerId id) : 
        Peer(Peer::BGP_PEER, name), server_ip_(server_ip), id_(id),
        bgp_xmpp_peer_(bgp_xmpp_peer) {
    }

    virtual ~BgpPeer() { }

    bool Compare(const Peer *rhs) const {
        const BgpPeer *bgp = static_cast<const BgpPeer *>(rhs);
        return server_ip_ < bgp->server_ip_;
    }

    void SetVrfListenerId(DBTableBase::ListenerId id) { id_ = id; }
    DBTableBase::ListenerId GetVrfExportListenerId() { return id_; } 
    AgentXmppChannel *GetBgpXmppPeer() { return bgp_xmpp_peer_; }    
private: 
    Ip4Address server_ip_;
    DBTableBase::ListenerId id_;
    AgentXmppChannel *bgp_xmpp_peer_;
    DISALLOW_COPY_AND_ASSIGN(BgpPeer);
};

// Peer for local-vm-port paths. There can be multiple VMs with same IP.
// They are all added as different path. ECMP path will consolidate all 
// local-vm-port paths
class LocalVmPortPeer : public Peer {
public:
    LocalVmPortPeer(const std::string &name, uint64_t handle) :
        Peer(Peer::LOCAL_VM_PORT_PEER, name), handle_(handle) {
    }

    virtual ~LocalVmPortPeer() { }

    bool Compare(const Peer *rhs) const {
        const LocalVmPortPeer *local = 
            static_cast<const LocalVmPortPeer *>(rhs);
        return handle_ < local->handle_;
    }

private:
    uint64_t handle_;
    DISALLOW_COPY_AND_ASSIGN(LocalVmPortPeer);
};

// ECMP peer
class EcmpPeer : public Peer {
public:
    EcmpPeer() : Peer(Peer::ECMP_PEER, "ECMP") { }
    virtual ~EcmpPeer() { }

    bool Compare(const Peer *rhs) const { return false; }
private:
    DISALLOW_COPY_AND_ASSIGN(EcmpPeer);
};
#endif // vnsw_agent_peer_h_
