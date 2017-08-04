/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/util.h>
#include <base/logging.h>
#include <base/connection_info.h>
#include <net/bgp_af.h>
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "controller/controller_peer.h"
#include "controller/controller_vrf_export.h"
#include "controller/controller_init.h"
#include "controller/controller_ifmap.h"
#include "oper/operdb_init.h"
#include "oper/vrf.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/mirror_table.h"
#include "oper/multicast.h"
#include "oper/peer.h"
#include "oper/vxlan.h"
#include "oper/agent_path.h"
#include "oper/ecmp_load_balance.h"
#include "cmn/agent_stats.h"
#include <pugixml/pugixml.hpp>
#include "xml/xml_pugi.h"
#include "xmpp/xmpp_init.h"
#include "xmpp_multicast_types.h"
#include "ifmap/ifmap_agent_table.h"
#include "controller/controller_types.h"
#include "net/tunnel_encap_type.h"
#include <assert.h>
#include <controller/controller_route_path.h>

using namespace boost::asio;
using namespace autogen;
using process::ConnectionType;
using process::ConnectionStatus;
using process::ConnectionState;

// Parses string ipv4-addr/plen or ipv6-addr/plen
// Stores address in addr and returns plen
static int ParseAddress(const string &str, IpAddress *addr) {
    size_t pos = str.find('/');
    if (pos == string::npos) {
        return -1;
    }

    int plen = 0;
    boost::system::error_code ec;
    string plen_str = str.substr(pos + 1);
    if (plen_str == "32") {
        Ip4Address ip4_addr;
        ec = Ip4PrefixParse(str, &ip4_addr, &plen);
        if (ec || plen != 32) {
            return -1;
        }
        *addr = ip4_addr;
    } else if (plen_str == "128") {
        Ip6Address ip6_addr;
        ec = Inet6PrefixParse(str, &ip6_addr, &plen);
        if (ec || plen != 128) {
            return -1;
        }
        *addr = ip6_addr;
    } else {
        return -1;
    }
    return plen;
}

AgentXmppChannel::AgentXmppChannel(Agent *agent,
                                   const std::string &xmpp_server,
                                   const std::string &label_range,
                                   uint8_t xs_idx)
    : channel_(NULL), channel_str_(),
      xmpp_server_(xmpp_server), label_range_(label_range),
      xs_idx_(xs_idx), route_published_time_(0), agent_(agent) {
    bgp_peer_id_.reset();
    end_of_rib_tx_timer_.reset(new EndOfRibTxTimer(agent));
    end_of_rib_rx_timer_.reset(new EndOfRibRxTimer(agent));
    CreateBgpPeer();
}

AgentXmppChannel::~AgentXmppChannel() {
    end_of_rib_tx_timer_.reset();
    end_of_rib_rx_timer_.reset();
}

void AgentXmppChannel::Unregister() {
    if (bgp_peer_id()) {
        bgp_peer_id()->StopRouteExports();
    }
    channel_->UnRegisterWriteReady(xmps::BGP);
    channel_->UnRegisterReceive(xmps::BGP);
    channel_ = NULL;
}

InetUnicastAgentRouteTable *AgentXmppChannel::PrefixToRouteTable
    (const std::string &vrf_name, const IpAddress &prefix_addr) {
    InetUnicastAgentRouteTable *rt_table = NULL;

    if (prefix_addr.is_v4()) {
        rt_table = agent_->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    } else if (prefix_addr.is_v6()) {
        rt_table = agent_->vrf_table()->GetInet6UnicastRouteTable(vrf_name);
    }
    if (rt_table == NULL) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Unable to fetch route table for prefix " +
                         prefix_addr.to_string());
    }
    return rt_table;
}

void AgentXmppChannel::RegisterXmppChannel(XmppChannel *channel) {
    if (channel == NULL)
        return;

    channel_ = channel;
    channel_str_ = channel_->ToString();
    channel->RegisterReceive(xmps::BGP,
                              boost::bind(&AgentXmppChannel::ReceiveInternal,
                                          this, _1));
}

std::string AgentXmppChannel::GetBgpPeerName() const {
    if (bgp_peer_id_.get() == NULL)
        return "No BGP peer";

    return bgp_peer_id_.get()->GetName();
}

void AgentXmppChannel::CreateBgpPeer() {
    assert(bgp_peer_id_.get() == NULL);
    DBTableBase::ListenerId id =
        agent_->vrf_table()->Register(boost::bind(&VrfExport::Notify,
                                       agent_, this, _1, _2));
    boost::system::error_code ec;
    const string &addr = agent_->controller_ifmap_xmpp_server(xs_idx_);
    Ip4Address ip = Ip4Address::from_string(addr.c_str(), ec);
    assert(ec.value() == 0);
    bgp_peer_id_.reset(new BgpPeer(this, ip, addr, id, Peer::BGP_PEER));
}

bool AgentXmppChannel::SendUpdate(uint8_t *msg, size_t size) {

    if (agent_->stats())
        agent_->stats()->incr_xmpp_out_msgs(xs_idx_);

    return channel_->Send(msg, size, xmps::BGP,
                          boost::bind(&AgentXmppChannel::WriteReadyCb, this, _1));
}

void AgentXmppChannel::ReceiveEvpnUpdate(XmlPugi *pugi) {
    pugi::xml_node node = pugi->FindNode("items");
    pugi::xml_attribute attr = node.attribute("node");

    char *saveptr;
    strtok_r(const_cast<char *>(attr.value()), "/", &saveptr);
    strtok_r(NULL, "/", &saveptr);
    char *vrf_name =  strtok_r(NULL, "", &saveptr);
    const std::string vrf(vrf_name);
    EvpnAgentRouteTable *rt_table =
        static_cast<EvpnAgentRouteTable *>
        (agent_->vrf_table()->GetEvpnRouteTable(vrf_name));
    if (rt_table == NULL) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                    "Invalid VRF. Ignoring route retract" +
                                    string(attr.value()));
        return;
    }

    pugi::xml_node node_check = pugi->FindNode("retract");
    if (!pugi->IsNull(node_check)) {
        for (node = node.first_child(); node; node = node.next_sibling()) {
            if (strcmp(node.name(), "retract") == 0)  {
                std::string id = node.first_attribute().value();
                CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                            "EVPN Delete Node id:" + id);

                char buff[id.length() + 1];
                strcpy(buff, id.c_str());

                // retract does not have nlri. Need to decode key fields from
                // retract id. Format of retract-id expected are:
                // 00:00:00:01:01:01,1.1.1.1/32 - Mac and IP for Non-VXLAN Encap
                // 10-00:00:00:01:01:01,1.1.1.1/32 - VXLAN, mac, ip.
                //
                // In case of not finding pattern "-" whole string will be
                // returned in token. So dont use it for ethernet_tag.
                // Check for string length of saveptr to know if string was
                // tokenised.

                uint16_t offset = 0;
                uint32_t ethernet_tag = 0;
                saveptr = NULL;

                // If id has "-", the value before "-" is treated as
                // ethernet-tag
                char *token = strtok_r(buff + offset, "-", &saveptr);
                if ((strlen(saveptr) != 0) && token) {
                    ethernet_tag = atoi(token);
                    offset += strlen(token) + 1;
                }

                // Get MAC address. Its delimited by ","
                token = strtok_r(buff + offset, ",", &saveptr);
                if ((strlen(saveptr) == 0) || (token == NULL)) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                     "Error parsing MAC from retract-id: " +id);
                    continue;
                }

                boost::system::error_code ec;
                MacAddress mac(token, &ec);
                if (ec) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                     "Error decoding MAC from retract-id: "+id);
                    continue;
                }

                offset += strlen(token) + 1;
                IpAddress ip_addr;
                if (ParseAddress(buff + offset, &ip_addr) < 0) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                     "Error decoding IP address from "
                                     "retract-id: "+id);
                    continue;
                }

                if (mac == MacAddress::BroadcastMac()) {
                    //Deletes the peer path for all boradcast and
                    //traverses the subnet route in VRF to issue delete of peer
                    //for them as well.
                    TunnelOlist olist;
                    agent_->oper_db()->multicast()->
                        ModifyEvpnMembers(bgp_peer_id(),
                                          vrf_name, olist,
                                          ethernet_tag,
                             ControllerPeerPath::kInvalidPeerIdentifier);

                    //Ideally in non TSN node leaf olist is not to be
                    //present
                    if (agent_->tsn_enabled() == false)
                        return;
                    agent_->oper_db()->multicast()->
                        ModifyTorMembers(bgp_peer_id(),
                                         vrf_name, olist,
                                         ethernet_tag,
                             ControllerPeerPath::kInvalidPeerIdentifier);
                } else {
                    rt_table->DeleteReq(bgp_peer_id(), vrf_name, mac,
                                        ip_addr, ethernet_tag,
                                        new ControllerVmRoute(bgp_peer_id()));
                }
            }
        }
        return;
    }

    //Call Auto-generated Code to return struct
    auto_ptr<AutogenProperty> xparser(new AutogenProperty());
    if (EnetItemsType::XmlParseProperty(node, &xparser) == false) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Xml Parsing for evpn Failed");
        return;
    }

    EnetItemsType *items;
    EnetItemType *item;

    items = (static_cast<EnetItemsType *>(xparser.get()));
    std::vector<EnetItemType>::iterator iter;
    for (vector<EnetItemType>::iterator iter =items->item.begin();
         iter != items->item.end(); iter++) {
        item = &*iter;
        if (item->entry.nlri.mac != "") {
            AddEvpnRoute(vrf_name, item->entry.nlri.mac, item);
        } else {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                        "NLRI missing mac address for evpn, failed parsing");
        }
    }
}

static TunnelType::TypeBmap
GetEnetTypeBitmap(const EnetTunnelEncapsulationListType &encap) {
    TunnelType::TypeBmap bmap = 0;
    for (EnetTunnelEncapsulationListType::const_iterator iter = encap.begin();
         iter != encap.end(); iter++) {
        TunnelEncapType::Encap encap =
            TunnelEncapType::TunnelEncapFromString(*iter);
        if ((encap == TunnelEncapType::GRE) ||
            (encap == TunnelEncapType::MPLS_O_GRE))
            bmap |= (1 << TunnelType::MPLS_GRE);
        if (encap == TunnelEncapType::MPLS_O_UDP)
            bmap |= (1 << TunnelType::MPLS_UDP);
        if (encap == TunnelEncapType::VXLAN)
            bmap |= (1 << TunnelType::VXLAN);
        if (encap == TunnelEncapType::NATIVE_CONTRAIL)
            bmap |= (1 << TunnelType::NATIVE);
    }
    return bmap;
}

