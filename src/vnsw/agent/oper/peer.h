/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_peer_h_
#define vnsw_agent_peer_h_

#include <string>
#include <map>
#include <tbb/mutex.h>
#include <db/db.h>
#include <db/db_table.h>
#include <db/db_table_walker.h>

#define LOCAL_PEER_NAME "Local"
#define LOCAL_VM_PEER_NAME "Local_Vm"
#define NOVA_PEER_NAME "Nova"
#define MDATA_PEER_NAME "MData"

class AgentXmppChannel;

class Peer {
public:
    typedef boost::function<void()> DelPeerDone;
    typedef std::map<std::string, Peer *> PeerMap;
    typedef std::pair<std::string, Peer *> PeerPair;
    enum Type {
        BGP_PEER,
        LOCAL_PEER,  // higher priority for local peer
        LOCAL_VM_PEER,
        MDATA_PEER,
        NOVA_PEER
    };

    Peer(Type type, std::string name) : type_(type), name_(name),
        vrf_uc_walkid_(DBTableWalker::kInvalidWalkerId),
        vrf_mc_walkid_(DBTableWalker::kInvalidWalkerId) {
        tbb::mutex::scoped_lock lock(mutex_);
        peer_map_.insert(PeerPair(name, this));
    };
    ~Peer() {
        tbb::mutex::scoped_lock lock(mutex_);
        peer_map_.erase(name_);
    };
    static Peer *GetPeer(std::string name) {
        tbb::mutex::scoped_lock lock(mutex_);
        PeerMap::const_iterator it = peer_map_.find(name);
        if (it == peer_map_.end())
            return NULL;
        return it->second;
    };
    void DelPeerRoutes(DelPeerDone cb);
    void PeerNotifyRoutes();
    void PeerNotifyMcastBcastRoutes(bool associate);
    bool ComparePath(const Peer *rhs) const {
        if  (type_ != rhs->type_) {
            return type_ < rhs->type_;
        }

        return name_ < rhs->name_;
    }

    const std::string &GetName() const { return name_; };
    const Type GetType() const { return type_; };

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
    

private:
    Type type_;
    std::string name_;
    static PeerMap peer_map_;
    static tbb::mutex mutex_;
    DBTableWalker::WalkId vrf_uc_walkid_;
    DBTableWalker::WalkId vrf_mc_walkid_;
    DISALLOW_COPY_AND_ASSIGN(Peer);
};

class BgpPeer : public Peer {
public:
    BgpPeer(std::string name, AgentXmppChannel *bgp_xmpp_peer, 
            DBTableBase::ListenerId id) : Peer(Peer::BGP_PEER, name) {
        bgp_xmpp_peer_ = bgp_xmpp_peer;
        id_ = id;
    }

    ~BgpPeer();

    void SetVrfListenerId(DBTableBase::ListenerId id) { id_ = id; }
    DBTableBase::ListenerId GetVrfExportListenerId() { return id_; } 
    AgentXmppChannel *GetBgpXmppPeer() { return bgp_xmpp_peer_; }    
private: 
    DBTableBase::ListenerId id_;
    AgentXmppChannel *bgp_xmpp_peer_;
};

#endif // vnsw_agent_peer_h_
