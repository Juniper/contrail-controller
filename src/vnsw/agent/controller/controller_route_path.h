/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef controller_route_path_hpp
#define controller_route_path_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/route_common.h>

//Forward declaration
class AgentXmppChannel;
class AgentRouteData;
class Peer;
class BgpPeer;
class TunnelNHKey;
class TunnelNHData;
class TunnelType;
class ControllerVmRoute;
class LocalVmRoute;
class InetInterfaceRoute;
class VlanNhRoute;
class VNController;

/*
 * Contains all Controller Route data definition.
 * Currently there are four kind of routes added from controller:
 * - Remote - Native to controller, derived from ControllerPeerPath
 * - Local - Derived from LocalVmRoute of agent_path
 * - Vlan - Derived from VlanNHRoute from agent_path
 * - Inet Interface - Derived from InetInterfaceRoute from agent_path.
 *
 * All route data passed from controller with peer type BGP, should
 * carry AgentXmppChannel and unicast_sequence_number. This is required
 * because BGP peer can go off by the time request for add/change/delete of
 * route is enqueued in DB. At Route input every data is checked for peer
 * validitiy. BGP peer will be valid if AgentXmppChannel is still up and
 * sequence number matches.
 *
 * ControllerPeerPath implements sequence number and agent_xmpp_channel,
 * so if there is native controller data it can be derived from same.
 *
 * In case data is derived from agent_path datas, then he derivative needs to
 * carry info on AgentXmppChannel and sequence number and has to implement
 * check for peer validity.
 */

class ControllerPeerPath : public AgentRouteData {
public:
    static const uint64_t kInvalidPeerIdentifier = 0xFFFFFFFFFFFFFFFFLL;
    ControllerPeerPath(const Peer *peer);
    ~ControllerPeerPath() { }

    // Only to be used for tests
    void set_sequence_number(uint64_t sequence_number) {
        sequence_number_ = sequence_number;
    }
    const AgentXmppChannel *channel() const {return channel_;}
    uint64_t sequence_number() const {return sequence_number_;}

private:
    const Peer *peer_;
    const AgentXmppChannel *channel_;
    uint64_t sequence_number_;
};

/*
 * Implementation for adding remote route data.
 */
class ControllerVmRoute : public ControllerPeerPath {
public:
    ControllerVmRoute(const Peer *peer, const string &vrf_name,
                  const Ip4Address &addr, uint32_t label,
                  const string &dest_vn_name, int bmap,
                  const SecurityGroupList &sg_list,
                  const PathPreference &path_preference,
                  DBRequest &req):
        ControllerPeerPath(peer), server_vrf_(vrf_name), server_ip_(addr),
        tunnel_bmap_(bmap), label_(label), dest_vn_name_(dest_vn_name),
        sg_list_(sg_list),path_preference_(path_preference)
        {nh_req_.Swap(&req);}
    // Data passed in case of delete from BGP peer, to validate 
    // the request at time of processing.
    ControllerVmRoute(const Peer *peer) : ControllerPeerPath(peer) { }
    virtual ~ControllerVmRoute() { }

    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual string ToString() const {return "remote VM";}
    virtual bool IsPeerValid() const;
    const SecurityGroupList &sg_list() const {return sg_list_;}
    static ControllerVmRoute *MakeControllerVmRoute(const Peer *peer,
                                            const string &default_vrf,
                                            const Ip4Address &router_id,
                                            const string &vrf_name,
                                            const Ip4Address &server_ip, 
                                            TunnelType::TypeBmap bmap,
                                            uint32_t label,
                                            const string &dest_vn_name,
                                            const SecurityGroupList &sg_list,
                                            const PathPreference
                                            &path_preference);

private:
    string server_vrf_;
    Ip4Address server_ip_;
    TunnelType::TypeBmap tunnel_bmap_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
    PathPreference path_preference_;
    DBRequest nh_req_;
    DISALLOW_COPY_AND_ASSIGN(ControllerVmRoute);
};

class ControllerEcmpRoute : public ControllerPeerPath {
public:
    ControllerEcmpRoute(const Peer *peer, const Ip4Address &dest_addr,
                        uint8_t plen, const string &vn_name, uint32_t label,
                        bool local_ecmp_nh, const string &vrf_name,
                        SecurityGroupList sg_list,
                        const PathPreference &path_preference,
                        DBRequest &nh_req) :
        ControllerPeerPath(peer), dest_addr_(dest_addr), plen_(plen),
        vn_name_(vn_name), label_(label), local_ecmp_nh_(local_ecmp_nh),
        vrf_name_(vrf_name), sg_list_(sg_list), path_preference_(path_preference)
        {nh_req_.Swap(&nh_req);}