static TunnelType::TypeBmap
GetTypeBitmap(const TunnelEncapsulationListType &encap) {
    TunnelType::TypeBmap bmap = 0;
    for (TunnelEncapsulationListType::const_iterator iter = encap.begin();
         iter != encap.end(); iter++) {
        TunnelEncapType::Encap encap =
            TunnelEncapType::TunnelEncapFromString(*iter);
        if ((encap == TunnelEncapType::GRE) ||
            (encap == TunnelEncapType::MPLS_O_GRE))
            bmap |= (1 << TunnelType::MPLS_GRE);
        if (encap == TunnelEncapType::MPLS_O_UDP)
            bmap |= (1 << TunnelType::MPLS_UDP);
        if (encap == TunnelEncapType::NATIVE_CONTRAIL)
            bmap |= (1 << TunnelType::NATIVE);
    }
    return bmap;
}
static TunnelType::TypeBmap
GetMcastTypeBitmap(const McastTunnelEncapsulationListType &encap) {
    TunnelType::TypeBmap bmap = 0;
    for (McastTunnelEncapsulationListType::const_iterator iter = encap.begin();
         iter != encap.end(); iter++) {
        TunnelEncapType::Encap encap =
            TunnelEncapType::TunnelEncapFromString(*iter);
        if ((encap == TunnelEncapType::GRE) ||
            (encap == TunnelEncapType::MPLS_O_GRE))
            bmap |= (1 << TunnelType::MPLS_GRE);
        if (encap == TunnelEncapType::MPLS_O_UDP)
            bmap |= (1 << TunnelType::MPLS_UDP);
    }
    return bmap;
}

void AgentXmppChannel::ReceiveMulticastUpdate(XmlPugi *pugi) {

    pugi::xml_node node = pugi->FindNode("items");
    pugi::xml_attribute attr = node.attribute("node");

    char *saveptr;
    strtok_r(const_cast<char *>(attr.value()), "/", &saveptr);
    strtok_r(NULL, "/", &saveptr);
    char *vrf_name =  strtok_r(NULL, "", &saveptr);
    const std::string vrf(vrf_name);
    TunnelOlist olist;

    pugi::xml_node node_check = pugi->FindNode("retract");
    if (!pugi->IsNull(node_check)) {
        pugi->ReadNode("retract"); //sets the context
        std::string retract_id = pugi->ReadAttrib("id");
        if (bgp_peer_id() != agent_->mulitcast_builder()->
                             bgp_peer_id()) {
            CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                       "Ignore retract request from non multicast tree "
                       "builder peer; Multicast Delete Node id:" + retract_id);
            return;
        }

        for (node = node.first_child(); node; node = node.next_sibling()) {
            if (strcmp(node.name(), "retract") == 0) {
                std::string id = node.first_attribute().value();
                CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                            "Multicast Delete Node id:" + id);

                // Parse identifier to obtain group,source
                // <addr:VRF:Group,Source)
                strtok_r(const_cast<char *>(id.c_str()), ":", &saveptr);
                strtok_r(NULL, ":", &saveptr);
                char *group = strtok_r(NULL, ",", &saveptr);
                char *source = strtok_r(NULL, "", &saveptr);
                if (group == NULL || source == NULL) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                       "Error parsing multicast group address from retract id");
                    return;
                }

                boost::system::error_code ec;
                IpAddress g_addr =
                    IpAddress::from_string(group, ec);
                if (ec.value() != 0) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                            "Error parsing multicast group address");
                    return;
                }

                IpAddress s_addr =
                    IpAddress::from_string(source, ec);
                if (ec.value() != 0) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                            "Error parsing multicast source address");
                    return;
                }

                //Retract with invalid identifier
                agent_->oper_db()->multicast()->
                    ModifyFabricMembers(agent_->multicast_tree_builder_peer(),
                                        vrf, g_addr.to_v4(),
                                        s_addr.to_v4(), 0, olist,
                                        ControllerPeerPath::kInvalidPeerIdentifier);
            }
        }
        return;
    }

    pugi::xml_node items_node = pugi->FindNode("item");
    if (!pugi->IsNull(items_node)) {
        pugi->ReadNode("item"); //sets the context
        std::string item_id = pugi->ReadAttrib("id");
        if (!(agent_->mulitcast_builder()) || (bgp_peer_id() !=
            agent_->mulitcast_builder()->bgp_peer_id())) {
            CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "Ignore request from non multicast tree "
                             "builder peer; Multicast Delete Node:" + item_id);
            return;
        }
    }

    //Call Auto-generated Code to return struct
    auto_ptr<AutogenProperty> xparser(new AutogenProperty());
    if (McastItemsType::XmlParseProperty(node, &xparser) == false) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                        "Xml Parsing for Multicast Message Failed");
        return;
    }

    McastItemsType *items;
    McastItemType *item;

    items = (static_cast<McastItemsType *>(xparser.get()));
    std::vector<McastItemType>::iterator items_iter;
    boost::system::error_code ec;
    for (items_iter = items->item.begin(); items_iter != items->item.end(); 
            items_iter++) {

        item = &*items_iter;

        IpAddress g_addr =
            IpAddress::from_string(item->entry.nlri.group, ec);
        if (ec.value() != 0) {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "Error parsing multicast group address");
            return;
        }

        IpAddress s_addr =
            IpAddress::from_string(item->entry.nlri.source, ec);
        if (ec.value() != 0) {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                            "Error parsing multicast source address");
            return;
        }

        std::vector<McastNextHopType>::iterator iter;
        for (iter = item->entry.olist.next_hop.begin();
                iter != item->entry.olist.next_hop.end(); iter++) {

            McastNextHopType nh = *iter;
            IpAddress addr = IpAddress::from_string(nh.address, ec);
            if (ec.value() != 0) {
                CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                 "Error parsing next-hop address");
                return;
            }

            int label;
            stringstream nh_label(nh.label);
            nh_label >> label;
            TunnelType::TypeBmap encap =
                GetMcastTypeBitmap(nh.tunnel_encapsulation_list);
            olist.push_back(OlistTunnelEntry(nil_uuid(), label,
                                             addr.to_v4(), encap));
        }

        agent_->oper_db()->multicast()->ModifyFabricMembers(
                agent_->multicast_tree_builder_peer(),
                vrf, g_addr.to_v4(), s_addr.to_v4(),
                item->entry.nlri.source_label, olist,
                agent_->controller()->multicast_sequence_number());
    }
}

void AgentXmppChannel::ReceiveV4V6Update(XmlPugi *pugi) {

    pugi::xml_node node = pugi->FindNode("items");
    pugi::xml_attribute attr = node.attribute("node");

    const char *af = NULL;
    char *saveptr;
    af = strtok_r(const_cast<char *>(attr.value()), "/", &saveptr);
    strtok_r(NULL, "/", &saveptr);
    char *vrf_name =  strtok_r(NULL, "", &saveptr);

    VrfKey vrf_key(vrf_name);
    VrfEntry *vrf = 
        static_cast<VrfEntry *>(agent_->vrf_table()->
                                FindActiveEntry(&vrf_key));
    if (!vrf) {
        CONTROLLER_INFO_TRACE (Trace, GetBgpPeerName(), vrf_name,
                                     "VRF not found");
        return;
    }

    InetUnicastAgentRouteTable *rt_table = NULL;
    if (atoi(af) == BgpAf::IPv4) {
        rt_table = vrf->GetInet4UnicastRouteTable();
    } else if (atoi(af) == BgpAf::IPv6) {
        rt_table = vrf->GetInet6UnicastRouteTable();
    }

    if (!rt_table) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name, 
                                    "VRF not found");
        return;
    }

    if (!pugi->IsNull(node)) {
  
        pugi::xml_node node_check = pugi->FindNode("retract");
        if (!pugi->IsNull(node_check)) {
            for (node = node.first_child(); node; node = node.next_sibling()) {
                if (strcmp(node.name(), "retract") == 0)  {
                    std::string id = node.first_attribute().value();
                    CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                                "Delete Node id:" + id);

                    boost::system::error_code ec;
                    int prefix_len;
                    if (atoi(af) == BgpAf::IPv4) {
                        Ip4Address prefix_addr;
                        ec = Ip4PrefixParse(id, &prefix_addr, &prefix_len);
                        if (ec.value() != 0) {
                            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                    "Error parsing v4 prefix for delete");
                            return;
                        }

                        rt_table->DeleteReq(bgp_peer_id(), vrf_name,
                                            prefix_addr, prefix_len,
                                            new ControllerVmRoute(bgp_peer_id()));

                    } else if (atoi(af) == BgpAf::IPv6) {
                        Ip6Address prefix_addr;
                        ec = Inet6PrefixParse(id, &prefix_addr, &prefix_len);
                        if (ec.value() != 0) {
                            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                    "Error parsing v6 prefix for delete");
                            return;
                        }
                        rt_table->DeleteReq(bgp_peer_id(), vrf_name,
                                            prefix_addr, prefix_len,
                                            new ControllerVmRoute(bgp_peer_id()));
                    }
                }
            }
            return;
        }
           
        //Call Auto-generated Code to return struct
        auto_ptr<AutogenProperty> xparser(new AutogenProperty());
        if (ItemsType::XmlParseProperty(node, &xparser) == false) {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "Xml Parsing Failed");
            return;
        }
        ItemsType *items;
        ItemType *item;

        items = (static_cast<ItemsType *>(xparser.get()));
        for (vector<ItemType>::iterator iter =items->item.begin();
                                        iter != items->item.end();
                                        ++iter) {
            item = &*iter;
            boost::system::error_code ec;
            int prefix_len;

            if (atoi(af) == BgpAf::IPv4) {
                Ip4Address prefix_addr;
                ec = Ip4PrefixParse(item->entry.nlri.address, &prefix_addr,
                                    &prefix_len);
                if (ec.value() != 0) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                            "Error parsing v4 route address");
                    return;
                }
                AddRoute(vrf_name, prefix_addr, prefix_len, item);
            } else if (atoi(af) == BgpAf::IPv6) {
                Ip6Address prefix_addr;
                ec = Inet6PrefixParse(item->entry.nlri.address, &prefix_addr,
                                      &prefix_len);
                if (ec.value() != 0) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                            "Error parsing v6 route address");
                    return;
                }
                AddRoute(vrf_name, prefix_addr, prefix_len, item);
            } else {
                CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                 "Error updating route, Unknown IP family");
            }
        }
    }
}

static void GetEcmpHashFieldsToUse(ItemType *item,
                                   EcmpLoadBalance &ecmp_load_balance) {
    ecmp_load_balance.ResetAll();
    if (item->entry.load_balance.load_balance_decision.empty() ||
        item->entry.load_balance.load_balance_decision !=
            LoadBalanceDecision)
        ecmp_load_balance.SetAll();

    uint8_t field_list_size =  item->entry.
        load_balance.load_balance_fields.load_balance_field_list.size();
    if (field_list_size == 0)
        ecmp_load_balance.SetAll();

    for (uint32_t i = 0; i < field_list_size; i++) {
        std::string field_type = item->entry.
            load_balance.load_balance_fields.load_balance_field_list[i];
        if (field_type == ecmp_load_balance.source_ip_str())
            ecmp_load_balance.set_source_ip();
        if (field_type == ecmp_load_balance.destination_ip_str())
            ecmp_load_balance.set_destination_ip();
        if (field_type == ecmp_load_balance.ip_protocol_str())
            ecmp_load_balance.set_ip_protocol();
        if (field_type == ecmp_load_balance.source_port_str())
            ecmp_load_balance.set_source_port();
        if (field_type == ecmp_load_balance.destination_port_str())
            ecmp_load_balance.set_destination_port();
    }
}

