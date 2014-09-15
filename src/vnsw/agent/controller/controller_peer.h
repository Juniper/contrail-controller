/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONTROLLER_PEER_H__
#define __CONTROLLER_PEER_H__

#include <map>
#include <string>

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <xmpp/xmpp_channel.h>
#include <xmpp_enet_types.h>
#include <xmpp_unicast_types.h>
#include <cmn/agent.h>

class AgentRoute;
class Peer;
class BgpPeer;
class VrfEntry;
class XmlPugi;
class PathPreference;

class AgentXmppChannel {
public:
    AgentXmppChannel(Agent *agent,
                     const std::string &xmpp_server, 
                     const std::string &label_range, uint8_t xs_idx);
    virtual ~AgentXmppChannel();

    virtual std::string ToString() const;
    virtual bool SendUpdate(uint8_t *msg, size_t msgsize);
    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);
    virtual void ReceiveEvpnUpdate(XmlPugi *pugi);
    virtual void ReceiveMulticastUpdate(XmlPugi *pugi);
    XmppChannel *GetXmppChannel() { return channel_; }

    //Helper to identify if specified peer has active BGP peer attached
    static bool IsBgpPeerActive(AgentXmppChannel *peer);
    static bool SetConfigPeer(AgentXmppChannel *peer);
    static void SetMulticastPeer(AgentXmppChannel *old_peer, 
                                 AgentXmppChannel *new_peer);
    static void CleanConfigStale(AgentXmppChannel *agent_xmpp_channel);
    static void CleanUnicastStale(AgentXmppChannel *agent_xmpp_channel);
    static void CleanMulticastStale(AgentXmppChannel *agent_xmpp_channel);
    static void UnicastPeerDown(AgentXmppChannel *peer, BgpPeer *peer_id);
    static void MulticastPeerDown(AgentXmppChannel *old_channel, 
                                  AgentXmppChannel *new_channel);
    static void HandleAgentXmppClientChannelEvent(AgentXmppChannel *peer,
                                                  xmps::PeerState state);
    static bool ControllerSendCfgSubscribe(AgentXmppChannel *peer);
    static bool ControllerSendVmCfgSubscribe(AgentXmppChannel *peer,
            const boost::uuids::uuid &vm_id, bool subscribe);
    static bool ControllerSendSubscribe(AgentXmppChannel *peer,
                                        VrfEntry *vrf,
                                        bool subscribe);
    //Add to control-node
    static bool ControllerSendRouteAdd(AgentXmppChannel *peer,
                                       AgentRoute *route,
                                       std::string vn,
                                       uint32_t label,
                                       uint32_t tunnel_bmap,
                                       const SecurityGroupList *sg_list,
                                       Agent::RouteTableType type,
                                       const PathPreference &path_preference);
    static bool ControllerSendEvpnRouteAdd(AgentXmppChannel *peer,
                                           AgentRoute *route,
                                           std::string vn,
                                           uint32_t mpls_label,
                                           uint32_t tunnel_bmap);
    static bool ControllerSendMcastRouteAdd(AgentXmppChannel *peer,
                                            AgentRoute *route);
    //Deletes to control node
    static bool ControllerSendRouteDelete(AgentXmppChannel *peer,
                                          AgentRoute *route,
                                          std::string vn,
                                          uint32_t label,
                                          uint32_t tunnel_bmap,
                                          const SecurityGroupList *sg_list,
                                          Agent::RouteTableType type,
                                          const PathPreference &path_preference);
    static bool ControllerSendEvpnRouteDelete(AgentXmppChannel *peer,
                                              AgentRoute *route,
                                              std::string vn,
                                              uint32_t mpls_label,
                                              uint32_t tunnel_bmap);
    static bool ControllerSendMcastRouteDelete(AgentXmppChannel *peer,
                                               AgentRoute *route);

    // Routines for BGP peer manipulations, lifecycle of bgp peer in xmpp
    // channel is as follows:
    // 1) Created whenever channel is xmps::READY
    // 2) When channel moves out of READY state, bgp peer moves to decommisioned
    // list. Once moved there it can never go back to active peer and can only
    // get deleted later.
    // 3) On arrival of some other active peer(i.e. channel is READY) cleanup
    // timers are started, expiration of which triggers removal of
    // decommissioned peer and eventually gets destroyed.
    void CreateBgpPeer();
    void DeCommissionBgpPeer();
    void RegisterXmppChannel(XmppChannel *channel);

    std::string controller_ifmap_xmpp_server() { return xmpp_server_; }
    uint8_t GetXmppServerIdx() { return xs_idx_; }
    std::string GetMcastLabelRange() { return label_range_; }

    Agent *agent() const {return agent_;}
    BgpPeer *bgp_peer_id() const {return bgp_peer_id_.get();}
    std::string GetBgpPeerName() const;
    void UpdateConnectionInfo(xmps::PeerState state);

    //Unicast peer identifier
    void increment_unicast_sequence_number() {unicast_sequence_number_++;}
    uint64_t unicast_sequence_number() const {return unicast_sequence_number_;}

    //Common helpers
    bool ControllerSendV4UnicastRouteCommon(AgentRoute *route,
                                            std::string vn,
                                            const SecurityGroupList *sg_list,
                                            uint32_t mpls_label,
                                            uint32_t tunnel_bmap,
                                            const PathPreference &path_preference,
                                            bool associate);
    bool ControllerSendEvpnRouteCommon(AgentRoute *route,
                                       std::string vn,
                                       uint32_t mpls_label,
                                       uint32_t tunnel_bmap,
                                       bool associate);
    bool ControllerSendMcastRouteCommon(AgentRoute *route,
                                        bool associate);

protected:
    virtual void WriteReadyCb(const boost::system::error_code &ec);

private:
    void ReceiveInternal(const XmppStanza::XmppMessage *msg);
    void AddRoute(std::string vrf_name, Ip4Address ip, uint32_t plen, 
                  autogen::ItemType *item);
    void AddMulticastEvpnRoute(std::string vrf_name, struct ether_addr &mac,
                               autogen::EnetItemType *item);
    void AddEvpnRoute(std::string vrf_name, std::string mac_addr,
                      autogen::EnetItemType *item);
    void AddRemoteRoute(std::string vrf_name, Ip4Address ip, uint32_t plen, 
                        autogen::ItemType *item);
    void AddEcmpRoute(std::string vrf_name, Ip4Address ip, uint32_t plen, 
                      autogen::ItemType *item);
    XmppChannel *channel_;
    std::string xmpp_server_;
    std::string label_range_;
    uint8_t xs_idx_;
    boost::shared_ptr<BgpPeer> bgp_peer_id_;
    Agent *agent_;
    uint64_t unicast_sequence_number_;
};

#endif // __CONTROLLER_PEER_H__
