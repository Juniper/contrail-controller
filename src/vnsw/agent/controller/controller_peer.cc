/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/util.h>
#include <base/logging.h>
#include <net/bgp_af.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include "cmn/agent_cmn.h"
#include "cmn/agent_stats.h"
#include "controller/controller_peer.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_vrf_export.h"
#include "controller/controller_init.h"
#include "oper/vrf.h"
#include "oper/nexthop.h"
#include "oper/mirror_table.h"
#include "oper/multicast.h"
#include "oper/peer.h"
#include "oper/vxlan.h"
#include <pugixml/pugixml.hpp>
#include "xml/xml_pugi.h"
#include "xmpp/xmpp_init.h"
#include "xmpp_multicast_types.h"
#include "ifmap/ifmap_agent_table.h"
#include "controller/controller_types.h"
#include "net/tunnel_encap_type.h"

using namespace boost::asio;
using namespace autogen;
 
AgentXmppChannel::AgentXmppChannel(XmppChannel *channel, std::string xmpp_server, 
                                   std::string label_range, uint8_t xs_idx) 
    : channel_(channel), xmpp_server_(xmpp_server), label_range_(label_range),
      xs_idx_(xs_idx) {

    channel_->RegisterReceive(xmps::BGP, 
                              boost::bind(&AgentXmppChannel::ReceiveInternal, 
                                          this, _1));
    DBTableBase::ListenerId id = 
        Agent::GetInstance()->GetVrfTable()->Register(boost::bind(&VrfExport::Notify,
                                       this, _1, _2)); 
    bgp_peer_id_ = new BgpPeer(Agent::GetInstance()->GetXmppServer(xs_idx_), this, id);
}

AgentXmppChannel::~AgentXmppChannel() {

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(bgp_peer_id_);
    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();

    Agent::GetInstance()->GetVrfTable()->Unregister(id);
    delete bgp_peer_id_;
    channel_->UnRegisterReceive(xmps::BGP);
}

bool AgentXmppChannel::SendUpdate(uint8_t *msg, size_t size) {

    if (channel_ && 
        (channel_->GetPeerState() == xmps::READY)) {
        AgentStats::GetInstance()->incr_xmpp_out_msgs(xs_idx_);
	    return channel_->Send(msg, size, xmps::BGP,
			  boost::bind(&AgentXmppChannel::WriteReadyCb, this, _1));
    } else {
        return false; 
    }
}