void AgentXmppChannel::AddEcmpRoute(string vrf_name, IpAddress prefix_addr,
                                    uint32_t prefix_len, ItemType *item,
                                    const VnListType &vn_list) {
    //Extract the load balancer fields.
    EcmpLoadBalance ecmp_load_balance;
    GetEcmpHashFieldsToUse(item, ecmp_load_balance);

    // use LOW PathPreference if local preference attribute is not set
    uint32_t preference = PathPreference::LOW;
    TunnelType::TypeBmap encap = TunnelType::MplsType(); //default
    if (item->entry.local_preference != 0) {
        preference = item->entry.local_preference;
    }
    PathPreference rp(item->entry.sequence_number, preference, false, false);
    InetUnicastAgentRouteTable *rt_table = PrefixToRouteTable(vrf_name,
                                                              prefix_addr);
    if (rt_table == NULL) {
        return;
    }

    TagList tag_list;
    BuildTagList(item, &tag_list);

    ComponentNHKeyList comp_nh_list;
    bool comp_nh_policy = false;
    for (uint32_t i = 0; i < item->entry.next_hops.next_hop.size(); i++) {
        std::string nexthop_addr =
            item->entry.next_hops.next_hop[i].address;
        boost::system::error_code ec;
        IpAddress addr = IpAddress::from_string(nexthop_addr, ec);
        if (ec.value() != 0) {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "Error parsing nexthop ip address");
            continue;
        }
        if (!addr.is_v4()) {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "Non IPv4 address not supported as nexthop");
            continue;
        }

        if (comp_nh_list.size() >= maximum_ecmp_paths) {
            std::stringstream msg;
            msg << "Nexthop paths for prefix "
                << prefix_addr.to_string() << "/" << prefix_len
                << " (" << item->entry.next_hops.next_hop.size()
                << ") exceed the maximum supported, ignoring them";
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name, msg.str());
            break;
        }

        uint32_t label = item->entry.next_hops.next_hop[i].label;
        if (agent_->router_id() == addr.to_v4()) {
            //Get local list of interface and append to the list
            MplsLabel *mpls =
                agent_->mpls_table()->FindMplsLabel(label);
            if (mpls != NULL) {
                if (mpls->nexthop()->GetType() == NextHop::VRF) {
                    BgpPeer *bgp_peer = bgp_peer_id();
                    ClonedLocalPath *data =
                        new ClonedLocalPath(label, vn_list,
                                item->entry.security_group_list.security_group,
                                tag_list, sequence_number());
                    rt_table->AddClonedLocalPathReq(bgp_peer, vrf_name,
                                                    prefix_addr, prefix_len,
                                                    data);
                    return;
                }

                const NextHop *mpls_nh = mpls->nexthop();
                DBEntryBase::KeyPtr key = mpls_nh->GetDBRequestKey();
                NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
                if (nh_key->GetType() != NextHop::COMPOSITE) {
                    //By default all component members of composite NH
                    //will be policy disabled, except for component NH
                    //of type composite
                    nh_key->SetPolicy(false);
                }
                std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
                ComponentNHKeyPtr component_nh_key(new ComponentNHKey(label,
                                                   nh_key_ptr));
                comp_nh_list.push_back(component_nh_key);
                if (!comp_nh_policy) {
                    comp_nh_policy = mpls_nh->NexthopToInterfacePolicy();
                }
            }
        } else {
            encap = GetTypeBitmap
                (item->entry.next_hops.next_hop[i].tunnel_encapsulation_list);
            TunnelNHKey *nh_key = new TunnelNHKey(agent_->fabric_vrf_name(),
                                                  agent_->router_id(),
                                                  addr.to_v4(), false,
                                                  TunnelType::ComputeType(encap));
            std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
            ComponentNHKeyPtr component_nh_key(new ComponentNHKey(label,
                                                                  nh_key_ptr));
            comp_nh_list.push_back(component_nh_key);
        }
    }

    // Build the NH request and then create route data to be passed
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::ECMP, comp_nh_policy,
                                        comp_nh_list, vrf_name));
    nh_req.data.reset(new CompositeNHData());
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(bgp_peer_id(), prefix_addr, prefix_len,
                                vn_list, -1, false, vrf_name,
                                item->entry.security_group_list.security_group,
                                tag_list, rp, encap, ecmp_load_balance, nh_req);

    //ECMP create component NH
    rt_table->AddRemoteVmRouteReq(bgp_peer_id(), vrf_name,
                                  prefix_addr, prefix_len, data);
}

static bool FillEvpnOlist(EnetOlistType &olist, TunnelOlist *tunnel_olist) {
    for (uint32_t i = 0; i < olist.next_hop.size(); i++) {
        boost::system::error_code ec;
        IpAddress addr =
            IpAddress::from_string(olist.next_hop[i].address,
                                   ec);
        if (ec.value() != 0) {
            return false;
        }

        int label = olist.next_hop[i].label;
        TunnelType::TypeBmap encap =
            GetEnetTypeBitmap(olist.next_hop[i].tunnel_encapsulation_list);
        tunnel_olist->push_back(OlistTunnelEntry(nil_uuid(), label,
                                                 addr.to_v4(), encap));
    }
    return true;
}

void AgentXmppChannel::AddMulticastEvpnRoute(const string &vrf_name,
                                             const MacAddress &mac,
                                             EnetItemType *item) {
    //Traverse Leaf Olist
    TunnelOlist leaf_olist;
    TunnelOlist olist;
    //Fill leaf olist and olist
    //TODO can check for item->entry.assisted_replication_supported
    //and then populate leaf_olist
    CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), "Composite",
                     "add leaf evpn multicast route");
    if (FillEvpnOlist(item->entry.leaf_olist, &leaf_olist) == false) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Error parsing next-hop address");
        return;
    }
    CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), "Composite",
                     "add evpn multicast route");
    if (FillEvpnOlist(item->entry.olist, &olist) == false) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Error parsing next-hop address");
        return;
    }

    agent_->oper_db()->multicast()->
        ModifyTorMembers(bgp_peer_id(), vrf_name, leaf_olist,
                         item->entry.nlri.ethernet_tag,
                         agent_->controller()->
                         multicast_sequence_number());

    agent_->oper_db()->multicast()->
        ModifyEvpnMembers(bgp_peer_id(), vrf_name, olist,
                          item->entry.nlri.ethernet_tag,
                          agent_->controller()->
                          multicast_sequence_number());
}

void AgentXmppChannel::AddEvpnRoute(const std::string &vrf_name,
                                    std::string mac_str,
                                    EnetItemType *item) {
    // Validate VRF first
    EvpnAgentRouteTable *rt_table =
        static_cast<EvpnAgentRouteTable *>
        (agent_->vrf_table()->GetEvpnRouteTable(vrf_name));
    if (rt_table == NULL) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Invalid VRF. Ignoring route");
        return;
    }

    boost::system::error_code ec;
    MacAddress mac(mac_str);
    if (mac == MacAddress::BroadcastMac()) {
        AddMulticastEvpnRoute(vrf_name, mac, item);
        return;
    }

    int n = -1;
    IpAddress nh_ip;
    // if list contains more than one nexthop, pick the lowest IP for
    // active nexthop of ecmp
    for (uint32_t i = 0; i < item->entry.next_hops.next_hop.size(); i++) {
        string nexthop_addr = item->entry.next_hops.next_hop[i].address;
        IpAddress temp_nh_ip = IpAddress::from_string(nexthop_addr, ec);
        if (ec.value() != 0) {
            continue;
        }

        if (n == -1 || temp_nh_ip < nh_ip) {
            n = i;
            nh_ip = temp_nh_ip;
        }
    }

    if (n == -1) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Error parsing nexthop ip address");
        return;
    }

    uint32_t label = item->entry.next_hops.next_hop[n].label;
    TunnelType::TypeBmap encap = GetEnetTypeBitmap
        (item->entry.next_hops.next_hop[n].tunnel_encapsulation_list);
    // use LOW PathPreference if local preference attribute is not set
    uint32_t preference = PathPreference::LOW;
    if (item->entry.local_preference != 0) {
        preference = item->entry.local_preference;
    }
    PathPreference path_preference(item->entry.sequence_number, preference,
                                   false, false);

    TagList tag_list;
    BuildTagList(item, &tag_list);

    IpAddress ip_addr;
    if (ParseAddress(item->entry.nlri.address, &ip_addr) < 0) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Error parsing address : " + item->entry.nlri.address);
        return;
    }

    string nexthop_addr = item->entry.next_hops.next_hop[n].address;
    CONTROLLER_INFO_TRACE(RouteImport, GetBgpPeerName(), vrf_name,
                     mac.ToString(), 0, nexthop_addr, label, "");

    if (agent_->router_id() != nh_ip.to_v4()) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), nexthop_addr,
                                    "add remote evpn route");
        VnListType vn_list;
        vn_list.insert(item->entry.virtual_network);
        // for number of nexthops more than 1, carry flag ecmp suppressed
        // to indicate the same to all modules, till we handle L2 ecmp
        ControllerVmRoute *data =
            ControllerVmRoute::MakeControllerVmRoute(bgp_peer_id(),
                                                     agent_->fabric_vrf_name(),
                                                     agent_->router_id(),
                                                     vrf_name, nh_ip.to_v4(),
                                                     encap, label,
                                                     vn_list,
                                                     item->entry.security_group_list.security_group,
                                                     tag_list,
                                                     path_preference,
                                                     (item->entry.next_hops.next_hop.size() > 1),
                                                     EcmpLoadBalance(),
                                                     item->entry.etree_leaf);
        rt_table->AddRemoteVmRouteReq(bgp_peer_id(), vrf_name, mac, ip_addr,
                                      item->entry.nlri.ethernet_tag, data);
        return;
    }

    // Route originated by us and reflected back by control-node
    // When encap is MPLS based, the nexthop can be found by label lookup
    // When encapsulation used is VXLAN, nexthop cannot be found from message
    // To have common design, get nexthop from the route already present
    VrfEntry *vrf =
        agent_->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                    "vrf not found, ignoring request");
        return;
    }

    EvpnRouteKey key(agent_->local_vm_peer(), vrf_name, mac,
                       ip_addr, item->entry.nlri.ethernet_tag);
    EvpnRouteEntry *route = static_cast<EvpnRouteEntry *>
        (rt_table->FindActiveEntry(&key));
    if (route == NULL) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "route not found, ignoring request");
        return;
    }

    AgentPath *local_path = route->FindLocalVmPortPath();
    const NextHop *nh = local_path ? local_path->nexthop() : NULL;
    if (nh == NULL) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                    "nexthop not found, ignoring request");
        return;
    }

    //In EVPN, if interface IP is not same as IP received in Evpn route
    //then use receive NH. This is done because this received evpn ip is
    //a floating IP associated with VM and it shoul be routed.
    if (nh->GetType() == NextHop::L2_RECEIVE) {
        rt_table->AddControllerReceiveRouteReq(bgp_peer_id(), vrf_name,
                                         label, mac, ip_addr,
                                         item->entry.nlri.ethernet_tag,
                                         item->entry.virtual_network,
                                         path_preference, sequence_number());
        return;
    }

    // We expect only INTERFACE nexthop for evpn routes
    const InterfaceNH *intf_nh = dynamic_cast<const InterfaceNH *>(nh);
    if (nh->GetType() != NextHop::INTERFACE) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Invalid nexthop in evpn route");
        return;
    }

    SecurityGroupList sg_list = item->entry.security_group_list.security_group;
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_nh->GetIfUuid(),
                            intf_nh->GetInterface()->name());
    LocalVmRoute *local_vm_route = NULL;
    VnListType vn_list;
    vn_list.insert(item->entry.virtual_network);
    EcmpLoadBalance ecmp_load_balance;

    if (encap == TunnelType::VxlanType()) {
        local_vm_route =
            new LocalVmRoute(intf_key,
                             MplsTable::kInvalidLabel,
                             label, false, vn_list,
                             InterfaceNHFlags::BRIDGE,
                             sg_list, tag_list, CommunityList(), path_preference,
                             Ip4Address(0), ecmp_load_balance, false, false,
                             sequence_number(), item->entry.etree_leaf);
    } else {
        local_vm_route =
            new LocalVmRoute(intf_key,
                             label,
                             VxLanTable::kInvalidvxlan_id,
                             false, vn_list,
                             InterfaceNHFlags::BRIDGE,
                             sg_list, tag_list, CommunityList(), path_preference,
                             Ip4Address(0), ecmp_load_balance, false, false,
                             sequence_number(), item->entry.etree_leaf);
    }
    rt_table->AddLocalVmRouteReq(bgp_peer_id(), vrf_name, mac,
                                 ip_addr, item->entry.nlri.ethernet_tag,
                                 static_cast<LocalVmRoute *>(local_vm_route));
}

