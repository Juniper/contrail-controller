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
#define LOCAL_VM_PORT_PEER_NAME "LocalVmPort"
#define NOVA_PEER_NAME "Nova"
#define LINKLOCAL_PEER_NAME "LinkLocal"
#define ECMP_PEER_NAME "Ecmp"
#define VGW_PEER_NAME "Vgw"
#define EVPN_PEER_NAME "EVPN"
#define MULTICAST_PEER_NAME "Multicast"
#define MULTICAST_TOR_PEER_NAME "Multicast TOR"
#define MULTICAST_FABRIC_TREE_BUILDER_NAME "MulticastTreeBuilder"
#define MAC_VM_BINDING_PEER_NAME "MacVmBindingPeer"

class AgentXmppChannel;
class ControllerRouteWalker;
class VrfTable;
class AgentPath;

class Peer {
public:
    typedef std::map<std::string, Peer *> PeerMap;
    typedef std::pair<std::string, Peer *> PeerPair;
    enum Type {
        MULTICAST_PEER,
        EVPN_PEER,
        BGP_PEER,
        LINKLOCAL_PEER,
        ECMP_PEER,
        LOCAL_VM_PORT_PEER,
        LOCAL_VM_PEER,
        LOCAL_PEER,
        NOVA_PEER,
        VGW_PEER,
        MULTICAST_FABRIC_TREE_BUILDER,
        OVS_PEER,
        MULTICAST_TOR_PEER,
        MAC_VM_BINDING_PEER
    };

    Peer(Type type, const std::string &name, bool controller_export);
    virtual ~Peer();

    bool IsLess(const Peer *rhs) const {
        if  (type_ != rhs->type_) {
            return type_ < rhs->type_;
        }

        return Compare(rhs);
    }
    virtual bool Compare(const Peer *rhs) const {return false;}
    // Should we export path from this peer to controller?
    virtual bool export_to_controller() const {return export_to_controller_;}
    virtual const Ip4Address *NexthopIp(Agent *agent,
                                        const AgentPath *path) const;
    virtual bool NeedValidityCheck() const {return false;}

    const std::string &GetName() const { return name_; }
    const Type GetType() const { return type_; }

private:
    Type type_;
    std::string name_;
    bool export_to_controller_;
    DISALLOW_COPY_AND_ASSIGN(Peer);
};

// Peer used for BGP paths
class BgpPeer : public Peer {
public:
    typedef boost::function<void()> DelPeerDone;
    BgpPeer(const Ip4Address &server_ip, const std::string &name,
            boost::shared_ptr<AgentXmppChannel> bgp_xmpp_peer,
            DBTableBase::ListenerId id,
            Peer::Type bgp_peer_type);
    virtual ~BgpPeer();
    virtual bool NeedValidityCheck() const {return true;}

    bool Compare(const Peer *rhs) const {
        const BgpPeer *bgp = static_cast<const BgpPeer *>(rhs);
        return server_ip_ < bgp->server_ip_;
    }

    // For testing
    void SetVrfListenerId(DBTableBase::ListenerId id) { id_ = id; }
    DBTableBase::ListenerId GetVrfExportListenerId() { return id_; } 
    AgentXmppChannel *GetBgpXmppPeer() const { return bgp_xmpp_peer_.get(); }
    const AgentXmppChannel *GetBgpXmppPeerConst() const {
        return bgp_xmpp_peer_.get();
    }

    // Table Walkers
    void DelPeerRoutes(DelPeerDone walk_done_cb);
    void PeerNotifyRoutes();
    void PeerNotifyMulticastRoutes(bool associate);
    void StalePeerRoutes();

    bool is_disconnect_walk() const {return is_disconnect_walk_;}
    void set_is_disconnect_walk(bool is_disconnect_walk) {
        is_disconnect_walk_ = is_disconnect_walk;
    }
    ControllerRouteWalker *route_walker() const {
        return route_walker_.get(); 
    }

    //Helper routines to get export state for vrf and route
    DBState *GetVrfExportState(DBTablePartBase *partition,
                               DBEntryBase *e);
    DBState *GetRouteExportState(DBTablePartBase *partition,
                                 DBEntryBase *e);
    void DeleteVrfState(DBTablePartBase *partition, DBEntryBase *entry);

    uint32_t setup_time() const {return setup_time_;}
    Agent *agent() const;

private: 
    Ip4Address server_ip_;
    DBTableBase::ListenerId id_;
    uint32_t setup_time_;
    boost::shared_ptr<AgentXmppChannel> bgp_xmpp_peer_;
    tbb::atomic<bool> is_disconnect_walk_;
    boost::scoped_ptr<ControllerRouteWalker> route_walker_;
    DISALLOW_COPY_AND_ASSIGN(BgpPeer);
};

// Peer for local-vm-port paths. There can be multiple VMs with same IP.
// They are all added as different path. ECMP path will consolidate all 
// local-vm-port paths
class LocalVmPortPeer : public Peer {
public:
    LocalVmPortPeer(const std::string &name, uint64_t handle) :
        Peer(Peer::LOCAL_VM_PORT_PEER, name, true), handle_(handle) {
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
    EcmpPeer() : Peer(Peer::ECMP_PEER, "ECMP", true) { }
    virtual ~EcmpPeer() { }

    bool Compare(const Peer *rhs) const { return false; }
    bool ExportToController() const {return true;}
private:
    DISALLOW_COPY_AND_ASSIGN(EcmpPeer);
};

// EVPN peer
class EvpnPeer : public Peer {
public:
    typedef boost::shared_ptr<EvpnPeer> EvpnPeerRef;

    EvpnPeer() : Peer(Peer::EVPN_PEER, "EVPN", false) { }
    virtual ~EvpnPeer() { }

    bool Compare(const Peer *rhs) const { return false; }
    bool ExportToController() const {return false;}
private:
    DISALLOW_COPY_AND_ASSIGN(EvpnPeer);
};
#endif // vnsw_agent_peer_h_