void AgentXmppChannel::ReceiveEvpnUpdate(XmlPugi *pugi) {
    pugi::xml_node node = pugi->FindNode("items");
    pugi::xml_attribute attr = node.attribute("node");

    char *saveptr;
    strtok_r(const_cast<char *>(attr.value()), "/", &saveptr);
    strtok_r(NULL, "/", &saveptr);
    char *vrf_name =  strtok_r(NULL, "", &saveptr);
    const std::string vrf(vrf_name);
    Layer2AgentRouteTable *rt_table = 
        static_cast<Layer2AgentRouteTable *>
        (Agent::GetInstance()->GetVrfTable()->GetLayer2RouteTable(vrf_name));

    pugi::xml_node node_check = pugi->FindNode("retract");
    if (!pugi->IsNull(node_check)) {
        for (node = node.first_child(); node; node = node.next_sibling()) {
            if (strcmp(node.name(), "retract") == 0)  {
                std::string id = node.first_attribute().value();
                CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                                 "EVPN Delete Node id:" + id);

                char *mac_str = 
                    strtok_r(const_cast<char *>(id.c_str()), "-", &saveptr);
                //char *mac_str = strtok_r(NULL, ",", &saveptr);
                struct ether_addr mac = *ether_aton(mac_str);;
                rt_table->DeleteReq(bgp_peer_id_, vrf_name, mac);
            }
        }
        return;
    }

    //Call Auto-generated Code to return struct
    auto_ptr<AutogenProperty> xparser(new AutogenProperty());
    if (EnetItemsType::XmlParseProperty(node, &xparser) == false) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
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
            struct ether_addr mac = *ether_aton((item->entry.nlri.mac).c_str());
            AddEvpnRoute(vrf_name, mac, item);
        } else {
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
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
        if (encap == TunnelEncapType::MPLS_O_GRE)
            bmap |= (1 << TunnelType::MPLS_GRE);
        if (encap == TunnelEncapType::MPLS_O_UDP)
            bmap |= (1 << TunnelType::MPLS_UDP);
        if (encap == TunnelEncapType::VXLAN)
            bmap |= (1 << TunnelType::VXLAN);
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
        if (encap == TunnelEncapType::MPLS_O_GRE)
            bmap |= (1 << TunnelType::MPLS_GRE);
        if (encap == TunnelEncapType::MPLS_O_UDP)
            bmap |= (1 << TunnelType::MPLS_UDP);
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
        if (encap == TunnelEncapType::MPLS_O_GRE)
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
        if (bgp_peer_id_ !=
            Agent::GetInstance()->GetControlNodeMulticastBuilder()->
            GetBgpPeer()) {
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                       "Ignore retract request from non multicast tree "
                       "builder peer; Multicast Delete Node id:" + retract_id);
            return;
        }

        for (node = node.first_child(); node; node = node.next_sibling()) {
            if (strcmp(node.name(), "retract") == 0) { 
                std::string id = node.first_attribute().value();
                CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                                "Multicast Delete Node id:" + id);

                // Parse identifier to obtain group,source
                // <addr:VRF:Group,Source) 
                strtok_r(const_cast<char *>(id.c_str()), ":", &saveptr);
                strtok_r(NULL, ":", &saveptr);
                char *group = strtok_r(NULL, ",", &saveptr);
                char *source = strtok_r(NULL, "", &saveptr);
                if (group == NULL || source == NULL) {
                    CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name, 
                       "Error parsing multicast group address from retract id");
                    return;
                }

                boost::system::error_code ec;
                IpAddress g_addr =
                    IpAddress::from_string(group, ec);
                if (ec.value() != 0) {
                    CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name, 
                            "Error parsing multicast group address");
                    return;
                }

                IpAddress s_addr =
                    IpAddress::from_string(source, ec);
                if (ec.value() != 0) {
                    CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name, 
                            "Error parsing multicast source address");
                    return;
                }

                MulticastHandler::ModifyFabricMembers(vrf, g_addr.to_v4(),
                        s_addr.to_v4(), 0,
                        olist);
            }
        }
        return;
    }

    pugi::xml_node items_node = pugi->FindNode("item");
    if (!pugi->IsNull(items_node)) {
        pugi->ReadNode("item"); //sets the context
        std::string item_id = pugi->ReadAttrib("id");
        if (bgp_peer_id_ !=
            Agent::GetInstance()->GetControlNodeMulticastBuilder()->
            GetBgpPeer()) {
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                             "Ignore request from non multicast tree "
                             "builder peer; Multicast Delete Node:" + item_id);
            return;
        }
    }

    //Call Auto-generated Code to return struct
    auto_ptr<AutogenProperty> xparser(new AutogenProperty());
    if (McastItemsType::XmlParseProperty(node, &xparser) == false) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name, 
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
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                             "Error parsing multicast group address");
            return;
        }

        IpAddress s_addr =
            IpAddress::from_string(item->entry.nlri.source, ec);
        if (ec.value() != 0) {
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                            "Error parsing multicast source address");
            return;
        }

        std::vector<McastNextHopType>::iterator iter;
        for (iter = item->entry.olist.next_hop.begin();
                iter != item->entry.olist.next_hop.end(); iter++) {

            McastNextHopType nh = *iter;
            IpAddress addr = IpAddress::from_string(nh.address, ec);
            if (ec.value() != 0) {
                CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                                 "Error parsing next-hop address");
                return;
            }

            int label;
            stringstream nh_label(nh.label);
            nh_label >> label;
            TunnelType::TypeBmap encap = 
                GetMcastTypeBitmap(nh.tunnel_encapsulation_list);
            olist.push_back(OlistTunnelEntry(label, addr.to_v4(), encap)); 
                                             //TunnelType::DefaultTypeBmap()));
        }

        MulticastHandler::ModifyFabricMembers(vrf, g_addr.to_v4(),
                s_addr.to_v4(), item->entry.nlri.source_label,
                olist);
    }
}