void AgentXmppChannel::AddRemoteRoute(string vrf_name, IpAddress prefix_addr,
                                      uint32_t prefix_len, ItemType *item,
                                      const VnListType &vn_list) {
    InetUnicastAgentRouteTable *rt_table = PrefixToRouteTable(vrf_name,
                                                              prefix_addr);

    if (rt_table == NULL) {
        return;
    }

    boost::system::error_code ec;
    string nexthop_addr = item->entry.next_hops.next_hop[0].address;
    uint32_t label = item->entry.next_hops.next_hop[0].label;
    IpAddress addr = IpAddress::from_string(nexthop_addr, ec);
    TunnelType::TypeBmap encap = GetTypeBitmap
        (item->entry.next_hops.next_hop[0].tunnel_encapsulation_list);

    if (ec.value() != 0) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Error parsing nexthop ip address");
        return;
    }

    // use LOW PathPreference if local preference attribute is not set
    uint32_t preference = PathPreference::LOW;
    if (item->entry.local_preference != 0) {
        preference = item->entry.local_preference;
    }
    PathPreference path_preference(item->entry.sequence_number, preference,
                                   false, false);

    TagList tag_list;
    BuildTagList(item, &tag_list);

    std::string vn_string;
    for (VnListType::const_iterator vnit = vn_list.begin();
         vnit != vn_list.end(); ++vnit) {
        vn_string += *vnit + " ";
    }
    CONTROLLER_INFO_TRACE(RouteImport, GetBgpPeerName(), vrf_name,
                     prefix_addr.to_string(), prefix_len,
                     addr.to_v4().to_string(), label, vn_string);

    if (agent_->router_id() != addr.to_v4()) {
        EcmpLoadBalance ecmp_load_balance;
        GetEcmpHashFieldsToUse(item, ecmp_load_balance);
        ControllerVmRoute *data =
            ControllerVmRoute::MakeControllerVmRoute(bgp_peer_id(),
                               agent_->fabric_vrf_name(), agent_->router_id(),
                               vrf_name, addr.to_v4(), encap, label, vn_list,
                               item->entry.security_group_list.security_group,
                               tag_list,
                               path_preference, false, ecmp_load_balance,
                               false);
        rt_table->AddRemoteVmRouteReq(bgp_peer_id(), vrf_name, prefix_addr,
                                      prefix_len, data);
        return;
    }

    MplsLabel *mpls = agent_->mpls_table()->FindMplsLabel(label);
    if (mpls != NULL) {
        const NextHop *nh = mpls->nexthop();
        switch(nh->GetType()) {
        case NextHop::INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            const Interface *interface = intf_nh->GetInterface();
            if (interface == NULL) {
                break;
            }

            VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE,
                                    intf_nh->GetIfUuid(), interface->name());
            EcmpLoadBalance ecmp_load_balance;
            GetEcmpHashFieldsToUse(item, ecmp_load_balance);
            BgpPeer *bgp_peer = bgp_peer_id();
            if (interface->type() == Interface::VM_INTERFACE) {
                LocalVmRoute *local_vm_route =
                    new LocalVmRoute(intf_key, label,
                             VxLanTable::kInvalidvxlan_id,
                             false, vn_list,
                             InterfaceNHFlags::INET4,
                             item->entry.security_group_list.security_group,
                             tag_list,
                             CommunityList(),
                             path_preference,
                             Ip4Address(0),
                             ecmp_load_balance, false, false,
                             sequence_number(), false);
                rt_table->AddLocalVmRouteReq(bgp_peer, vrf_name,
                                             prefix_addr, prefix_len,
                                             static_cast<LocalVmRoute *>(local_vm_route));
            } else if (interface->type() == Interface::INET) {

                if (!prefix_addr.is_v4()) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                    "MPLS label inet interface type not supported for non IPv4");
                    return;
                }
                InetInterfaceKey intf_key(interface->name());
                InetInterfaceRoute *inet_interface_route =
                    new InetInterfaceRoute(intf_key, label,
                                           TunnelType::MplsType(),
                                           vn_list, sequence_number());

                rt_table->AddInetInterfaceRouteReq(bgp_peer, vrf_name,
                                                prefix_addr.to_v4(), prefix_len,
                                                inet_interface_route);
            } else {
                // Unsupported scenario
                CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                 "MPLS label points to invalid interface type");
                 break;
            }

            break;
            }

        case NextHop::VLAN: {
            const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
            VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE,
                                    vlan_nh->GetIfUuid(), "");
            BgpPeer *bgp_peer = bgp_peer_id();
            VlanNhRoute *data =
                new VlanNhRoute(intf_key, vlan_nh->GetVlanTag(),
                                label, vn_list,
                                item->entry.security_group_list.security_group,
                                tag_list,
                                path_preference, sequence_number());
            rt_table->AddVlanNHRouteReq(bgp_peer, vrf_name, prefix_addr,
                                        prefix_len, data);
            break;
            }
        case NextHop::COMPOSITE: {
            AddEcmpRoute(vrf_name, prefix_addr, prefix_len, item, vn_list);
            break;
            }
        case NextHop::VRF: {
            //In case of gateway interface with example subnet
            //1.1.1.0/24 may be reachable on this gateway inteface,
            //Path added by local vm peer would point to
            //resolve NH, so that if any path hits this route, ARP resolution
            //can begin, and the label exported for this route would point to
            //table nexthop.
            //Hence existing logic of picking up nexthop from mpls label to
            //nexthop, will not work. We have added a special path where we
            //pick nexthop from local vm path, instead of BGP
            BgpPeer *bgp_peer = bgp_peer_id();
            ClonedLocalPath *data =
                new ClonedLocalPath(label, vn_list,
                        item->entry.security_group_list.security_group,
                        tag_list,
                        sequence_number());
            rt_table->AddClonedLocalPathReq(bgp_peer, vrf_name,
                                            prefix_addr.to_v4(),
                                            prefix_len, data);
            break;
        }

        default:
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "MPLS label points to invalid NH");
        }
    }
}

bool AgentXmppChannel::IsEcmp(const std::vector<autogen::NextHopType> &nexthops) {
    if (nexthops.size() == 0)
        return false;

    std::string address = nexthops[0].address;
    for (uint32_t index = 1; index < nexthops.size(); index++) {
        if (nexthops[index].address != address)
            return true;
    }

    return false;
}

void AgentXmppChannel::GetVnList(const std::vector<autogen::NextHopType> &nexthops,
                                 VnListType *vn_list) {
    for (uint32_t index = 0; index < nexthops.size(); index++) {
        vn_list->insert(nexthops[index].virtual_network);
    }
}

void AgentXmppChannel::AddRoute(string vrf_name, IpAddress prefix_addr,
                                uint32_t prefix_len, ItemType *item) {
    if (item->entry.next_hops.next_hop[0].label ==
            MplsTable::kInvalidExportLabel) {
        return;
    }

    VnListType vn_list;
    GetVnList(item->entry.next_hops.next_hop, &vn_list);
    if (IsEcmp(item->entry.next_hops.next_hop)) {
        AddEcmpRoute(vrf_name, prefix_addr, prefix_len, item, vn_list);
    } else {
        AddRemoteRoute(vrf_name, prefix_addr, prefix_len, item, vn_list);
    }
}

void AgentXmppChannel::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
    if (msg && msg->type == XmppStanza::MESSAGE_STANZA) {
        auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
        XmlPugi *msg_pugi = reinterpret_cast<XmlPugi *>(msg->dom.get());
        pugi->LoadXmlDoc(msg_pugi->doc());
        boost::shared_ptr<ControllerXmppData> data(new ControllerXmppData(xmps::BGP,
                                                                          xmps::UNKNOWN,
                                                                          xs_idx_,
                                                                          impl,
                                                                          true));
        agent_->controller()->Enqueue(data);
    }
}

void AgentXmppChannel::ReceiveBgpMessage(std::auto_ptr<XmlBase> impl) {
    if (agent_->stats())
        agent_->stats()->incr_xmpp_in_msgs(xs_idx_);

    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
    pugi::xml_node node = pugi->FindNode("items");
    if (node == 0) {
        end_of_rib_rx_timer()->Cancel();
        EndOfRibRx();
        return;
    }

    pugi->ReadNode("items"); //sets the context
    std::string nodename = pugi->ReadAttrib("node");

    const char *af = NULL, *safi = NULL, *vrf_name;
    char *str = const_cast<char *>(nodename.c_str());
    char *saveptr;
    af = strtok_r(str, "/", &saveptr);
    safi = strtok_r(NULL, "/", &saveptr);
    vrf_name = saveptr;

    // No BGP peer
    if (bgp_peer_id() == NULL) {
        CONTROLLER_TRACE (Trace, GetBgpPeerName(), vrf_name,
                          "BGP peer not present, agentxmppchannel is inactive");
        return;
    }

    // If EndOfRib marker is received, process it accordingly.
    if (nodename == XmppInit::kEndOfRibMarker) {
        return;
    }

    if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Mcast) {
        ReceiveMulticastUpdate(pugi);
        return;
    }
    if (atoi(af) == BgpAf::L2Vpn && atoi(safi) == BgpAf::Enet) {
        ReceiveEvpnUpdate(pugi);
        return;
    }
    if (atoi(safi) == BgpAf::Unicast) {
        ReceiveV4V6Update(pugi);
        return;
    }
    CONTROLLER_TRACE (Trace, GetBgpPeerName(), vrf_name,
                      "Error Route update, Unknown Address Family or safi");
}

void AgentXmppChannel::ReceiveInternal(const XmppStanza::XmppMessage *msg) {
    ReceiveUpdate(msg);
}

std::string AgentXmppChannel::ToString() const {
    return channel_str_;
}

void AgentXmppChannel::WriteReadyCb(const boost::system::error_code &ec) {
}

