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
#include <oper/peer.h>

class AgentRoute;
class Peer;
class BgpPeer;
class VrfEntry;
class XmlPugi;
class PathPreference;
class AgentPath;
class EcmpLoadBalance;
class ControllerTimer;
class EndOfRibTxTimer;
class EndOfRibRxTimer;

class AgentXmppChannel {
public:
    static const uint32_t maximum_ecmp_paths = 128;

    AgentXmppChannel(Agent *agent,
                     const std::string &xmpp_server, 
                     const std::string &label_range, uint8_t xs_idx);
    virtual ~AgentXmppChannel();

    virtual std::string ToString() const;
    virtual bool SendUpdate(uint8_t *msg, size_t msgsize);
    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);
    virtual void ReceiveEvpnUpdate(XmlPugi *pugi);
    virtual void ReceiveMulticastUpdate(XmlPugi *pugi);
    virtual void ReceiveV4V6Update(XmlPugi *pugi);
    XmppChannel *GetXmppChannel() { return channel_; }
    void ReceiveBgpMessage(std::auto_ptr<XmlBase> impl);

    //Helper to identify if specified peer has active BGP peer attached
    static bool IsXmppChannelActive(const Agent *agent, AgentXmppChannel *peer);
    static bool IsBgpPeerActive(const Agent *agent, AgentXmppChannel *peer);
    static bool SetConfigPeer(AgentXmppChannel *peer);
    static void SetMulticastPeer(AgentXmppChannel *old_peer, 
                                 AgentXmppChannel *new_peer);
    static void CleanConfigStale(AgentXmppChannel *agent_xmpp_channel);
    static void XmppClientChannelEvent(AgentXmppChannel *peer,
                                       xmps::PeerState state);
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
                                       const Ip4Address *nexthop_ip,
                                       const VnListType &vn_list,
                                       uint32_t label,
                                       uint32_t tunnel_bmap,
                                       const SecurityGroupList *sg_list,
                                       const CommunityList *communities,
                                       Agent::RouteTableType type,
                                       const PathPreference &path_preference,
                                       const EcmpLoadBalance &ecmp_load_balance);
    static bool ControllerSendEvpnRouteAdd(AgentXmppChannel *peer,
                                           AgentRoute *route,
                                           const Ip4Address *nexthop_ip,
                                           std::string vn,
                                           uint32_t mpls_label,
                                           uint32_t tunnel_bmap,
                                           const SecurityGroupList *sg_list,
                                           const CommunityList *communities,
                                           const std::string &destination,
                                           const std::string &source,
                                           const PathPreference &path_preference);
    static bool ControllerSendMcastRouteAdd(AgentXmppChannel *peer,
                                            AgentRoute *route);
    //Deletes to control node
    static bool ControllerSendRouteDelete(AgentXmppChannel *peer,
                                          AgentRoute *route,
                                          const VnListType &vn_list,
                                          uint32_t label,
                                          uint32_t tunnel_bmap,
                                          const SecurityGroupList *sg_list,
                                          const CommunityList *communities,
                                          Agent::RouteTableType type,
                                          const PathPreference &path_preference);
    static bool ControllerSendEvpnRouteDelete(AgentXmppChannel *peer,
                                              AgentRoute *route,
                                              std::string vn,
                                              uint32_t mpls_label,
                                              const std::string &destination,
                                              const std::string &source,
                                              uint32_t tunnel_bmap);
    static bool ControllerSendMcastRouteDelete(AgentXmppChannel *peer,
                                               AgentRoute *route);
    //Channel event handlers
    void Ready();
    void NotReady();
    void TimedOut();

    //End of rib rx/tx
    void StartEndOfRibTxWalker();
    void StopEndOfRibTxWalker();
    void EndOfRibTx();
    void EndOfRibRx();

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
    void RegisterXmppChannel(XmppChannel *channel);

    std::string GetXmppServer() { return xmpp_server_; }
    uint8_t GetXmppServerIdx() { return xs_idx_; }
    std::string GetMcastLabelRange() { return label_range_; }

    Agent *agent() const {return agent_;}
    BgpPeer *bgp_peer_id() {
        return static_cast<BgpPeer *>(bgp_peer_id_.get());
    }
    PeerPtr bgp_peer_id_ref() {return bgp_peer_id_;}
    std::string GetBgpPeerName() const;
    void UpdateConnectionInfo(xmps::PeerState state);
    bool ControllerSendEvpnRouteCommon(AgentRoute *route,
                                       const Ip4Address *nexthop_ip,
                                       std::string vn,
                                       const SecurityGroupList *sg_list,
                                       const CommunityList *communities,
                                       uint32_t mpls_label,
                                       uint32_t tunnel_bmap,
                                       const std::string &destination,
                                       const std::string &source,
                                       const PathPreference &path_preference,
                                       bool associate);
    bool ControllerSendMcastRouteCommon(AgentRoute *route,
                                        bool associate);
    bool BuildEvpnMulticastMessage(autogen::EnetItemType &item,
                                   std::stringstream &node_id,
                                   AgentRoute *route,
                                   const Ip4Address *nh_ip,
                                   const std::string &vn,
                                   const SecurityGroupList *sg_list,
                                   const CommunityList *communities,
                                   uint32_t label,
                                   uint32_t tunnel_bmap,
                                   bool associate,
                                   const AgentPath *path,
                                   bool assisted_replication);
    void AddEvpnRoute(const std::string &vrf_name, std::string mac_addr,
                      autogen::EnetItemType *item);
    uint64_t route_published_time() const {return route_published_time_;}
    EndOfRibTxTimer *end_of_rib_tx_timer();
    EndOfRibRxTimer *end_of_rib_rx_timer();
    //Sequence number for this channel
    uint64_t sequence_number() const;