void AgentXmppChannel::AddEcmpRoute(string vrf_name, Ip4Address prefix_addr, 
                                    uint32_t prefix_len, ItemType *item) {
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (Agent::GetInstance()->GetVrfTable()->GetInet4UnicastRouteTable
         (vrf_name));

    std::vector<ComponentNHData> comp_nh_list;
    for (uint32_t i = 0; i < item->entry.next_hops.next_hop.size(); i++) {
        std::string nexthop_addr = 
            item->entry.next_hops.next_hop[i].address;
        boost::system::error_code ec;
        IpAddress addr = IpAddress::from_string(nexthop_addr, ec);
        if (ec.value() != 0) {
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                             "Error parsing nexthop ip address");
            continue;
        }

        uint32_t label = item->entry.next_hops.next_hop[i].label;
        if (Agent::GetInstance()->GetRouterId() == addr.to_v4()) {
            //Get local list of interface and append to the list
            MplsLabel *mpls = 
                Agent::GetInstance()->GetMplsTable()->FindMplsLabel(label);
            if (mpls != NULL) {
                DBEntryBase::KeyPtr key = mpls->GetNextHop()->GetDBRequestKey();
                NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
                nh_key->SetPolicy(false);
                ComponentNHData nh_data(label, nh_key);
                comp_nh_list.push_back(nh_data);
            }
        } else {
            TunnelType::TypeBmap encap = GetTypeBitmap
                (item->entry.next_hops.next_hop[i].tunnel_encapsulation_list);
            ComponentNHData nh_data(label, Agent::GetInstance()->GetDefaultVrf(),
                                    Agent::GetInstance()->GetRouterId(), 
                                    addr.to_v4(), false, encap);
            comp_nh_list.push_back(nh_data);
        }
    }
    //ECMP create component NH
    rt_table->AddRemoteVmRouteReq(bgp_peer_id_, vrf_name,
                                  prefix_addr, prefix_len, comp_nh_list, -1,
                                  item->entry.virtual_network, 
                                  item->entry.security_group_list.security_group);
}

void AgentXmppChannel::AddRemoteEvpnRoute(string vrf_name, 
                                      struct ether_addr &mac, 
                                      EnetItemType *item) {
    boost::system::error_code ec; 
    string nexthop_addr = item->entry.next_hops.next_hop[0].address;
    uint32_t label = item->entry.next_hops.next_hop[0].label;
    TunnelType::TypeBmap encap = GetEnetTypeBitmap
        (item->entry.next_hops.next_hop[0].tunnel_encapsulation_list);
    IpAddress addr = IpAddress::from_string(nexthop_addr, ec);
    Layer2AgentRouteTable *rt_table = 
        static_cast<Layer2AgentRouteTable *>
        (Agent::GetInstance()->GetVrfTable()->GetLayer2RouteTable(vrf_name));

    if (ec.value() != 0) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                         "Error parsing nexthop ip address");
        return;
    }

    stringstream str;
    str << (ether_ntoa ((struct ether_addr *)&mac)); 
    CONTROLLER_TRACE(RouteImport, bgp_peer_id_->GetName(), vrf_name, 
                     str.str(), 0, nexthop_addr, label, "");

    Ip4Address prefix_addr;
    int prefix_len;
    ec = Ip4PrefixParse(item->entry.nlri.address, &prefix_addr,
                        &prefix_len);
    if (ec.value() != 0) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                         "Error parsing route address");
        return;
    }
    if (Agent::GetInstance()->GetRouterId() != addr.to_v4()) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), nexthop_addr,
                         "add remote evpn route");
        rt_table->AddRemoteVmRouteReq(bgp_peer_id_, vrf_name, encap,
                                      addr.to_v4(), label, mac,
                                      prefix_addr, prefix_len);
        return;
    }

    const NextHop *nh = NULL;
    if (encap == (1 << TunnelType::VXLAN)) {
        VrfEntry *vrf = 
            Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
        Layer2RouteKey key(Agent::GetInstance()->GetLocalVmPeer(), 
                           vrf_name, mac);
        if (vrf != NULL) {
            Layer2RouteEntry *route = 
                static_cast<Layer2RouteEntry *>
                (static_cast<Layer2AgentRouteTable *>
                 (vrf->GetLayer2RouteTable())->FindActiveEntry(&key));
            if (route) {
                nh = route->GetActiveNextHop();
            } else {
                CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                                 "route not found, ignoring request");
            }
        } else {
                CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                                 "vrf not found, ignoring request");
        }
    } else {
        MplsLabel *mpls = 
            Agent::GetInstance()->GetMplsTable()->FindMplsLabel(label);
        if (mpls != NULL) {
            nh = mpls->GetNextHop();
        }
    }
    if (nh != NULL) {
        switch(nh->GetType()) {
        case NextHop::INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            rt_table->AddLocalVmRouteReq(bgp_peer_id_, intf_nh->GetIfUuid(),
                                          "", vrf_name, label, encap, 
                                          mac, prefix_addr, prefix_len);
            break;
            }
        default:
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                             "label points to invalid NH");
        }
    } else {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                         "nexthop not found, ignoring request");
    }
}