void AgentXmppChannel::CleanConfigStale(AgentXmppChannel *agent_xmpp_channel) {
    assert(agent_xmpp_channel);
    const Agent *agent = agent_xmpp_channel->agent();

    //Start a timer to flush off all old configs
    if (agent->ifmap_xmpp_channel(agent_xmpp_channel->GetXmppServerIdx())) {
        agent->ifmap_xmpp_channel(agent_xmpp_channel->GetXmppServerIdx())->
            StartConfigCleanupTimer();
    }
}

bool AgentXmppChannel::IsXmppChannelActive(const Agent *agent,
                                           AgentXmppChannel *peer) {
    bool xmpp_channel_found = false;
    //Verify if channel registered is stiil active or has been deleted
    //after bgp peer was down. This is checked under existing agent
    //xmpp channels in agent.
    for (uint8_t idx = 0; idx < MAX_XMPP_SERVERS; idx++) {
        if (agent->controller_xmpp_channel(idx) == peer) {
            xmpp_channel_found = true;
            break;
        }
    }
    return xmpp_channel_found;
}

/*
 * AgentXmppChannel is active when:
 * 1) bgp peer is not null(bgp_peer_id)
 * 2) xmpp channel is in READY state
 * 3) Valid XMPP channel
 */
bool AgentXmppChannel::IsBgpPeerActive(const Agent *agent,
                                       AgentXmppChannel *peer) {
    if (!IsXmppChannelActive(agent, peer))
        return false;

    //Reach here if channel is present. Now check for BGP peer
    //as channel may have come up and created another BGP peer.
    //Also check for the state of channel.
    if (peer && peer->GetXmppChannel() && peer->bgp_peer_id() &&
        (peer->GetXmppChannel()->GetPeerState() == xmps::READY)) {
        return true;
    }
    return false;
}

/*
 * New peer is config peer. Increment the global config sequence number,
 * Notify for new config peer, set it in agent xmppcfg
 */
bool AgentXmppChannel::SetConfigPeer(AgentXmppChannel *peer) {
    Agent *agent = peer->agent();
    if (AgentXmppChannel::ControllerSendCfgSubscribe(peer)) {
        agent->set_ifmap_active_xmpp_server(peer->GetXmppServer(),
                                peer->GetXmppServerIdx());
        //Generate a new sequence number for the configuration
        AgentIfMapXmppChannel::NewSeqNumber();
        agent->ifmap_parser()->reset_statistics();
        agent->controller()->agent_ifmap_vm_export()->NotifyAll(peer);
        if (agent->ifmap_xmpp_channel(peer->GetXmppServerIdx())) {
            agent->ifmap_xmpp_channel(peer->GetXmppServerIdx())->
                end_of_config_timer()->Start(peer);
        }
        return true;
    }
    return false;
}

/*
 * New multicast peer found - either first one or a lower one got selected.
 * Increment peer identifier so that new peer can update using incremented seq
 * number and multicast entries which didnt get updated can be removed via stale
 * cleanup.
 */
void AgentXmppChannel::SetMulticastPeer(AgentXmppChannel *old_peer,
                                        AgentXmppChannel *new_peer) {
    old_peer->agent()->controller()->increment_multicast_sequence_number();
    old_peer->agent()->set_cn_mcast_builder(new_peer);
}

void AgentXmppChannel::XmppClientChannelEvent(AgentXmppChannel *peer,
                                              xmps::PeerState state) {
    std::auto_ptr<XmlBase> dummy_dom;
    boost::shared_ptr<ControllerXmppData> data(new ControllerXmppData(xmps::BGP,
                                                   state,
                                                   peer->GetXmppServerIdx(),
                                                   dummy_dom,
                                                   false));
    peer->agent()->controller()->Enqueue(data);
}

uint64_t AgentXmppChannel::sequence_number() const {
    return (static_cast<BgpPeer *>(bgp_peer_id_.get()))->sequence_number();
}

/*
 * AgentXmppChanel state - READY
 *
 * - Bump up the sequence number to identify updates of routes on this channel
 *   after connection is in ready state and later at the end of EOR TX timer,
 *   flush out all the stales(which never got updated after READY state is seen).
 * - Config server selection is done if no config server is present. This can
 *   happen when this is the first active channel or is becoming active after
 *   other channel has become inactive.
 * - Multicast builder - Same explanation as of config server. If there is no
 *   mcast builder, take the ownership.
 * - Notify all routes only if this channel is not becoming config peer. Reason
 *   for same is that config peer selection, runs end of config timer which in
 *   turn runs route walker to notify. So notification is deferred till end of
 *   config is computed. For more explanation on timer check controller_timer.cc
 * - End of Rib Rx timer is started to handle the fallback when EOR from control
 *   node is not seen within fallback time.
 */
void AgentXmppChannel::Ready() {
    agent_->set_controller_xmpp_channel_setup_time(UTCTimestampUsec(), xs_idx_);
    CONTROLLER_TRACE(Session, GetXmppServer(), "READY",
                     "NULL", "BGP peer ready.");
    //Increment sequence number, all new updates should use this sequence
    //number.
    bgp_peer_id()->incr_sequence_number();

    // Switch-over Config Control-node
    if (agent_->ifmap_active_xmpp_server().empty()) {
        AgentXmppChannel::SetConfigPeer(this);
        CONTROLLER_TRACE(Session, GetXmppServer(), "READY",
                         "NULL", "BGP peer set as config server.");
    } else {
        //Notify all routes to channel, if channel is not selected as config.
        //Config selection results in end of config computation which internally
        //will start the route notification. So notify is delayed till end of
        //config is computed.
        StartEndOfRibTxWalker();
    }

    AgentXmppChannel *agent_mcast_builder = agent_->mulitcast_builder();
    //Mcast builder was not set it, use the new peer
    if (agent_mcast_builder == NULL) {
        //Since this is first time mcast peer so old and new peer are same
        AgentXmppChannel::SetMulticastPeer(this, this);
        CONTROLLER_TRACE(Session, GetXmppServer(), "READY",
                         agent_->mulitcast_builder()->
                         GetBgpPeerName(), "Peer elected Mcast builder");
    }

    //Timer to delete stale paths in case EOR is not seen fro CN.
    //If EOR is seen it will cancel this timer.
    end_of_rib_rx_timer()->Start(this);

    if (agent_->stats())
        agent_->stats()->incr_xmpp_reconnects(GetXmppServerIdx());
}

/*
 * AgentXmppChanel state - NotREADY
 *
 * - Firstly stop all timers and walkers. No stale cleanup is to be
 *   done. Caveat: if channel is not config channel then it should not stop any
 *   config timers/walker.
 *   By default end of rib rx timer is stopped and delete of stale walker is
 *   stopped.
 *   If channel is config server then stop end of config and config
 *   cleanup(PeerIsNotConfig).
 *   If channel is config server and there is another active config server
 *   select the other channel as config and stop any timer from this channel.
 * - If its a mcast builder, try finding other if any.
 */
void AgentXmppChannel::NotReady() {
    CONTROLLER_TRACE(Session, GetXmppServer(), "NOT_READY",
                     "NULL", "BGP peer decommissioned for xmpp channel.");
    //Stop stale cleanup if its running
    bgp_peer_id()->StopDeleteStale();
    //Also stop notify as there is no CN for this peer.
    StopEndOfRibTxWalker();
    //Also stop end-of-rib rx fallback and retain.
    end_of_rib_rx_timer()->Cancel();

    // evaluate peer change for config and multicast
    AgentXmppChannel *agent_mcast_builder =
        agent_->mulitcast_builder();
    bool peer_is_config_server = (agent_->
                 ifmap_active_xmpp_server().compare(GetXmppServer()) == 0);
    bool peer_is_agent_mcast_builder = (agent_mcast_builder == this);

    // Switch-over Config Control-node
    if (peer_is_config_server) {
        //stop all config clean timer, retain old config,
        //if there is a new config selected, it will take care of flushing
        //stale.
        PeerIsNotConfig();
        //send cfg subscribe to other peer if exists
        uint8_t idx = ((agent_->ifmap_active_xmpp_server_index() == 0) ? 1: 0);
        agent_->reset_ifmap_active_xmpp_server();
        AgentXmppChannel *new_cfg_peer = agent_->controller_xmpp_channel(idx);

        if (AgentXmppChannel::IsBgpPeerActive(agent_, new_cfg_peer) &&
            AgentXmppChannel::SetConfigPeer(new_cfg_peer)) {
            CONTROLLER_TRACE(Session, new_cfg_peer->GetXmppServer(),
                             "NOT_READY", "NULL", "BGP peer selected as"
                             "config peer on decommission of old config "
                             "peer.");
        }
    }

    // Switch-over Multicast Tree Builder
    if (peer_is_agent_mcast_builder) {
        uint8_t idx = ((agent_mcast_builder->GetXmppServerIdx() == 0)
                       ? 1: 0);
        AgentXmppChannel *new_mcast_builder =
            agent_->controller_xmpp_channel(idx);

        // Selection of new peer as mcast builder is dependant on following
        // criterias:
        // 1) Channel is present (new_mcast_builder is not null)
        // 2) Channel is in READY state
        // 3) BGP peer is commissioned for channel
        bool evaluate_new_mcast_builder =
            AgentXmppChannel::IsBgpPeerActive(agent_, new_mcast_builder);

        if (!evaluate_new_mcast_builder) {
            new_mcast_builder = NULL;
            CONTROLLER_TRACE(Session, GetXmppServer(), "NOT_READY",
                             "NULL", "No elected Multicast Tree Builder");
        }
        AgentXmppChannel::SetMulticastPeer(this, new_mcast_builder);
        if (evaluate_new_mcast_builder) {
            //Advertise subnet and all broadcast routes to
            //the new multicast tree builder
            new_mcast_builder->bgp_peer_id()->
                PeerNotifyMulticastRoutes(true);
            CONTROLLER_TRACE(Session, GetXmppServer(), "NOT_READY",
                             agent_->mulitcast_builder()->
                             GetBgpPeerName(),
                             "Peer elected Multicast Tree Builder");
        }
    }
}

/*
 * AgentXmppChannel state TimedOut
 *
 * Injects NotReady event for this channel.
 *
 * If there are more than two channels available in config, then try picking
 * other channel to replace this timed out channel. And push this channel at the
 * end of the channel list so that it is picked only in worst case.
 * Also this channel gets moved to timed out list where it waits for any other
 * channel to take up the slot(xs_idx_) and is stable(Done to retain
 * config/routes till new channel is stable).
 *
 * If there are only two channels configured, no action to be taken.
 */
void AgentXmppChannel::TimedOut() {
    CONTROLLER_TRACE(Session, GetXmppServer(), "TIMEDOUT",
                     "NULL", "Connection to Xmpp Server, Timed out");
    {
        bool update_list = false;
        std::vector<string>::iterator iter = agent_->GetControllerlist().begin();
        std::vector<string>::iterator end = agent_->GetControllerlist().end();
        for (; iter != end; iter++) {
            std::vector<string> server;
            boost::split(server, *iter, boost::is_any_of(":"));
            if (GetXmppServer().compare(server[0]) == 0) {
                // Add the TIMEDOUT server to the end.
                if (iter+1 == end) break;
                std::rotate(iter, iter+1, end);
                update_list = true;
                break;
            }
        }
        if (update_list) {
            agent_->controller()->ReConnectXmppServer();
        }
    }
}