protected:
    virtual void WriteReadyCb(const boost::system::error_code &ec);

private:
    void PeerIsNotConfig();
    InetUnicastAgentRouteTable *PrefixToRouteTable(const std::string &vrf_name,
                                                   const IpAddress &prefix_addr);
    void ReceiveInternal(const XmppStanza::XmppMessage *msg);
    void AddRoute(std::string vrf_name, IpAddress ip, uint32_t plen,
                  autogen::ItemType *item);
    void AddMulticastEvpnRoute(const std::string &vrf_name,
                               const MacAddress &mac,
                               autogen::EnetItemType *item);
    void AddRemoteRoute(std::string vrf_name, IpAddress ip, uint32_t plen,
                        autogen::ItemType *item,
                        const VnListType &vn_list);
    void AddEcmpRoute(std::string vrf_name, IpAddress ip, uint32_t plen,
                      autogen::ItemType *item,
                      const VnListType &vn_list);
    //Common helpers
    bool ControllerSendV4V6UnicastRouteCommon(AgentRoute *route,
                                            const VnListType &vn_list,
                                            const SecurityGroupList *sg_list,
                                            const CommunityList *communities,
                                            uint32_t mpls_label,
                                            uint32_t tunnel_bmap,
                                            const PathPreference &path_preference,
                                            bool associate,
                                            Agent::RouteTableType type,
                                            const EcmpLoadBalance &ecmp_load_balance);
    bool BuildTorMulticastMessage(autogen::EnetItemType &item,
                                  std::stringstream &node_id,
                                  AgentRoute *route,
                                  const Ip4Address *nh_ip,
                                  const std::string &vn,
                                  const SecurityGroupList *sg_list,
                                  const CommunityList *communities,
                                  uint32_t label,
                                  uint32_t tunnel_bmap,
                                  const std::string &destination,
                                  const std::string &source,
                                  bool associate);
    bool BuildEvpnUnicastMessage(autogen::EnetItemType &item,
                                 std::stringstream &node_id,
                                 AgentRoute *route,
                                 const Ip4Address *nh_ip,
                                 const std::string &vn,
                                 const SecurityGroupList *sg_list,
                                 const CommunityList *communities,
                                 uint32_t label,
                                 uint32_t tunnel_bmap,
                                 const PathPreference &path_prefernce,
                                 bool associate);
    bool BuildAndSendEvpnDom(autogen::EnetItemType &item,
                             std::stringstream &ss_node,
                             const AgentRoute *route,
                             bool associate);
    bool IsEcmp(const std::vector<autogen::NextHopType> &nexthops);
    void GetVnList(const std::vector<autogen::NextHopType> &nexthops,
                   VnListType *vn_list);

    XmppChannel *channel_;
    std::string xmpp_server_;
    std::string label_range_;
    uint8_t xs_idx_;
    PeerPtr bgp_peer_id_;
    uint64_t route_published_time_;
    boost::scoped_ptr<EndOfRibTxTimer> end_of_rib_tx_timer_;
    boost::scoped_ptr<EndOfRibRxTimer> end_of_rib_rx_timer_;
    Agent *agent_;
};

#endif // __CONTROLLER_PEER_H__