void AgentXmppChannel::AddRemoteRoute(string vrf_name, Ip4Address prefix_addr, 
                                      uint32_t prefix_len, ItemType *item) {
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (Agent::GetInstance()->GetVrfTable()->GetInet4UnicastRouteTable
         (vrf_name));

    boost::system::error_code ec; 
    string nexthop_addr = item->entry.next_hops.next_hop[0].address;
    uint32_t label = item->entry.next_hops.next_hop[0].label;
    IpAddress addr = IpAddress::from_string(nexthop_addr, ec);
    TunnelType::TypeBmap encap = GetTypeBitmap
        (item->entry.next_hops.next_hop[0].tunnel_encapsulation_list);

    if (ec.value() != 0) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                         "Error parsing nexthop ip address");
        return;
    }

    CONTROLLER_TRACE(RouteImport, bgp_peer_id_->GetName(), vrf_name, 
                     prefix_addr.to_string(), prefix_len, 
                     addr.to_v4().to_string(), label, 
                     item->entry.virtual_network);

    if (Agent::GetInstance()->GetRouterId() != addr.to_v4()) {
        rt_table->AddRemoteVmRouteReq(bgp_peer_id_, vrf_name,
                                      prefix_addr, prefix_len, addr.to_v4(),
                                      encap, label, item->entry.virtual_network,
                                      item->entry.security_group_list.security_group);
        return;
    }

    MplsLabel *mpls = Agent::GetInstance()->GetMplsTable()->FindMplsLabel(label);
    if (mpls != NULL) {
        const NextHop *nh = mpls->GetNextHop();
        switch(nh->GetType()) {
        case NextHop::INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            const Interface *interface = intf_nh->GetInterface();
            if (interface == NULL) {
                break;
            }

            if (interface->type() == Interface::VM_INTERFACE) {
                rt_table->AddLocalVmRouteReq(bgp_peer_id_, vrf_name, prefix_addr,
                                             prefix_len, intf_nh->GetIfUuid(),
                                             item->entry.virtual_network, label,
                                             item->entry.security_group_list.security_group,
                                             false);
            } else if (interface->type() == Interface::INET) {
                rt_table->AddInetInterfaceRoute(bgp_peer_id_, vrf_name,
                                                 prefix_addr, prefix_len,
                                                 interface->name(),
                                                 label,
                                                 item->entry.virtual_network);
            } else {
                // Unsupported scenario
                CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                                 "MPLS label points to invalid interface type");
                 break;
            }

            break;
            }

        case NextHop::VLAN: {
            const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
            const VmInterface *vm_port =
                static_cast<const VmInterface *>(vlan_nh->GetInterface());
            std::vector<int> sg_l;
            vm_port->CopySgIdList(&sg_l);
            rt_table->AddVlanNHRouteReq(bgp_peer_id_, vrf_name, prefix_addr,
                                        prefix_len, vlan_nh->GetIfUuid(),
                                        vlan_nh->GetVlanTag(), label,
                                        item->entry.virtual_network, sg_l);
            break;
            }
        case NextHop::COMPOSITE: {
            AddEcmpRoute(vrf_name, prefix_addr, prefix_len, item);
            break;
            }

        default:
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                             "MPLS label points to invalid NH");
        }
    }
}

void AgentXmppChannel::AddEvpnRoute(string vrf_name, 
                                   struct ether_addr &mac, 
                                   EnetItemType *item) {
    if (item->entry.next_hops.next_hop.size() > 1) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                         "Multiple NH in evpn not supported");
    } else {
        AddRemoteEvpnRoute(vrf_name, mac, item);
    }
}

void AgentXmppChannel::AddRoute(string vrf_name, Ip4Address prefix_addr, 
                                uint32_t prefix_len, ItemType *item) {
    if (item->entry.next_hops.next_hop.size() > 1) {
        AddEcmpRoute(vrf_name, prefix_addr, prefix_len, item);
    } else {
        AddRemoteRoute(vrf_name, prefix_addr, prefix_len, item);
    }
}