void AgentXmppChannel::HandleAgentXmppClientChannelEvent(AgentXmppChannel *peer,
                                                         xmps::PeerState state) {
    peer->UpdateConnectionInfo(state);
    if (state == xmps::READY) {
        peer->Ready();
    } else if (state == xmps::NOT_READY) {
        peer->NotReady();
    } else if (state == xmps::TIMEDOUT) {
        peer->TimedOut();
    }
}

EndOfRibTxTimer *AgentXmppChannel::end_of_rib_tx_timer() {
    return end_of_rib_tx_timer_.get();
}

EndOfRibRxTimer *AgentXmppChannel::end_of_rib_rx_timer() {
    return end_of_rib_rx_timer_.get();
}

void AgentXmppChannel::PeerIsNotConfig() {
    if (agent_->ifmap_xmpp_channel(xs_idx_)) {
        agent_->ifmap_xmpp_channel(xs_idx_)->end_of_config_timer()->Cancel();
        agent_->ifmap_xmpp_channel(xs_idx_)->config_cleanup_timer()->Cancel();
    }
}

bool AgentXmppChannel::ControllerSendVmCfgSubscribe(AgentXmppChannel *peer,
                         const boost::uuids::uuid &vm_id,
                         bool subscribe) {
    uint8_t data_[4096];
    size_t datalen_;

    if (!peer) {
        return false;
    }

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");
    pugi->AddAttribute("from", peer->channel_->FromString());
    std::string to(peer->channel_->ToString());
    to += "/";
    to += XmppInit::kConfigPeer;
    pugi->AddAttribute("to", to);

    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    if (subscribe == true) {
        pugi->AddChildNode("subscribe", "");
    } else {
        pugi->AddChildNode("unsubscribe", "");
    }
    std::string vm("virtual-machine:");
    stringstream vmid;
    vmid << vm_id;
    vm += vmid.str();
    pugi->AddAttribute("node", vm);


    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    CONTROLLER_TX_CONFIG_TRACE(Trace, peer->GetXmppServerIdx(),
                               peer->GetBgpPeerName(), "",
              std::string(reinterpret_cast<const char *>(data_), datalen_));
    // send data
    if (peer->SendUpdate(data_,datalen_) == false) {
        CONTROLLER_TRACE(Session, peer->GetXmppServer(),
                         "VM subscribe Send Update deferred", vm, "");
    }

    return true;
}

bool AgentXmppChannel::ControllerSendCfgSubscribe(AgentXmppChannel *peer) {

    uint8_t data_[4096];
    size_t datalen_;

    if (!peer) {
        return false;
    }

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");
    pugi->AddAttribute("from", peer->channel_->FromString());
    std::string to(peer->channel_->ToString());
    to += "/";
    to += XmppInit::kConfigPeer;
    pugi->AddAttribute("to", to);

    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("subscribe", "");
    string node("virtual-router:");
    node  = node + XmppInit::kFqnPrependAgentNodeJID  + peer->channel_->FromString();
    pugi->AddAttribute("node", node);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    CONTROLLER_TX_CONFIG_TRACE(Trace, peer->GetXmppServerIdx(),
                               peer->GetBgpPeerName(), "",
            std::string(reinterpret_cast<const char *>(data_), datalen_));
    // send data
    if (peer->SendUpdate(data_,datalen_) == false) {
        CONTROLLER_TRACE(Session, peer->GetXmppServer(),
                         "Config subscribe Send Update deferred", node, "");
    }
    return true;
}

bool AgentXmppChannel::ControllerSendSubscribe(AgentXmppChannel *peer,
                                               VrfEntry *vrf,
                                               bool subscribe) {
    static int req_id = 0;
    uint8_t data_[4096];
    size_t datalen_;

    if (!peer) {
        return false;
    }
    CONTROLLER_INFO_TRACE(Trace, peer->GetBgpPeerName(), vrf->GetName(),
                     subscribe ? "Subscribe" : "Unsubscribe");
    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");
    pugi->AddAttribute("from", peer->channel_->FromString());
    std::string to(peer->channel_->ToString());
    to += "/";
    to += XmppInit::kBgpPeer;
    pugi->AddAttribute("to", to);

    stringstream request_id;
    request_id << "subscribe" << req_id++;
    pugi->AddAttribute("id", request_id.str());
    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    if (subscribe) {
        pugi->AddChildNode("subscribe", "");
    } else {
        pugi->AddChildNode("unsubscribe", "");
    }
    pugi->AddAttribute("node", vrf->GetName());
    pugi->AddChildNode("options", "" );
    stringstream vrf_id;
    vrf_id << vrf->rd();
    pugi->AddChildNode("instance-id", vrf_id.str());

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));

    // send data
    if (peer->SendUpdate(data_,datalen_) == false) {
        CONTROLLER_TRACE(Session, peer->GetXmppServer(),
                         "Vrf subscribe Send Update deferred", vrf_id.str(), "");
    }
    return true;
}

void PopulateEcmpHashFieldsToUse(ItemType &item,
                                 const EcmpLoadBalance &ecmp_load_balance) {
    item.entry.load_balance.load_balance_decision = LoadBalanceDecision;

    if (ecmp_load_balance.AllSet())
        return;

    ecmp_load_balance.GetStringVector(
        item.entry.load_balance.load_balance_fields.load_balance_field_list);
}

bool AgentXmppChannel::ControllerSendV4V6UnicastRouteCommon(AgentRoute *route,
                                       const VnListType &vn_list,
                                       const SecurityGroupList *sg_list,
                                       const TagList *tag_list,
                                       const CommunityList *communities,
                                       uint32_t mpls_label,
                                       TunnelType::TypeBmap bmap,
                                       const PathPreference &path_preference,
                                       bool associate,
                                       Agent::RouteTableType type,
                                       const EcmpLoadBalance &ecmp_load_balance) {

    static int id = 0;
    ItemType item;
    uint8_t data_[4096];
    size_t datalen_;

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    if (type == Agent::INET4_UNICAST) {
        item.entry.nlri.af = BgpAf::IPv4;
    } else {
        item.entry.nlri.af = BgpAf::IPv6; 
    } 
    item.entry.nlri.safi = BgpAf::Unicast;
    stringstream rstr;
    rstr << route->ToString();
    item.entry.nlri.address = rstr.str();

    string rtr(agent_->router_id().to_string());

    PopulateEcmpHashFieldsToUse(item, ecmp_load_balance);
    autogen::NextHopType nh;
    nh.af = BgpAf::IPv4;
    nh.address = rtr;
    nh.label = mpls_label;
    if (bmap & TunnelType::GREType()) {
        nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
    }
    if (bmap & TunnelType::UDPType()) {
        nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
    }

    if (bmap & TunnelType::NativeType()) {
        nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("native");
    }

    if (tag_list && tag_list->size()) {
        nh.tag_list.tag = *tag_list;
    }

    for (VnListType::const_iterator vnit = vn_list.begin();
         vnit != vn_list.end(); ++vnit) {
        nh.virtual_network = *vnit;
        item.entry.next_hops.next_hop.push_back(nh);
    }

    if (sg_list && sg_list->size()) {
        item.entry.security_group_list.security_group = *sg_list;
    }

    if (communities && !communities->empty()) {
        item.entry.community_tag_list.community_tag = *communities;
    }

    item.entry.version = 1; //TODO
    item.entry.med = 0;

    //Set sequence number and preference of route
    item.entry.sequence_number = path_preference.sequence();
    item.entry.local_preference = path_preference.preference();

    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");

    pugi->AddAttribute("from", channel_->FromString());
    std::string to(channel_->ToString());
    to += "/";
    to += XmppInit::kBgpPeer;
    pugi->AddAttribute("to", to);

    stringstream pubsub_id;
    pubsub_id << "pubsub" << id;
    pugi->AddAttribute("id", pubsub_id.str());

    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("publish", "");

    //Catering for inet4 and evpn unicast routes
    stringstream ss_node;
    ss_node << item.entry.nlri.af << "/"
            << item.entry.nlri.safi << "/"
            << route->vrf()->GetName() << "/"
            << route->GetAddressString();
    std::string node_id(ss_node.str());
    pugi->AddAttribute("node", node_id);
    pugi->AddChildNode("item", "");

    pugi::xml_node node = pugi->FindNode("item");

    //Call Auto-generated Code to encode the struct
    item.Encode(&node);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    SendUpdate(data_,datalen_);

    pugi->DeleteNode("pubsub");
    pugi->ReadNode("iq");

    stringstream collection_id;
    collection_id << "collection" << id++;
    pugi->ModifyAttribute("id", collection_id.str());
    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("collection", "");

    pugi->AddAttribute("node", route->vrf()->GetName());
    if (associate) {
        pugi->AddChildNode("associate", "");
    } else {
        pugi->AddChildNode("dissociate", "");
    }
    pugi->AddAttribute("node", node_id);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    SendUpdate(data_,datalen_);
    end_of_rib_tx_timer()->last_route_published_time_ = UTCTimestampUsec(); 
    return true;
}

bool AgentXmppChannel::BuildTorMulticastMessage(EnetItemType &item,
                                                stringstream &node_id,
                                                AgentRoute *route,
                                                const Ip4Address *nh_ip,
                                                const std::string &vn,
                                                const SecurityGroupList *sg_list,
                                                const TagList *tag_list,
                                                const CommunityList *communities,
                                                uint32_t label,
                                                uint32_t tunnel_bmap,
                                                const std::string &destination,
                                                const std::string &source,
                                                bool associate) {
    assert(route->GetTableType() == Agent::EVPN);
    const AgentPath *path = NULL;
    EvpnRouteEntry *evpn_route =
        dynamic_cast<EvpnRouteEntry *>(route);
    path = evpn_route->FindOvsPath();
    if ((path == NULL) && (associate)) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(),
                         route->vrf()->GetName(),
                         "OVS path not found for ff:ff:ff:ff:ff:ff, skip send");
        return false;
    }

    item.entry.local_preference = PathPreference::LOW;
    item.entry.sequence_number = 0;
    item.entry.replicator_address = source;
    item.entry.nlri.af = BgpAf::L2Vpn;
    item.entry.nlri.safi = BgpAf::Enet;
    stringstream rstr;
    rstr << route->ToString();
    item.entry.nlri.mac = rstr.str();
    item.entry.assisted_replication_supported = false;
    item.entry.edge_replication_not_supported = false;

    rstr.str("");
    //TODO fix this when multicast moves to evpn
    assert(evpn_route->is_multicast());
    rstr << destination;
    rstr << "/32";
    item.entry.nlri.ethernet_tag = 0;
    if (associate == false)
        item.entry.nlri.ethernet_tag = label;

    item.entry.nlri.address = rstr.str();
    assert(item.entry.nlri.address != "0.0.0.0");

    autogen::EnetNextHopType nh;
    nh.af = Address::INET;
    nh.address = destination;
    nh.label = label;

    node_id << item.entry.nlri.af << "/" << item.entry.nlri.safi << "/"
        << route->ToString() << "," << item.entry.nlri.address;
    TunnelType::Type tunnel_type = TunnelType::ComputeType(tunnel_bmap);

    if (path) {
        tunnel_type = path->tunnel_type();
    }
    if (associate) {
        if (tunnel_type != TunnelType::VXLAN) {
            if (tunnel_bmap & TunnelType::GREType()) {
                nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
            }
            if (tunnel_bmap & TunnelType::UDPType()) {
                nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
            }
        } else {
            if (path) {
                nh.label = path->vxlan_id();
                item.entry.nlri.ethernet_tag = nh.label;
            } else {
                nh.label = 0;
            }
            nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("vxlan");
        }

        if (sg_list && sg_list->size()) {
            item.entry.security_group_list.security_group = *sg_list;
        }

        if (tag_list && tag_list->size()) {
            nh.tag_list.tag = *tag_list;
        }
    }

    item.entry.next_hops.next_hop.push_back(nh);
    item.entry.med = 0;
    //item.entry.version = 1; //TODO
    //item.entry.virtual_network = vn;
    return true;
}

