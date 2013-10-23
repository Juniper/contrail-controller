/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONTROLLER_PEER_H__
#define __CONTROLLER_PEER_H__

#include <map>
#include <string>

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include "xmpp/xmpp_channel.h"
#include "xmpp_enet_types.h"
#include "xmpp_unicast_types.h"
#include <oper/route_types.h>

class RouteEntry;
class Peer;
class VrfEntry;
class XmlPugi;

class AgentXmppChannel {
public:
    explicit AgentXmppChannel(XmppChannel *channel);
    AgentXmppChannel(XmppChannel *channel, std::string xmpp_server, 
                     std::string label_range, uint8_t xs_idx);
    virtual ~AgentXmppChannel();

    virtual std::string ToString() const;
    virtual bool SendUpdate(uint8_t *msg, size_t msgsize);
    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);
    virtual void ReceiveEvpnUpdate(XmlPugi *pugi);
    virtual void ReceiveMulticastUpdate(XmlPugi *pugi);
    XmppChannel *GetXmppChannel() { return channel_; }
    static void HandleXmppClientChannelEvent(AgentXmppChannel *peer, 
                                             xmps::PeerState state);
    static bool ControllerSendCfgSubscribe(AgentXmppChannel *peer);
    static bool ControllerSendVmCfgSubscribe(AgentXmppChannel *peer, 
            const boost::uuids::uuid &vm_id, bool subscribe);
    static bool ControllerSendSubscribe(AgentXmppChannel *peer,
                                        VrfEntry *vrf,
                                        bool subscribe);
    static bool ControllerSendRoute(AgentXmppChannel *peer,
                                    RouteEntry *route, std::string vn,
                                    uint32_t label, 
                                    uint32_t tunnel_bmap,
                                    const SecurityGroupList *sg_list, 
                                    bool add_route, 
                                    AgentRouteTableAPIS::TableType type);
    static bool ControllerSendMcastRoute(AgentXmppChannel *peer,
                                         RouteEntry *route, bool add_route);
    static bool ControllerSendV4UnicastRoute(AgentXmppChannel *peer,
                                             RouteEntry *route, 
                                             std::string vn,
                                             const SecurityGroupList *sg_list,
                                             uint32_t mpls_label, bool add_route);
    static bool ControllerSendEvpnRoute(AgentXmppChannel *peer,
                                        RouteEntry *route, 
                                        std::string vn,
                                        uint32_t mpls_label, 
                                        uint32_t tunnel_bmap, 
                                        bool add_route);
    Peer *GetBgpPeer() { return bgp_peer_id_; }
    std::string GetXmppServer() { return xmpp_server_; }
    uint8_t GetXmppServerIdx() { return xs_idx_; }
    std::string GetMcastLabelRange() { return label_range_; }

protected:
    virtual void WriteReadyCb(const boost::system::error_code &ec);

private:
    void ReceiveInternal(const XmppStanza::XmppMessage *msg);
    void BgpPeerDelDone();
    void AddEvpnRoute(std::string vrf_name, struct ether_addr &mac, 
                  autogen::EnetItemType *item);
    void AddRoute(std::string vrf_name, Ip4Address ip, uint32_t plen, 
                  autogen::ItemType *item);
    void AddRemoteEvpnRoute(std::string vrf_name, struct ether_addr &mac, 
                        autogen::EnetItemType *item);
    void AddRemoteRoute(std::string vrf_name, Ip4Address ip, uint32_t plen, 
                        autogen::ItemType *item);
    void AddEcmpRoute(std::string vrf_name, Ip4Address ip, uint32_t plen, 
                      autogen::ItemType *item);
    XmppChannel *channel_;
    std::string xmpp_server_;
    std::string label_range_;
    uint8_t xs_idx_;
    Peer *bgp_peer_id_;
};

#endif // __CONTROLLER_PEER_H__