void AgentXmppChannel::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
    
    AgentStats::GetInstance()->incr_xmpp_in_msgs(xs_idx_);
    if (msg && msg->type == XmppStanza::MESSAGE_STANZA) {
      
        XmlBase *impl = msg->dom.get();
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);        
        pugi::xml_node node = pugi->FindNode("items");
        pugi->ReadNode("items"); //sets the context
        std::string nodename = pugi->ReadAttrib("node");

        const char *af = NULL, *safi = NULL, *vrf_name;
        char *str = const_cast<char *>(nodename.c_str());
        char *saveptr;
        af = strtok_r(str, "/", &saveptr);
        safi = strtok_r(NULL, "/", &saveptr);
        vrf_name = saveptr;

        if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Mcast) {
            ReceiveMulticastUpdate(pugi);
            return;
        }
        if (atoi(af) == BgpAf::L2Vpn && atoi(safi) == BgpAf::Enet) {
            ReceiveEvpnUpdate(pugi);
            return;
        }

        VrfKey vrf_key(vrf_name);
        VrfEntry *vrf = 
            static_cast<VrfEntry *>(Agent::GetInstance()->GetVrfTable ()->FindActiveEntry(&vrf_key));
        if (!vrf) {
            CONTROLLER_TRACE (Trace, bgp_peer_id_->GetName (), vrf_name,
                    "VRF not found");
            return;
        }

        Inet4UnicastAgentRouteTable *rt_table = 
            static_cast<Inet4UnicastAgentRouteTable *>
            (vrf->GetInet4UnicastRouteTable());
        if (!rt_table) {
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name, 
                             "VRF not found");
            return;
        }

        if (!pugi->IsNull(node)) {
  
            pugi::xml_node node_check = pugi->FindNode("retract");
            if (!pugi->IsNull(node_check)) {
                for (node = node.first_child(); node; node = node.next_sibling()) {
                    if (strcmp(node.name(), "retract") == 0)  {
                        std::string id = node.first_attribute().value();
                        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                                        "Delete Node id:" + id);
                        boost::system::error_code ec;
                        Ip4Address prefix_addr;
                        int prefix_len;
                        ec = Ip4PrefixParse(id, &prefix_addr, &prefix_len);
                        if (ec.value() != 0) {
                            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                                    "Error parsing prefix for delete");
                            return;
                        }
                        rt_table->DeleteReq(bgp_peer_id_, vrf_name,
                                prefix_addr, prefix_len);
                    }
                }
                return;
            }
           
            //Call Auto-generated Code to return struct
            auto_ptr<AutogenProperty> xparser(new AutogenProperty());
            if (ItemsType::XmlParseProperty(node, &xparser) == false) {
                CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
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
                Ip4Address prefix_addr;
                int prefix_len;
                ec = Ip4PrefixParse(item->entry.nlri.address, &prefix_addr,
                                    &prefix_len);
                if (ec.value() != 0) {
                    CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                            "Error parsing route address");
                    return;
                }
                AddRoute(vrf_name, prefix_addr, prefix_len, item);
            }
        }
    }
}

void AgentXmppChannel::ReceiveInternal(const XmppStanza::XmppMessage *msg) {
    ReceiveUpdate(msg);
}

std::string AgentXmppChannel::ToString() const {
    return channel_->ToString();
}

void AgentXmppChannel::WriteReadyCb(const boost::system::error_code &ec) {
}

void AgentXmppChannel::BgpPeerDelDone() {
    if (GetBgpPeer()->NoOfWalks() == 0) {
        VNController::Cleanup();
    }
}