//TODO simplify label selection below.
bool AgentXmppChannel::BuildEvpnMulticastMessage(EnetItemType &item,
                                                 stringstream &node_id,
                                                 AgentRoute *route,
                                                 const Ip4Address *nh_ip,
                                                 const std::string &vn,
                                                 const SecurityGroupList *sg_list,
                                                 const TagList *tag_list,
                                                 const CommunityList *communities,
                                                 uint32_t label,
                                                 uint32_t tunnel_bmap,
                                                 bool associate,
                                                 const AgentPath *path,
                                                 bool assisted_replication) {
    assert(route->is_multicast() == true);
    item.entry.local_preference = PathPreference::LOW;
    item.entry.sequence_number = 0;
    if (agent_->simulate_evpn_tor()) {
        item.entry.edge_replication_not_supported = true;
    } else {
        item.entry.edge_replication_not_supported = false;
    }
    item.entry.nlri.af = BgpAf::L2Vpn;
    item.entry.nlri.safi = BgpAf::Enet;
    stringstream rstr;
    //TODO fix this when multicast moves to evpn
    rstr << "0.0.0.0/32";
    item.entry.nlri.address = rstr.str();
    assert(item.entry.nlri.address != "0.0.0.0");

    rstr.str("");
    if (assisted_replication) {
        rstr << route->ToString();
        item.entry.assisted_replication_supported = true;
        node_id << item.entry.nlri.af << "/" << item.entry.nlri.safi << "/"
            << route->ToString() << "," << item.entry.nlri.address;
    } else {
        rstr << route->GetAddressString();
        item.entry.assisted_replication_supported = false;
        node_id << item.entry.nlri.af << "/" << item.entry.nlri.safi << "/"
            << route->GetAddressString() << "," << item.entry.nlri.address;
    }
    item.entry.nlri.mac = route->ToString();

    autogen::EnetNextHopType nh;
    nh.af = Address::INET;
    nh.address = nh_ip->to_string();
    nh.label = label;

    TunnelType::Type tunnel_type = TunnelType::ComputeType(tunnel_bmap);
    item.entry.nlri.ethernet_tag = route->vrf()->isid();
    if (associate == false) {
        //In case of VXLAN ethernet tag is set to vxlan ID.
        //Upon withdraw or encap change label holds the VN ID
        //which is used to withdraw route
        item.entry.nlri.ethernet_tag = label;
    }

    if (path) {
        tunnel_type = path->tunnel_type();
    }
    if (associate) {
        if (tunnel_type != TunnelType::VXLAN) {
            if (tunnel_bmap & TunnelType::GREType()) {
                nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
            }
            if (tunnel_bmap & TunnelType::UDPType()) {
                nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
            }
        } else {
            if (path == NULL)
                path = route->FindPath(agent_->local_peer());

            if (path) {
                nh.label = path->vxlan_id();
                item.entry.nlri.ethernet_tag = nh.label;
            } else {
                nh.label = 0;
            }
            nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("vxlan");
        }

        if (sg_list && sg_list->size()) {
            item.entry.security_group_list.security_group = *sg_list;
        }
    }

    item.entry.next_hops.next_hop.push_back(nh);
    item.entry.med = 0;
    //item.entry.version = 1; //TODO
    //item.entry.virtual_network = vn;
    return true;
}

bool AgentXmppChannel::BuildEvpnUnicastMessage(EnetItemType &item,
                                               stringstream &node_id,
                                               AgentRoute *route,
                                               const Ip4Address *nh_ip,
                                               const std::string &vn,
                                               const SecurityGroupList *sg_list,
                                               const TagList *tag_list,
                                               const CommunityList *communities,
                                               uint32_t label,
                                               uint32_t tunnel_bmap,
                                               const PathPreference
                                               &path_preference,
                                               bool associate) {
    assert(route->is_multicast() == false);
    assert(route->GetTableType() == Agent::EVPN);
    item.entry.local_preference = path_preference.preference();
    item.entry.sequence_number = path_preference.sequence();
    item.entry.assisted_replication_supported = false;
    item.entry.edge_replication_not_supported = false;
    item.entry.nlri.af = BgpAf::L2Vpn;
    item.entry.nlri.safi = BgpAf::Enet;

    stringstream rstr;
    rstr << route->GetAddressString();
    item.entry.nlri.mac = rstr.str();

    const AgentPath *active_path = NULL;
    rstr.str("");
    EvpnRouteEntry *evpn_route = static_cast<EvpnRouteEntry *>(route);
    rstr << evpn_route->ip_addr().to_string() << "/"
        << evpn_route->GetVmIpPlen();
    active_path = evpn_route->FindLocalVmPortPath();
    item.entry.nlri.ethernet_tag = evpn_route->ethernet_tag();

    item.entry.nlri.address = rstr.str();
    assert(item.entry.nlri.address != "0.0.0.0");

    item.entry.etree_leaf = true;
    if (active_path) {
        item.entry.etree_leaf = active_path->etree_leaf();
    }

    autogen::EnetNextHopType nh;
    nh.af = Address::INET;
    nh.address = nh_ip->to_string();
    nh.label = label;
    TunnelType::Type tunnel_type = TunnelType::ComputeType(tunnel_bmap);
    if (active_path) {
        tunnel_type = active_path->tunnel_type();
    }
    if (associate) {
        if (tunnel_type != TunnelType::VXLAN) {
            if (tunnel_bmap & TunnelType::GREType()) {
                nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
            }
            if (tunnel_bmap & TunnelType::UDPType()) {
                nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
            }
        } else {
            if (active_path) {
                nh.label = active_path->vxlan_id();
            } else {
                nh.label = 0;
            }
            nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("vxlan");
        }

        if (sg_list && sg_list->size()) {
            item.entry.security_group_list.security_group = *sg_list;
        }

        if (tag_list && tag_list->size()) {
            nh.tag_list.tag = *tag_list;
        }

    }

    item.entry.next_hops.next_hop.push_back(nh);
    item.entry.med = 0;
    //item.entry.version = 1; //TODO
    //item.entry.virtual_network = vn;

    node_id << item.entry.nlri.af << "/" << item.entry.nlri.safi << "/"
        << route->GetAddressString() << "," << item.entry.nlri.address;
    return true;
}

bool AgentXmppChannel::BuildAndSendEvpnDom(EnetItemType &item,
                                           stringstream &ss_node,
                                           const AgentRoute *route,
                                           bool associate) {
    static int id = 0;
    uint8_t data_[4096];
    size_t datalen_;

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");

    pugi->AddAttribute("from", channel_->FromString());
    std::string to(channel_->ToString());
    to += "/";
    to += XmppInit::kBgpPeer;
    pugi->AddAttribute("to", to);

    stringstream pubsub_id;
    pubsub_id << "pubsub_l2" << id;
    pugi->AddAttribute("id", pubsub_id.str());

    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("publish", "");

    std::string node_id(ss_node.str());
    pugi->AddAttribute("node", node_id);
    pugi->AddChildNode("item", "");

    pugi::xml_node node = pugi->FindNode("item");

    //Call Auto-generated Code to encode the struct
    item.Encode(&node);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    SendUpdate(data_,datalen_);

    pugi->DeleteNode("pubsub");
    pugi->ReadNode("iq");

    stringstream collection_id;
    collection_id << "collection_l2" << id++;
    pugi->ModifyAttribute("id", collection_id.str());
    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("collection", "");

    pugi->AddAttribute("node", route->vrf()->GetExportName());
    if (associate) {
        pugi->AddChildNode("associate", "");
    } else {
        pugi->AddChildNode("dissociate", "");
    }
    pugi->AddAttribute("node", node_id);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    SendUpdate(data_,datalen_);
    end_of_rib_tx_timer()->last_route_published_time_ = UTCTimestampUsec(); 
    return true;
}

bool AgentXmppChannel::ControllerSendEvpnRouteCommon(AgentRoute *route,
                                                     const Ip4Address *nh_ip,
                                                     std::string vn,
                                                     const SecurityGroupList *sg_list,
                                                     const TagList *tag_list,
                                                     const CommunityList *communities,
                                                     uint32_t label,
                                                     uint32_t tunnel_bmap,
                                                     const std::string &destination,
                                                     const std::string &source,
                                                     const PathPreference
                                                     &path_preference,
                                                     bool associate) {
    EnetItemType item;
    stringstream ss_node;
    bool ret = true;

    if (label == MplsTable::kInvalidLabel) return false;

    if (route->is_multicast()) {
        BridgeRouteEntry *l2_route =
            dynamic_cast<BridgeRouteEntry *>(route);
        if (agent_->tsn_enabled()) {
            //Second subscribe for TSN assited replication
            if (BuildEvpnMulticastMessage(item, ss_node, route, nh_ip, vn,
                                          sg_list, tag_list, communities,
                                          label, tunnel_bmap, associate,
                                          l2_route->FindPath(agent_->
                                                             local_peer()),
                                          true) == false)
                return false;
            ret |= BuildAndSendEvpnDom(item, ss_node,
                                       route, associate);
        } else if (agent_->tor_agent_enabled()) {
            if (BuildTorMulticastMessage(item, ss_node, route, nh_ip, vn,
                                         sg_list, tag_list, communities, label,
                                         tunnel_bmap, destination, source,
                                         associate) == false)
                return false;;
            ret = BuildAndSendEvpnDom(item, ss_node, route, associate);
        } else {
            const AgentPath *path =
                l2_route->FindPath(agent_->multicast_peer());
            if (BuildEvpnMulticastMessage(item, ss_node, route, nh_ip, vn,
                                          sg_list, tag_list, communities, label,
                                          tunnel_bmap, associate,
                                          path,
                                          false) == false)
                return false;
            ret = BuildAndSendEvpnDom(item, ss_node, route, associate);
        }
    } else {
        if (BuildEvpnUnicastMessage(item, ss_node, route, nh_ip, vn, sg_list,
                                tag_list, communities, label, tunnel_bmap,
                                path_preference, associate) == false)
            return false;;
            ret = BuildAndSendEvpnDom(item, ss_node, route, associate);
    }
    return ret;
}