    virtual ~ControllerEcmpRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *);
    virtual string ToString() const {return "inet4 ecmp";}
    virtual bool IsPeerValid() const;

private:
    Ip4Address dest_addr_;
    uint8_t plen_;
    string vn_name_;
    uint32_t label_;
    bool local_ecmp_nh_;
    string vrf_name_;
    SecurityGroupList sg_list_;
    PathPreference path_preference_;
    DBRequest nh_req_;
    DISALLOW_COPY_AND_ASSIGN(ControllerEcmpRoute);
};

/*
 * Route data for adding local route with BGP peer.
 * Send agent_xmpp_channel and sequence number to
 * validate peeer validity at the time of route operation
 */
class ControllerLocalVmRoute : public LocalVmRoute {
public:
    ControllerLocalVmRoute(const VmInterfaceKey &intf, uint32_t mpls_label,
                           uint32_t vxlan_id, bool force_policy,
                           const string &vn_name, uint8_t flags,
                           const SecurityGroupList &sg_list,
                           const PathPreference &path_preference,
                           uint64_t sequence_number,
                           const AgentXmppChannel *channel);
    virtual ~ControllerLocalVmRoute() { }
    virtual bool IsPeerValid() const;

private:
    uint64_t sequence_number_;
    const AgentXmppChannel *channel_;
    DISALLOW_COPY_AND_ASSIGN(ControllerLocalVmRoute);
};

/*
 * Route data for adding Inet Interface route with BGP peer.
 * Send agent_xmpp_channel and sequence number to
 * validate peeer validity at the time of route operation
 */
class ControllerInetInterfaceRoute : public InetInterfaceRoute {
public:
    ControllerInetInterfaceRoute(const InetInterfaceKey &intf, uint32_t label,
                                 int tunnel_bmap, const string &dest_vn_name,
                                 uint64_t sequence_number,
                                 const AgentXmppChannel *channel);
    virtual ~ControllerInetInterfaceRoute() { }
    virtual bool IsPeerValid() const;

private:
    uint64_t sequence_number_;
    const AgentXmppChannel *channel_;
    DISALLOW_COPY_AND_ASSIGN(ControllerInetInterfaceRoute);
};

/*
 * Route data for adding Vlan route with BGP peer.
 * Send agent_xmpp_channel and sequence number to
 * validate peeer validity at the time of route operation
 */
class ControllerVlanNhRoute : public VlanNhRoute {
public:
    ControllerVlanNhRoute(const VmInterfaceKey &intf, uint32_t tag,
                          uint32_t label, const string &dest_vn_name,
                          const SecurityGroupList &sg_list,
                          const PathPreference &path_preference,
                          uint64_t sequence_number,
                          const AgentXmppChannel *channel);
    virtual ~ControllerVlanNhRoute() { }
    virtual bool IsPeerValid() const;

private:
    uint64_t sequence_number_;
    const AgentXmppChannel *channel_;
    DISALLOW_COPY_AND_ASSIGN(ControllerVlanNhRoute);
};

/*
 * ClonedLocalPath would be used to pick nexthop from the
 * local peer, instead of nexthop pointed by mpls label.
 * Currently it gets used in gateway interface. In case of
 * gateway interface, label exported by agent would point
 * to table nexthop, and the prefix route of gateway
 * interface would point resolve nexthop, so that ARP resolution
 * can be triggered when packet hits the subnet route.
 */
class ClonedLocalPath : public AgentRouteData {
public:
    ClonedLocalPath(uint64_t seq, const AgentXmppChannel *channel,
                    uint32_t label, const std::string &vn,
                    const SecurityGroupList &sg_list):
        AgentRouteData(false), sequence_number_(seq),
        channel_(channel), mpls_label_(label), vn_(vn), sg_list_(sg_list) {}
    virtual ~ClonedLocalPath() {}
    virtual bool IsPeerValid() const;
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {
        return "Nexthop cloned from local path";
    }
private:
    uint64_t sequence_number_;
    const AgentXmppChannel *channel_;
    uint32_t mpls_label_;
    const std::string vn_;
    const SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(ClonedLocalPath);
};
#endif //controller_route_path_hpp
