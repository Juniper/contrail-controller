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
#include "bgp_l3vpn_unicast_types.h"

class Inet4Route;
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
                                    Inet4UcRoute *route, std::string vn,
                                    uint32_t mpls_label, const SecurityGroupList *sg_list,
                                    bool add_route);
    static bool ControllerSendMcastRoute(AgentXmppChannel *peer,
                                         Inet4Route *route, bool add_route);
    Peer *GetBgpPeer() { return bgp_peer_id_; }
    std::string GetXmppServer() { return xmpp_server_; }
    uint8_t GetXmppServerIdx() { return xs_idx_; }
    std::string GetMcastLabelRange() { return label_range_; }

protected:
    virtual void WriteReadyCb(const boost::system::error_code &ec);

private:
    void ReceiveInternal(const XmppStanza::XmppMessage *msg);
    void BgpPeerDelDone();
    void AddRoute(std::string vrf_name, Ip4Address ip, uint32_t plen, 
                  autogen::ItemType *item);
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