bool AgentXmppChannel::ControllerSendMcastRouteCommon(AgentRoute *route,
                                                      bool add_route) {
    static int id = 0;
    autogen::McastItemType item;
    uint8_t data_[4096];
    size_t datalen_;

    if (add_route && (agent_->mulitcast_builder() != this)) {
        CONTROLLER_INFO_TRACE(Trace, GetBgpPeerName(),
                                    route->vrf()->GetName(),
                                    "Peer not elected Multicast Tree Builder");
        return false;
    }

    CONTROLLER_INFO_TRACE(McastSubscribe, GetBgpPeerName(),
                                route->vrf()->GetName(), " ",
                                route->ToString());

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    item.entry.nlri.af = BgpAf::IPv4;
    item.entry.nlri.safi = BgpAf::Mcast;
    item.entry.nlri.group = route->GetAddressString();
    item.entry.nlri.source = "0.0.0.0";

    autogen::McastNextHopType item_nexthop;
    item_nexthop.af = BgpAf::IPv4;
    string rtr(agent_->router_id().to_string());
    item_nexthop.address = rtr;
    item_nexthop.label = GetMcastLabelRange();
    item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
    item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
    item.entry.next_hops.next_hop.push_back(item_nexthop);

    //Build the pugi tree
    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");
    pugi->AddAttribute("from", channel_->FromString());
    std::string to(channel_->ToString());
    to += "/";
    to += XmppInit::kBgpPeer;
    pugi->AddAttribute("to", to);

    std::string pubsub_id("pubsub_b");
    stringstream str_id;
    str_id << id;
    pubsub_id += str_id.str();
    pugi->AddAttribute("id", pubsub_id);

    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("publish", "");
    stringstream ss_node;
    ss_node << item.entry.nlri.af << "/"
            << item.entry.nlri.safi << "/"
            << route->vrf()->GetExportName() << "/"
            << route->GetAddressString();
    std::string node_id(ss_node.str());
    pugi->AddAttribute("node", node_id);
    pugi->AddChildNode("item", "");

    pugi::xml_node node = pugi->FindNode("item");

    //Call Auto-generated Code to encode the struct
    item.Encode(&node);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    SendUpdate(data_,datalen_);


    pugi->DeleteNode("pubsub");
    pugi->ReadNode("iq");

    stringstream collection_id;
    collection_id << "collection" << id++;
    pugi->ModifyAttribute("id", collection_id.str());
    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("collection", "");

    pugi->AddAttribute("node", route->vrf()->GetName());
    if (add_route) {
        pugi->AddChildNode("associate", "");
    } else {
        pugi->AddChildNode("dissociate", "");
    }
    pugi->AddAttribute("node", node_id);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    SendUpdate(data_,datalen_);
    end_of_rib_tx_timer()->last_route_published_time_ = UTCTimestampUsec(); 
    return true;
}

bool AgentXmppChannel::ControllerSendEvpnRouteAdd(AgentXmppChannel *peer,
                                                  AgentRoute *route,
                                                  const Ip4Address *nh_ip,
                                                  std::string vn,
                                                  uint32_t label,
                                                  uint32_t tunnel_bmap,
                                                  const SecurityGroupList *sg_list,
                                                  const TagList *tag_list,
                                                  const CommunityList *communities,
                                                  const std::string &destination,
                                                  const std::string &source,
                                                  const PathPreference
                                                  &path_preference) {
    if (!peer) return false;

    CONTROLLER_INFO_TRACE(RouteExport, peer->GetBgpPeerName(),
                                route->vrf()->GetName(),
                                route->ToString(), true, label);
    return (peer->ControllerSendEvpnRouteCommon(route,
                                                nh_ip,
                                                vn,
                                                sg_list,
                                                tag_list,
                                                communities,
                                                label,
                                                tunnel_bmap,
                                                destination,
                                                source,
                                                path_preference,
                                                true));
}

bool AgentXmppChannel::ControllerSendEvpnRouteDelete(AgentXmppChannel *peer,
                                                     AgentRoute *route,
                                                     std::string vn,
                                                     uint32_t label,
                                                     const std::string &destination,
                                                     const std::string &source,
                                                     uint32_t tunnel_bmap) {
    if (!peer) return false;

    CONTROLLER_INFO_TRACE(RouteExport, peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(), false, label);
    Ip4Address nh_ip = Ip4Address(0);
    return (peer->ControllerSendEvpnRouteCommon(route,
                                                &nh_ip,
                                                vn,
                                                NULL,
                                                NULL,
                                                NULL,
                                                label,
                                                tunnel_bmap,
                                                destination,
                                                source,
                                                PathPreference(),
                                                false));
}

bool AgentXmppChannel::ControllerSendRouteAdd(AgentXmppChannel *peer,
                                              AgentRoute *route,
                                              const Ip4Address *nexthop_ip,
                                              const VnListType &vn_list,
                                              uint32_t label,
                                              TunnelType::TypeBmap bmap,
                                              const SecurityGroupList *sg_list,
                                              const TagList *tag_list,
                                              const CommunityList *communities,
                                              Agent::RouteTableType type,
                                              const PathPreference &path_preference,
                                              const EcmpLoadBalance &ecmp_load_balance)
{
    if (!peer) return false;

    CONTROLLER_INFO_TRACE(RouteExport,
                     peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(),
                     true, label);
    bool ret = false;
    if (((type == Agent::INET4_UNICAST) || (type == Agent::INET6_UNICAST)) &&
         (peer->agent()->simulate_evpn_tor() == false)) {
        ret = peer->ControllerSendV4V6UnicastRouteCommon(route, vn_list,
                                                   sg_list, tag_list, communities, label,
                                                   bmap, path_preference, true,
                                                   type, ecmp_load_balance);
    }
    if (type == Agent::EVPN) {
        std::string vn;
        if (vn_list.size())
            vn = *vn_list.begin();
        ret = peer->ControllerSendEvpnRouteCommon(route, nexthop_ip, vn,
                                                  sg_list, tag_list, communities, label,
                                                  bmap, "", "",
                                                  path_preference, true);
    }
    return ret;
}

bool AgentXmppChannel::ControllerSendRouteDelete(AgentXmppChannel *peer,
                                          AgentRoute *route,
                                          const VnListType &vn_list,
                                          uint32_t label,
                                          TunnelType::TypeBmap bmap,
                                          const SecurityGroupList *sg_list,
                                          const TagList *tag_list,
                                          const CommunityList *communities,
                                          Agent::RouteTableType type,
                                          const PathPreference
                                          &path_preference)
{
    if (!peer) return false;

    CONTROLLER_INFO_TRACE(RouteExport,
                     peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(),
                     false, 0);
    bool ret = false;
    if (((type == Agent::INET4_UNICAST) || (type == Agent::INET6_UNICAST)) &&
         (peer->agent()->simulate_evpn_tor() == false)) {
        EcmpLoadBalance ecmp_load_balance;
        ret = peer->ControllerSendV4V6UnicastRouteCommon(route, vn_list,
                                                  sg_list, tag_list, communities,
                                                       label,
                                                       bmap,
                                                       path_preference,
                                                       false,
                                                       type,
                                                       ecmp_load_balance);
    }
    if (type == Agent::EVPN) {
        Ip4Address nh_ip(0);
        std::string vn;
        if (vn_list.size())
            vn = *vn_list.begin();
        ret = peer->ControllerSendEvpnRouteCommon(route, &nh_ip,
                                                  vn, NULL, NULL, NULL,
                                                  label, bmap, "", "",
                                                  path_preference, false);
    }
    return ret;
}

bool AgentXmppChannel::ControllerSendMcastRouteAdd(AgentXmppChannel *peer,
                                                   AgentRoute *route) {
    if (!peer) return false;

    CONTROLLER_INFO_TRACE(RouteExport, peer->GetBgpPeerName(),
                                route->vrf()->GetName(),
                                route->ToString(), true, 0);
    return peer->ControllerSendMcastRouteCommon(route, true);
}

bool AgentXmppChannel::ControllerSendMcastRouteDelete(AgentXmppChannel *peer,
                                                      AgentRoute *route) {
    if (!peer) return false;

    CONTROLLER_INFO_TRACE(RouteExport, peer->GetBgpPeerName(),
                                route->vrf()->GetName(),
                                route->ToString(), false, 0);

    return peer->ControllerSendMcastRouteCommon(route, false);
}

void AgentXmppChannel::EndOfRibRx() {
    end_of_rib_rx_timer()->end_of_rib_rx_time_ = UTCTimestampUsec();
    bgp_peer_id()->DeleteStale();
    agent()->controller()->FlushTimedOutChannels(xs_idx_);
    if (agent()->mulitcast_builder() == this) {
        MulticastHandler::GetInstance()->FlushPeerInfo(agent()->
                             controller()->multicast_sequence_number());
    }
}

void AgentXmppChannel::EndOfRibTx() {
    //This is a callback from walker for bgp peer.
    //It may happen that channel went down and stop of this walk was executed.
    //However stop of the walk is enqueued and by that time, walk done for
    //previously started walk for this peer gets executed.
    //This can result in channel_ being NULL on walk done call.
    if (channel_ == NULL) {
        return;
    }

    string msg;
    msg += "\n<message from=\"";
    msg += channel_->FromString();
    msg += "\" to=\"";
    msg += channel_->ToString();
    msg += "/";
    msg += XmppInit::kBgpPeer;
    msg += "\">";
    msg += "\n\t<event xmlns=\"http://jabber.org/protocol/pubsub\">";
    msg = (msg + "\n<items node=\"") + XmppInit::kEndOfRibMarker +
          "\"></items>";
    msg += "\n\t</event>\n</message>\n";

    if (channel_->connection()) {
        channel_->connection()->Send((const uint8_t *) msg.data(), msg.size());
        end_of_rib_tx_timer()->end_of_rib_tx_time_ = UTCTimestampUsec();
    }
}

void AgentXmppChannel::UpdateConnectionInfo(xmps::PeerState state) {

    if (agent_->connection_state() == NULL)
        return;

    boost::asio::ip::tcp::endpoint ep;
    boost::system::error_code ec;
    string last_state_name;
    ep.address(boost::asio::ip::address::from_string(agent_->
                controller_ifmap_xmpp_server(xs_idx_), ec));
    ep.port(agent_->controller_ifmap_xmpp_port(xs_idx_));
    const string name = agent_->xmpp_control_node_prefix() +
                        ep.address().to_string();
    XmppChannel *xc = GetXmppChannel();
    if (xc) {
        last_state_name = xc->LastStateName();
    }
    if (state == xmps::READY) {
        agent_->connection_state()->Update(ConnectionType::XMPP, name,
                                           ConnectionStatus::UP, ep,
                                           last_state_name);
    } else {
        agent_->connection_state()->Update(ConnectionType::XMPP, name,
                                           ConnectionStatus::DOWN, ep,
                                           last_state_name);
    }
}

void AgentXmppChannel::StartEndOfRibTxWalker() {
    if (bgp_peer_id()) {
        bgp_peer_id()->PeerNotifyRoutes(
                       boost::bind(&AgentXmppChannel::EndOfRibTx, this));
    }
}

void AgentXmppChannel::StopEndOfRibTxWalker() {
    if (bgp_peer_id()) {
        bgp_peer_id()->StopPeerNotifyRoutes();
    }
}

void AgentXmppChannel::BuildTagList(const autogen::ItemType *item,
                                    TagList *tag_list) {
    *tag_list = item->entry.next_hops.next_hop[0].tag_list.tag;
    std::sort(tag_list->begin(), tag_list->end());
}

void AgentXmppChannel::BuildTagList(const autogen::EnetItemType *item,
                                    TagList *tag_list) {
    *tag_list = item->entry.next_hops.next_hop[0].tag_list.tag;
    std::sort(tag_list->begin(), tag_list->end());
}