void AgentXmppChannel::HandleXmppClientChannelEvent(AgentXmppChannel *peer,
                                                    xmps::PeerState state) {
    if (state == xmps::READY) {
        Agent::GetInstance()->SetAgentXmppChannelSetupTime(UTCTimestampUsec(), 
                                            peer->GetXmppServerIdx());
        // Switch-over Config Control-node
        if (Agent::GetInstance()->GetXmppCfgServer().empty()) {
            if (ControllerSendCfgSubscribe(peer)) {
                Agent::GetInstance()->SetXmppCfgServer(peer->GetXmppServer(), 
                                        peer->GetXmppServerIdx() );
                //Generate a new sequence number for the configuration
                AgentIfMapXmppChannel::NewSeqNumber();
                AgentIfMapVmExport::NotifyAll(peer);
            } 
        }

        // Switch-over Multicast Tree Builder
        AgentXmppChannel *agent_mcast_builder = 
            Agent::GetInstance()->GetControlNodeMulticastBuilder();
        if (agent_mcast_builder == NULL) {
            Agent::GetInstance()->SetControlNodeMulticastBuilder(peer);
        } else if (agent_mcast_builder != peer) {
            boost::system::error_code ec;
            IpAddress ip1 = ip::address::from_string(peer->GetXmppServer(),ec);
            IpAddress ip2 = ip::address::from_string(agent_mcast_builder->GetXmppServer(),ec);
            if (ip1.to_v4().to_ulong() < ip2.to_v4().to_ulong()) {
                // Cleanup sub-nh list and mpls learnt from older peer
                MulticastHandler::HandlePeerDown();
                // Walk route-tables and send dissociate to older peer
                // for subnet and broadcast routes
                agent_mcast_builder->GetBgpPeer()->
                    PeerNotifyMulticastRoutes(false); 
                // Reset Multicast Tree Builder
                Agent::GetInstance()->SetControlNodeMulticastBuilder(peer);
            }
        } else {
            //same peer
            return;
        }

        // Walk route-tables and notify unicast routes
        // and notify subnet and broadcast if TreeBuilder  
        peer->GetBgpPeer()->PeerNotifyRoutes();
        AgentStats::GetInstance()->incr_xmpp_reconnects(peer->GetXmppServerIdx());

        CONTROLLER_TRACE(Session, peer->GetXmppServer(), "READY",
                         Agent::GetInstance()->GetControlNodeMulticastBuilder()->GetBgpPeer()->GetName(),
                         "Peer elected Multicast Tree Builder"); 

    } else {

        //Enqueue cleanup of unicast routes
        peer->GetBgpPeer()->DelPeerRoutes(
            boost::bind(&AgentXmppChannel::BgpPeerDelDone, peer));

        //Enqueue cleanup of multicast routes
        AgentXmppChannel *agent_mcast_builder = 
            Agent::GetInstance()->GetControlNodeMulticastBuilder();
        if (agent_mcast_builder == peer) {
            // Cleanup sub-nh list and mpls learnt from peer
            MulticastHandler::HandlePeerDown();
        }
        
        // Switch-over Config Control-node
        if (Agent::GetInstance()->GetXmppCfgServer().compare(peer->GetXmppServer()) == 0) {
            //send cfg subscribe to other peer if exists
            uint8_t o_idx = ((Agent::GetInstance()->GetXmppCfgServerIdx() == 0) ? 1: 0);
            Agent::GetInstance()->ResetXmppCfgServer();
            AgentXmppChannel *new_cfg_peer = 
                Agent::GetInstance()->GetAgentXmppChannel(o_idx);
            AgentIfMapXmppChannel::NewSeqNumber();
            if (new_cfg_peer && ControllerSendCfgSubscribe(new_cfg_peer)) {
                Agent::GetInstance()->SetXmppCfgServer(new_cfg_peer->GetXmppServer(),
                                        new_cfg_peer->GetXmppServerIdx());
                AgentIfMapVmExport::NotifyAll(new_cfg_peer);
            }  
            //Start a timer
            Agent::GetInstance()->GetIfMapAgentStaleCleaner()->
                    StaleCleanup(AgentIfMapXmppChannel::GetSeqNumber());
        }

        // Switch-over Multicast Tree Builder
        if (agent_mcast_builder == peer) {
            uint8_t o_idx = ((agent_mcast_builder->GetXmppServerIdx() == 0) ? 1: 0);
            AgentXmppChannel *new_mcast_builder = 
                Agent::GetInstance()->GetAgentXmppChannel(o_idx);
            if (new_mcast_builder && 
                new_mcast_builder->GetXmppChannel()->GetPeerState() == xmps::READY) {

                Agent::GetInstance()->SetControlNodeMulticastBuilder(new_mcast_builder);
                //Advertise subnet and all broadcast routes to
                //the new multicast tree builder
                new_mcast_builder->GetBgpPeer()->
                    PeerNotifyMulticastRoutes(true); 

                CONTROLLER_TRACE(Session, peer->GetXmppServer(), "NOT_READY",
                                 Agent::GetInstance()->GetControlNodeMulticastBuilder()->GetBgpPeer()->GetName(),
                                 "Peer elected Multicast Tree Builder"); 

            } else {
                Agent::GetInstance()->SetControlNodeMulticastBuilder(NULL);

                CONTROLLER_TRACE(Session, peer->GetXmppServer(), "NOT_READY", "NULL",
                                 "No elected Multicast Tree Builder"); 
            }

        } 
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
    CONTROLLER_TRACE(Trace, peer->GetBgpPeer()->GetName(), "",
              std::string(reinterpret_cast<const char *>(data_), datalen_));
    // send data
    return (peer->SendUpdate(data_,datalen_));
        
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
    CONTROLLER_TRACE(Trace, peer->GetBgpPeer()->GetName(), "",
            std::string(reinterpret_cast<const char *>(data_), datalen_));
    // send data
    return (peer->SendUpdate(data_,datalen_));
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
    vrf_id << vrf->GetVrfId();
    pugi->AddChildNode("instance-id", vrf_id.str());

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    
    // send data
    return (peer->SendUpdate(data_,datalen_));
}

bool AgentXmppChannel::ControllerSendV4UnicastRoute(AgentXmppChannel *peer,
                                               AgentRoute *route, 
                                               std::string vn, 
                                               const SecurityGroupList *sg_list,
                                               uint32_t mpls_label,
                                               bool add_route) {

    static int id = 0;
    ItemType item;
    uint8_t data_[4096];
    size_t datalen_;
   
    if (!peer) return false;

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    item.entry.nlri.af = BgpAf::IPv4; 
    item.entry.nlri.safi = BgpAf::Unicast; 
    stringstream rstr;
    rstr << route->ToString();
    item.entry.nlri.address = rstr.str();

    string rtr(Agent::GetInstance()->GetRouterId().to_string());

    autogen::NextHopType nh;
    nh.af = BgpAf::IPv4;
    nh.address = rtr;
    nh.label = mpls_label;
    nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
    nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
    item.entry.next_hops.next_hop.push_back(nh);

    if (sg_list && sg_list->size()) {
        item.entry.security_group_list.security_group = *sg_list;
    }

    item.entry.version = 1; //TODO
    item.entry.virtual_network = vn;
   
    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");
    
    pugi->AddAttribute("from", peer->channel_->FromString());
    std::string to(peer->channel_->ToString());
    to += "/";
    to += XmppInit::kBgpPeer; 
    pugi->AddAttribute("to", to);

    stringstream pubsub_id;
    pubsub_id << "pubsub" << id++;
    pugi->AddAttribute("id", pubsub_id.str()); 

    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("publish", "");

    //Catering for inet4 and evpn unicast routes
    stringstream ss_node;
    ss_node << item.entry.nlri.af << "/" 
            << item.entry.nlri.safi << "/" 
            << route->GetVrfEntry()->GetName() << "/" 
            << route->GetAddressString();
    std::string node_id(ss_node.str());
    pugi->AddAttribute("node", node_id);
    pugi->AddChildNode("item", "");

    pugi::xml_node node = pugi->FindNode("item");

    //Call Auto-generated Code to encode the struct
    item.Encode(&node);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    peer->SendUpdate(data_,datalen_);

    pugi->DeleteNode("pubsub");
    pugi->ReadNode("iq");

    stringstream collection_id;
    collection_id << "collection" << id++;
    pugi->ModifyAttribute("id", collection_id.str()); 
    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("collection", "");

    pugi->AddAttribute("node", route->GetVrfEntry()->GetName());
    if (add_route) {
        pugi->AddChildNode("associate", "");
    } else {
        pugi->AddChildNode("dissociate", "");
    }
    pugi->AddAttribute("node", node_id);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    return (peer->SendUpdate(data_,datalen_));
}

bool AgentXmppChannel::ControllerSendEvpnRoute(AgentXmppChannel *peer,
                                               AgentRoute *route, 
                                               std::string vn, 
                                               uint32_t label,
                                               uint32_t tunnel_bmap,
                                               bool add_route) {
    static int id = 0;
    EnetItemType item;
    uint8_t data_[4096];
    size_t datalen_;
   
    if (!peer) return false;

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    //TODO remove hardcoding
    item.entry.nlri.af = 25; 
    item.entry.nlri.safi = 242; 
    stringstream rstr;
    rstr << route->ToString();
    item.entry.nlri.mac = rstr.str();
    Layer2RouteEntry *l2_route = static_cast<Layer2RouteEntry *>(route);
    rstr.str("");
    rstr << l2_route->GetVmIpAddress().to_string() << "/" 
        << l2_route->GetVmIpPlen();
    item.entry.nlri.address = rstr.str();
    assert(item.entry.nlri.address != "0.0.0.0");

    string rtr(Agent::GetInstance()->GetRouterId().to_string());

    autogen::EnetNextHopType nh;
    nh.af = Address::INET;
    nh.address = rtr;
    nh.label = label;

    TunnelType::Type tunnel_type = TunnelType::ComputeType(tunnel_bmap);
    if (l2_route->GetActivePath()) {
        tunnel_type = l2_route->GetActivePath()->tunnel_type();
    }
    if (tunnel_type != TunnelType::VXLAN) {
        nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
        nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
    } else {
        nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("vxlan");
    }

    item.entry.next_hops.next_hop.push_back(nh);

    //item.entry.version = 1; //TODO
    //item.entry.virtual_network = vn;
   
    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");
    
    pugi->AddAttribute("from", peer->channel_->FromString());
    std::string to(peer->channel_->ToString());
    to += "/";
    to += XmppInit::kBgpPeer; 
    pugi->AddAttribute("to", to);

    stringstream pubsub_id;
    pubsub_id << "pubsub" << id++;
    pugi->AddAttribute("id", pubsub_id.str()); 

    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("publish", "");

    stringstream ss_node;
    ss_node << item.entry.nlri.af << "/" << item.entry.nlri.safi << "/" 
        << route->GetAddressString() << "," << item.entry.nlri.address; 
    std::string node_id(ss_node.str());
    pugi->AddAttribute("node", node_id);
    pugi->AddChildNode("item", "");

    pugi::xml_node node = pugi->FindNode("item");

    //Call Auto-generated Code to encode the struct
    item.Encode(&node);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    peer->SendUpdate(data_,datalen_);

    pugi->DeleteNode("pubsub");
    pugi->ReadNode("iq");

    stringstream collection_id;
    collection_id << "collection" << id++;
    pugi->ModifyAttribute("id", collection_id.str()); 
    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("collection", "");

    pugi->AddAttribute("node", route->GetVrfEntry()->GetName());
    if (add_route) {
        pugi->AddChildNode("associate", "");
    } else {
        pugi->AddChildNode("dissociate", "");
    }
    pugi->AddAttribute("node", node_id);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    return (peer->SendUpdate(data_,datalen_));
}

bool AgentXmppChannel::ControllerSendRoute(AgentXmppChannel *peer,
                                           AgentRoute *route, 
                                           std::string vn, 
                                           uint32_t label,
                                           TunnelType::TypeBmap bmap,
                                           const SecurityGroupList *sg_list,
                                           bool add_route,
                                           Agent::RouteTableType type)
{
    bool ret = false;
    if (type == Agent::INET4_UNICAST) {
        ret = AgentXmppChannel::ControllerSendV4UnicastRoute(peer, route, vn,
                                          sg_list, label, add_route);
    } 
    if (type == Agent::LAYER2) {
        ret = AgentXmppChannel::ControllerSendEvpnRoute(peer, route, vn, 
                                         label, bmap, add_route);
    } 
    return ret;
}

bool AgentXmppChannel::ControllerSendMcastRoute(AgentXmppChannel *peer,
                                                AgentRoute *route, 
                                                bool add_route) {

    static int id = 0;
    autogen::McastItemType item;
    uint8_t data_[4096];
    size_t datalen_;
   
    if (!peer) return false;
    if (add_route && (Agent::GetInstance()->GetControlNodeMulticastBuilder() != peer)) {
        CONTROLLER_TRACE(Trace, peer->GetBgpPeer()->GetName(),
                         route->GetVrfEntry()->GetName(),
                         "Peer not elected Multicast Tree Builder");
        return false;
    }

    CONTROLLER_TRACE(McastSubscribe, peer->GetBgpPeer()->GetName(),
                     route->GetVrfEntry()->GetName(), " ",
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
    string rtr(Agent::GetInstance()->GetRouterId().to_string());
    item_nexthop.address = rtr;
    item_nexthop.label = peer->GetMcastLabelRange();
    item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
    item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");

    item.entry.next_hops.next_hop.push_back(item_nexthop);

    //Build the pugi tree
    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");
    pugi->AddAttribute("from", peer->channel_->FromString());
    std::string to(peer->channel_->ToString());
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
            << route->GetVrfEntry()->GetName() << "/" 
            << route->GetAddressString();
    std::string node_id(ss_node.str());
    pugi->AddAttribute("node", node_id);
    pugi->AddChildNode("item", "");

    pugi::xml_node node = pugi->FindNode("item");

    //Call Auto-generated Code to encode the struct
    item.Encode(&node);

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    peer->SendUpdate(data_,datalen_);


    pugi->DeleteNode("pubsub");
    pugi->ReadNode("iq");

    stringstream collection_id;
    collection_id << "collection" << id++;
    pugi->ModifyAttribute("id", collection_id.str()); 
    pugi->AddChildNode("pubsub", "");
    pugi->AddAttribute("xmlns", "http://jabber.org/protocol/pubsub");
    pugi->AddChildNode("collection", "");

    pugi->AddAttribute("node", route->GetVrfEntry()->GetName());
    if (add_route) {
        pugi->AddChildNode("associate", "");
    } else {
        pugi->AddChildNode("dissociate", "");
    }
    pugi->AddAttribute("node", node_id); 

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    return (peer->SendUpdate(data_,datalen_));
}
