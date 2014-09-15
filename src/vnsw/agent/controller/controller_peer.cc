/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/util.h>
#include <base/logging.h>
#include <base/connection_info.h>
#include <net/bgp_af.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include "cmn/agent_cmn.h"
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
#include "oper/agent_path.h"
#include "pkt/agent_stats.h"
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
 
AgentXmppChannel::AgentXmppChannel(Agent *agent,
                                   const std::string &xmpp_server, 
                                   const std::string &label_range, 
                                   uint8_t xs_idx) 
    : channel_(NULL), xmpp_server_(xmpp_server), label_range_(label_range),
      xs_idx_(xs_idx), agent_(agent), unicast_sequence_number_(0) {
    bgp_peer_id_.reset();
}

AgentXmppChannel::~AgentXmppChannel() {
    channel_->UnRegisterReceive(xmps::BGP);
}

void AgentXmppChannel::RegisterXmppChannel(XmppChannel *channel) {
    if (channel == NULL)
        return;

    channel_ = channel;
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
                                       this, _1, _2)); 
    boost::system::error_code ec;
    const string &addr = agent_->controller_ifmap_xmpp_server(xs_idx_);
    Ip4Address ip = Ip4Address::from_string(addr.c_str(), ec);
    assert(ec.value() == 0);
    bgp_peer_id_.reset(new BgpPeer(ip, addr, this, id));
}

void AgentXmppChannel::DeCommissionBgpPeer() {
    //Unregister to db table is in  destructor of peer. Unregister shud happen
    //after dbstate for the id has happened w.r.t. this peer. If unregiter is 
    //done here, then there is a chance that it is reused and before state
    //is removed it is overwritten. Also it may happen that state delete may be
    //of somebody else.

    // Add the peer to global decommisioned list
    agent_->controller()->AddToDecommissionedPeerList(bgp_peer_id_);
    //Reset channel BGP peer id
    bgp_peer_id_.reset();
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
    Layer2AgentRouteTable *rt_table = 
        static_cast<Layer2AgentRouteTable *>
        (agent_->vrf_table()->GetLayer2RouteTable(vrf_name));

    pugi::xml_node node_check = pugi->FindNode("retract");
    if (!pugi->IsNull(node_check)) {
        for (node = node.first_child(); node; node = node.next_sibling()) {
            if (strcmp(node.name(), "retract") == 0)  {
                std::string id = node.first_attribute().value();
                CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                 "EVPN Delete Node id:" + id);

                char *mac_str = 
                    strtok_r(const_cast<char *>(id.c_str()), "-", &saveptr);
                uint32_t ethernet_tag = 0;
                if (strlen(saveptr) != 0) {
                    ethernet_tag = atoi(mac_str);
                    mac_str = saveptr;
                }
                struct ether_addr mac = *ether_aton(mac_str);;
                if (strcmp("ff:ff:ff:ff:ff:ff", mac_str) == 0) {
                    //Deletes the peer path for all boradcast and 
                    //traverses the subnet route in VRF to issue delete of peer
                    //for them as well.
                    TunnelOlist olist;
                    MulticastHandler::ModifyEvpnMembers(bgp_peer_id(),
                                                        vrf_name, olist,
                                                        ethernet_tag,
                             ControllerPeerPath::kInvalidPeerIdentifier);
                } else {
                    rt_table->DeleteReq(bgp_peer_id(), vrf_name, mac, ethernet_tag,
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
        if (bgp_peer_id() != agent_->mulitcast_builder()->
                             bgp_peer_id()) {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                       "Ignore retract request from non multicast tree "
                       "builder peer; Multicast Delete Node id:" + retract_id);
            return;
        }

        for (node = node.first_child(); node; node = node.next_sibling()) {
            if (strcmp(node.name(), "retract") == 0) { 
                std::string id = node.first_attribute().value();
                CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
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
                MulticastHandler::ModifyFabricMembers(agent_->
                                              multicast_tree_builder_peer(),
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
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
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
            olist.push_back(OlistTunnelEntry(label, addr.to_v4(), encap)); 
        }

        MulticastHandler::ModifyFabricMembers(
                agent_->multicast_tree_builder_peer(),
                vrf, g_addr.to_v4(), s_addr.to_v4(),
                item->entry.nlri.source_label, olist,
                agent_->controller()->multicast_sequence_number());
    }
}

void AgentXmppChannel::AddEcmpRoute(string vrf_name, Ip4Address prefix_addr, 
                                    uint32_t prefix_len, ItemType *item) {
    PathPreference::Preference preference = PathPreference::LOW;
    if (item->entry.local_preference == PathPreference::HIGH) {
        preference = PathPreference::HIGH;
    }
    PathPreference rp(item->entry.sequence_number, preference, false, false);
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->vrf_table()->GetInet4UnicastRouteTable
         (vrf_name));

    ComponentNHKeyList comp_nh_list;
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

        uint32_t label = item->entry.next_hops.next_hop[i].label;
        if (agent_->router_id() == addr.to_v4()) {
            //Get local list of interface and append to the list
            MplsLabel *mpls = 
                agent_->mpls_table()->FindMplsLabel(label);
            if (mpls != NULL) {
                DBEntryBase::KeyPtr key = mpls->nexthop()->GetDBRequestKey();
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
            }
        } else {
            TunnelType::TypeBmap encap = GetTypeBitmap
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
    nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                        comp_nh_list, vrf_name));
    nh_req.data.reset(new CompositeNHData());
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(bgp_peer_id(), prefix_addr, prefix_len,
                                item->entry.virtual_network, -1,
                                false, vrf_name,
                                item->entry.security_group_list.security_group,
                                rp, nh_req);

    //ECMP create component NH
    rt_table->AddRemoteVmRouteReq(bgp_peer_id(), vrf_name,
                                  prefix_addr, prefix_len, data);
}

void AgentXmppChannel::AddMulticastEvpnRoute(string vrf_name,
                                             struct ether_addr &mac,
                                             EnetItemType *item) {
    TunnelOlist olist;
    for (uint32_t i = 0; i < item->entry.olist.next_hop.size(); i++) {
        boost::system::error_code ec;
        IpAddress addr =
            IpAddress::from_string(item->entry.olist.next_hop[i].address,
                                   ec);
        if (ec.value() != 0) {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "Error parsing next-hop address");
            return;
        }

        int label = item->entry.olist.next_hop[i].label;
        TunnelType::TypeBmap encap = GetEnetTypeBitmap(item->
                       entry.olist.next_hop[i].tunnel_encapsulation_list);
        olist.push_back(OlistTunnelEntry(label, addr.to_v4(), encap));
    }

    CONTROLLER_TRACE(Trace, GetBgpPeerName(), "Composite",
                     "add evpn multicast route");
    MulticastHandler::ModifyEvpnMembers(bgp_peer_id(), vrf_name, olist,
                                        item->entry.nlri.ethernet_tag,
                                        agent_->controller()->
                                        multicast_sequence_number());
}

void AgentXmppChannel::AddEvpnRoute(std::string vrf_name,
                                    std::string mac_str,
                                    EnetItemType *item) {
    boost::system::error_code ec; 
    struct ether_addr mac = *ether_aton((mac_str).c_str());

    if (strcmp("ff:ff:ff:ff:ff:ff", mac_str.c_str()) == 0) {
        AddMulticastEvpnRoute(vrf_name, mac, item);
        return;
    }

    string nexthop_addr = item->entry.next_hops.next_hop[0].address;
    uint32_t label = item->entry.next_hops.next_hop[0].label;
    TunnelType::TypeBmap encap = GetEnetTypeBitmap
        (item->entry.next_hops.next_hop[0].tunnel_encapsulation_list);
    IpAddress addr = IpAddress::from_string(nexthop_addr, ec);
    Layer2AgentRouteTable *rt_table = 
        static_cast<Layer2AgentRouteTable *>
        (agent_->vrf_table()->GetLayer2RouteTable(vrf_name));

    if (ec.value() != 0) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Error parsing nexthop ip address");
        return;
    }

    stringstream str;
    str << (ether_ntoa ((struct ether_addr *)&mac)); 
    CONTROLLER_TRACE(RouteImport, GetBgpPeerName(), vrf_name, 
                     str.str(), 0, nexthop_addr, label, "");

    Ip4Address prefix_addr;
    int prefix_len;
    ec = Ip4PrefixParse(item->entry.nlri.address, &prefix_addr,
                        &prefix_len);
    if (ec.value() != 0) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "Error parsing route address");
        return;
    }
    if (agent_->router_id() != addr.to_v4()) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), nexthop_addr,
                         "add remote evpn route");
        // Currently SG not supported for l2 route.
        SecurityGroupList sg;
        ControllerVmRoute *data =
            ControllerVmRoute::MakeControllerVmRoute(bgp_peer_id(),
                                                    agent_->fabric_vrf_name(),
                                                    agent_->router_id(),
                                                    vrf_name, addr.to_v4(),
                                                    encap, label,
                                                    item->entry.virtual_network,
                                                    sg, PathPreference());
        rt_table->AddRemoteVmRouteReq(bgp_peer_id(), vrf_name, mac, prefix_addr,
                                      item->entry.nlri.ethernet_tag,
                                      prefix_len, data);
        return;
    }

    const NextHop *nh = NULL;
    if (encap == (1 << TunnelType::VXLAN)) {
        VrfEntry *vrf = 
            agent_->vrf_table()->FindVrfFromName(vrf_name);
        Layer2RouteKey key(agent_->local_vm_peer(), 
                           vrf_name, mac, item->entry.nlri.ethernet_tag);
        if (vrf != NULL) {
            Layer2RouteEntry *route = 
                static_cast<Layer2RouteEntry *>
                (static_cast<Layer2AgentRouteTable *>
                 (vrf->GetLayer2RouteTable())->FindActiveEntry(&key));
            if (route) {
                nh = route->GetActiveNextHop();
            } else {
                CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                 "route not found, ignoring request");
            }
        } else {
                CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                 "vrf not found, ignoring request");
        }
    } else {
        MplsLabel *mpls = 
            agent_->mpls_table()->FindMplsLabel(label);
        if (mpls != NULL) {
            nh = mpls->nexthop();
        }
    }
    if (nh != NULL) {
        switch(nh->GetType()) {
        case NextHop::INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            ControllerLocalVmRoute *local_vm_route = NULL;
            VmInterfaceKey vm_intf_key(AgentKey::ADD_DEL_CHANGE,
                                    intf_nh->GetIfUuid(), "");
            SecurityGroupList sg_list;
            PathPreference path_preference;
            BgpPeer *bgp_peer = bgp_peer_id();
            if (encap == TunnelType::VxlanType()) {
                local_vm_route =
                    new ControllerLocalVmRoute(vm_intf_key,
                                               MplsTable::kInvalidLabel,
                                               label, false, "",
                                               InterfaceNHFlags::LAYER2,
                                               sg_list, path_preference,
                                               unicast_sequence_number(),
                                               this);
            } else {
                local_vm_route =
                    new ControllerLocalVmRoute(vm_intf_key,
                                               label,
                                               VxLanTable::kInvalidvxlan_id,
                                               false, "",
                                               InterfaceNHFlags::LAYER2,
                                               sg_list, path_preference,
                                               unicast_sequence_number(),
                                               this);
            }
            rt_table->AddLocalVmRouteReq(bgp_peer, vrf_name, mac,
                                         prefix_addr,
                                         item->entry.nlri.ethernet_tag,
                                         prefix_len,
                                         static_cast<LocalVmRoute *>(local_vm_route));
            break;
            }
        default:
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "label points to invalid NH");
        }
    } else {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                         "nexthop not found, ignoring request");
    }
}

void AgentXmppChannel::AddRemoteRoute(string vrf_name, Ip4Address prefix_addr, 
                                      uint32_t prefix_len, ItemType *item) {
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (agent_->vrf_table()->GetInet4UnicastRouteTable
         (vrf_name));

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

    PathPreference::Preference preference = PathPreference::LOW;
    if (item->entry.local_preference == PathPreference::HIGH) {
        preference = PathPreference::HIGH;
    }
    PathPreference path_preference(item->entry.sequence_number, preference,
                                   false, false);
    CONTROLLER_TRACE(RouteImport, GetBgpPeerName(), vrf_name, 
                     prefix_addr.to_string(), prefix_len, 
                     addr.to_v4().to_string(), label, 
                     item->entry.virtual_network);

    if (agent_->router_id() != addr.to_v4()) {
        ControllerVmRoute *data =
            ControllerVmRoute::MakeControllerVmRoute(bgp_peer_id(),
                               agent_->fabric_vrf_name(), agent_->router_id(),
                               vrf_name, addr.to_v4(), encap, label,
                               item->entry.virtual_network ,
                               item->entry.security_group_list.security_group,
                               path_preference);
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
                                    intf_nh->GetIfUuid(), "");
            BgpPeer *bgp_peer = bgp_peer_id();
            if (interface->type() == Interface::VM_INTERFACE) {
                ControllerLocalVmRoute *local_vm_route =
                    new ControllerLocalVmRoute(intf_key, label,
                                               VxLanTable::kInvalidvxlan_id, false,
                                               item->entry.virtual_network,
                                               InterfaceNHFlags::INET4,
                                               item->entry.security_group_list.security_group,
                                               path_preference,
                                               unicast_sequence_number(),
                                               this);
                rt_table->AddLocalVmRouteReq(bgp_peer, vrf_name,
                                             prefix_addr, prefix_len,
                                             static_cast<LocalVmRoute *>(local_vm_route));
            } else if (interface->type() == Interface::INET) {
                InetInterfaceKey intf_key(interface->name());
                ControllerInetInterfaceRoute *inet_interface_route =
                    new ControllerInetInterfaceRoute(intf_key, label,
                                                     TunnelType::GREType(),
                                                     item->entry.virtual_network,
                                                     unicast_sequence_number(),
                                                     this);
                rt_table->AddInetInterfaceRouteReq(bgp_peer, vrf_name,
                                                prefix_addr, prefix_len,
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
            ControllerVlanNhRoute *data =
                new ControllerVlanNhRoute(intf_key, vlan_nh->GetVlanTag(),
                                          label, item->entry.virtual_network,
                                          item->entry.security_group_list.security_group,
                                          path_preference,
                                          unicast_sequence_number(),
                                          this);
            rt_table->AddVlanNHRouteReq(bgp_peer, vrf_name, prefix_addr,
                                        prefix_len, data);
            break;
            }
        case NextHop::COMPOSITE: {
            AddEcmpRoute(vrf_name, prefix_addr, prefix_len, item);
            break;
            }

        default:
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                             "MPLS label points to invalid NH");
        }
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
    
    if (agent_->stats())
        agent_->stats()->incr_xmpp_in_msgs(xs_idx_);
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

        // No BGP peer
        if (bgp_peer_id() == NULL) {
            CONTROLLER_TRACE (Trace, GetBgpPeerName(), vrf_name,
                    "BGP peer not present, agentxmppchannel is inactive");
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

        VrfKey vrf_key(vrf_name);
        VrfEntry *vrf = 
            static_cast<VrfEntry *>(agent_->vrf_table()->
                                    FindActiveEntry(&vrf_key));
        if (!vrf) {
            CONTROLLER_TRACE (Trace, GetBgpPeerName(), vrf_name,
                    "VRF not found");
            return;
        }

        Inet4UnicastAgentRouteTable *rt_table = 
            static_cast<Inet4UnicastAgentRouteTable *>
            (vrf->GetInet4UnicastRouteTable());
        if (!rt_table) {
            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name, 
                             "VRF not found");
            return;
        }

        if (!pugi->IsNull(node)) {
  
            pugi::xml_node node_check = pugi->FindNode("retract");
            if (!pugi->IsNull(node_check)) {
                for (node = node.first_child(); node; node = node.next_sibling()) {
                    if (strcmp(node.name(), "retract") == 0)  {
                        std::string id = node.first_attribute().value();
                        CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                        "Delete Node id:" + id);
                        boost::system::error_code ec;
                        Ip4Address prefix_addr;
                        int prefix_len;
                        ec = Ip4PrefixParse(id, &prefix_addr, &prefix_len);
                        if (ec.value() != 0) {
                            CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
                                    "Error parsing prefix for delete");
                            return;
                        }
                        rt_table->DeleteReq(bgp_peer_id(), vrf_name,
                                        prefix_addr, prefix_len,
                                        new ControllerVmRoute(bgp_peer_id()));
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
                Ip4Address prefix_addr;
                int prefix_len;
                ec = Ip4PrefixParse(item->entry.nlri.address, &prefix_addr,
                                    &prefix_len);
                if (ec.value() != 0) {
                    CONTROLLER_TRACE(Trace, GetBgpPeerName(), vrf_name,
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

void AgentXmppChannel::CleanConfigStale(AgentXmppChannel *agent_xmpp_channel) {
    assert(agent_xmpp_channel);

    //Start a timer to flush off all old configs
    agent_xmpp_channel->agent()->controller()->
        StartConfigCleanupTimer(agent_xmpp_channel); 
}

void AgentXmppChannel::CleanUnicastStale(AgentXmppChannel *agent_xmpp_channel) {
    assert(agent_xmpp_channel);

    // Start Cleanup Timers on stale bgp-peer's
    agent_xmpp_channel->agent()->controller()->
        StartUnicastCleanupTimer(agent_xmpp_channel); 
}

void AgentXmppChannel::CleanMulticastStale(AgentXmppChannel *agent_xmpp_channel) {
    assert(agent_xmpp_channel);

    // Start Cleanup Timers on stale bgp-peers, use current peer identifier
    // for cleanup.
    agent_xmpp_channel->agent()->controller()->
        StartMulticastCleanupTimer(agent_xmpp_channel); 
}

/*
 * Handles both headless and non headless mode
 * all_peer_gone - true indicates that all active peers are gone, false
 * indicates that still one or more active peer is present. Used for headless.
 * In case of non-headless mode delete the peer irrespective of all_peer_gone
 * state. 
 *
 * peer - decommissioned peer xmpp channel
 */
void AgentXmppChannel::UnicastPeerDown(AgentXmppChannel *peer, 
                                       BgpPeer *peer_id) {
    Agent *agent = peer->agent();
    uint32_t active_xmpp_count = agent->controller()->
        ActiveXmppConnectionCount();
    VNController *vn_controller = agent->controller();

    // There may be some DB request queued from this peer.
    // Ignore those as this peer is dead.
    // DB request from the peer is consumed only if peer was alive
    // at the time of request handling.
    peer->increment_unicast_sequence_number();

    // Cancel timer - when second peer comes up at say 4.5 mts and
    // immediately first peer does down then there is a interval of few seconds
    // for second peer to clean up whereas he shud have had 5 mts.
    if (agent->headless_agent_mode()) {
        if (active_xmpp_count == 0) {
            //Enqueue stale marking of unicast v4 & l2 routes
            vn_controller->unicast_cleanup_timer().Cancel();
            // Mark the peer path info as stale and retain it till new active
            // peer comes over. So no deletion of path.
            peer_id->StalePeerRoutes();
            CONTROLLER_TRACE(Trace, peer->GetBgpPeerName(), "None",
                       "No active xmpp, cancel cleanup timer, stale routes");
            return;
        }

        // Number of active peers has come down to 1 and the active peer
        // remaining may not be the one who had started the stale walk.
        // So re-evaluate the stale walk so that new peer can gets its due time 
        // for subscription.
        if (active_xmpp_count == 1) {
            // Dont depend on the xmpp channel sent as function argument;
            // it can be the deleted one and not the current active remaining.
            // So find the active peer.
            AgentXmppChannel *active_xmpp_channel = agent->controller()->
                GetActiveXmppChannel();
            AgentXmppChannel::CleanUnicastStale(active_xmpp_channel);
            CONTROLLER_TRACE(Trace, peer->GetBgpPeerName(), "None",
             "Active xmpp count is one, evaluate reschedule of cleanup timer");
        }

        // Ideally two xmpp channels are supported so assert if we reach here
        // with active_xmpp_count is greater than 1.
        assert(active_xmpp_count <= 1);
    }
    // Dont bother, delete, we are safe
    // These cases result in delete
    // 1) Non headless - blindly delete
    // 2) Headless (active_xmpp present) - Delete peer path
    // Callback provided  for all walk done - this invokes cleanup in case
    // delete of peer is issued because of channel getting disconnected.
    peer_id->DelPeerRoutes(boost::bind(
                           &VNController::ControllerPeerHeadlessAgentDelDone,
                           agent->controller(), peer_id));
    CONTROLLER_TRACE(Trace, peer->GetBgpPeerName(), "None",
                     "Delete peer paths");
}

/*
 * all_peer_gone - true indicates all active peers are gone and false specifies
 * atleast one peer is active. Valid only for headless mode.
 * In non headless mode always remove the peer info.
 */
void AgentXmppChannel::MulticastPeerDown(AgentXmppChannel *old_mcast_builder,
                                         AgentXmppChannel *new_mcast_builder) {
    Agent *agent = old_mcast_builder->agent();
    if (old_mcast_builder && agent->headless_agent_mode()) { 
        if (new_mcast_builder == NULL) {
            VNController *vn_controller = agent->controller();
            vn_controller->multicast_cleanup_timer().Cancel();
            CONTROLLER_TRACE(Trace, old_mcast_builder->GetBgpPeerName(), "None",
                             "No mcast builder, cancel cleanup timer");
        } else {
            //Peer going down has resulted in switch over of peer.
            //In case stale cleanup timer is active reschedule it so that new 
            //peer can have its quota of stale timeout.
            AgentXmppChannel::CleanMulticastStale(new_mcast_builder);
            CONTROLLER_TRACE(Trace, old_mcast_builder->GetBgpPeerName(), "None",
                             "evaluate reschedule of mcast cleanup timer");
        }
        return;
    }

    //Start multicast timer for cleanup, though peer has nothing to do 
    //w.r.t. multicast, its sent for syntax sake.
    AgentXmppChannel::CleanMulticastStale(old_mcast_builder);
}

/*
 * AgentXmppChannel is active when:
 * 1) bgp peer is not null(bgp_peer_id)
 * 2) xmpp channel is in READY state
 * 3) Valid XMPP channel
 */
bool AgentXmppChannel::IsBgpPeerActive(AgentXmppChannel *peer) {
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
        agent->set_ifmap_active_xmpp_server(peer->controller_ifmap_xmpp_server(), 
                                peer->GetXmppServerIdx());
        //Generate a new sequence number for the configuration
        AgentIfMapXmppChannel::NewSeqNumber();
        agent->controller()->agent_ifmap_vm_export()->NotifyAll(peer);
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

/*
 * Xmpp Channel event handler- handled events are READY and NOT_READY
 *
 * READY State
 *
 * Headless Mode
 * If bgp_peer_id is already set ignore the notification. READY is only
 * processed when bgp_peer_id is not present.
 * - Sets this peer as config server if no config peer was configured, start
 *   cleanup timer for flushing stales(based on sequence number) as there is 
 *   active peer now.
 * - Start walk to notify all unicast routes to new peer. Along with this 
 *   start unicast cleanup timer to flush stale paths.
 * - Elect multicast builder if there was no mcast builder configured or the new
 *   peer has lower IP than that of mcast builder. Start mcast cleanup timer
 *   based on multicast sequence number.
 *
 * Non Headless mode
 * Everything remains as in headless mode except for start of timers.
 *
 *
 * NON_READY State 
 *
 * Headless
 * Ignore non ready state if there is no bgp peer attached. Its duplicate.
 * Move the BGP peer to decommissioned list. This list is flushed off when
 * unicast stale timer expires. 
 * - Active XMPP connection goes down to one i.e. last active xmpp peer 
 *   
 *   For config and unicast, reevaluate cleanup timer to see
 *   if it is running and was started by the peer which is going down. On
 *   finding that peer going down is the timer starter try rescheduling it so
 *   that last remaing peer gets its due time for updates.
 *   
 *   For Multicast if bulder is same as peer going down then switch over and 
 *   send subscription. Also reevaluate the cleanup timer in case of switch
 *   over. Timer is rescheduled.
 * - Active XMPP connection goes down to zero
 *
 *   For all(config, unicast, multicast) cancel the cleanup timer so that stale
 *   paths are not lost.
 *
 * - Active XMPP connection > 0
 *   
 *   For Unicast delete peer path. 
 *   For config and multicast logic is same as in active XMPP connection = 1
 *
 * Non Headless mode
 * Processing of Unicast deletes path for peer unconditionally.
 * Multicast remains same as in headless except for timer. Timer is started to
 * clean stale entries. New peer may not have sent some routes whose fabric 
 * data will then have to be flushed on timer expiration.
 * Config remains same except for timer which gets trigeered to clean stale if
 * no peer comes up within specified time.
 */ 

void AgentXmppChannel::HandleAgentXmppClientChannelEvent(AgentXmppChannel *peer,
                                                         xmps::PeerState state) {
    Agent *agent = peer->agent();
    peer->UpdateConnectionInfo(state);
    bool change_agent_mcast_builder = false;
    bool headless_mode = agent->headless_agent_mode();

    if (state == xmps::READY) {

        //Ignore duplicate ready messages, active peer present
        if (peer->bgp_peer_id() != NULL)
            return;

        // Create a new BgpPeer channel is UP from DOWN state
        peer->CreateBgpPeer();
        agent->set_controller_xmpp_channel_setup_time(UTCTimestampUsec(), peer->
                                            GetXmppServerIdx());
        CONTROLLER_TRACE(Session, peer->controller_ifmap_xmpp_server(), "READY", 
                         "NULL", "BGP peer ready."); 
        if ((agent->controller()->ActiveXmppConnectionCount() == 1) &&
            headless_mode) {
            CleanUnicastStale(peer);
        }

        // Switch-over Config Control-node
        if (agent->ifmap_active_xmpp_server().empty()) {
            AgentXmppChannel::SetConfigPeer(peer);
            if (headless_mode) {
                CleanConfigStale(peer);
            }
            CONTROLLER_TRACE(Session, peer->controller_ifmap_xmpp_server(), "READY", 
                             "NULL", "BGP peer set as config server."); 
        }

        // Evaluate switching over Multicast Tree Builder
        AgentXmppChannel *agent_mcast_builder = 
            agent->mulitcast_builder();
        //Either mcast builder is being set for first time or a lower peer has
        //come up. In both cases its time to set new mcast peer as builder.
        change_agent_mcast_builder = agent_mcast_builder ? false : true;

        if (agent_mcast_builder && (agent_mcast_builder != peer)) {
            // Check whether new peer can be a potential mcast builder
            boost::system::error_code ec;
            IpAddress ip1 = ip::address::from_string(peer->controller_ifmap_xmpp_server(),ec);
            IpAddress ip2 = ip::address::from_string(agent_mcast_builder->
                                                     controller_ifmap_xmpp_server(),ec);
            if (ip1.to_v4().to_ulong() < ip2.to_v4().to_ulong()) {
                change_agent_mcast_builder = true;
                // Walk route-tables and send dissociate to older peer
                // for subnet and broadcast routes
                agent_mcast_builder->bgp_peer_id()->
                    PeerNotifyMulticastRoutes(false); 
            } 
        }

        if (change_agent_mcast_builder) {
            //Since this is first time mcast peer so old and new peer are same
            AgentXmppChannel::SetMulticastPeer(peer, peer);
            CleanMulticastStale(peer);
            CONTROLLER_TRACE(Session, peer->controller_ifmap_xmpp_server(), "READY", 
                             agent->mulitcast_builder()->
                             GetBgpPeerName(), "Peer elected Mcast builder"); 
        }

        // Walk route-tables and notify unicast routes
        // and notify subnet and broadcast if TreeBuilder
        //TODO this will send notification for mcast routes even though
        //peer was not selected as mcast_builder. The notification gets dropped
        //when message is dropped at ControllerSendMulticastRoute by checking
        //peer against mcast builder. This can be refined though.
        peer->bgp_peer_id()->PeerNotifyRoutes();

        //Cleanup stales if any
        // If its headless agent mode clean stale for config and unicast 
        // unconditionally, multicast cleanup is not required if change in mcast
        // builder is not present.
        // In case of non headless mode the cleanup shud have happened when all
        // channels were down for config and unicast so nothing to do. Multicast
        // handling remains same as of headless.

        if (agent->stats())
            agent->stats()->incr_xmpp_reconnects(peer->GetXmppServerIdx());
    } else {
        //Ignore duplicate not-ready messages
        if (peer->bgp_peer_id() == NULL)
            return;

        BgpPeer *decommissioned_peer_id = peer->bgp_peer_id();
        // Add BgpPeer to global decommissioned list
        peer->DeCommissionBgpPeer();

        CONTROLLER_TRACE(Session, peer->controller_ifmap_xmpp_server(), "NOT_READY", 
                         "NULL", "BGP peer decommissioned for xmpp channel."); 

        // Remove all unicast peer paths(in non headless mode) and cancel stale
        // timer in headless
        AgentXmppChannel::UnicastPeerDown(peer, decommissioned_peer_id);

        // evaluate peer change for config and multicast
        AgentXmppChannel *agent_mcast_builder = 
            agent->mulitcast_builder();
        bool peer_is_config_server = (agent->ifmap_active_xmpp_server().compare(peer->
                                             controller_ifmap_xmpp_server()) == 0);
        bool peer_is_agent_mcast_builder = (agent_mcast_builder == peer);

        // Switch-over Config Control-node
        if (peer_is_config_server) {
            //send cfg subscribe to other peer if exists
            uint8_t idx = ((agent->ifmap_active_xmpp_server_index() == 0) ? 1: 0);
            agent->reset_ifmap_active_xmpp_server();
            AgentXmppChannel *new_cfg_peer = agent->controller_xmpp_channel(idx);

            if (IsBgpPeerActive(new_cfg_peer) && 
                AgentXmppChannel::SetConfigPeer(new_cfg_peer)) {
                AgentXmppChannel::CleanConfigStale(new_cfg_peer);
                CONTROLLER_TRACE(Session, new_cfg_peer->controller_ifmap_xmpp_server(), 
                                 "NOT_READY", "NULL", "BGP peer selected as" 
                                 "config peer on decommission of old config "
                                 "peer."); 

            } else {
                //All cfg peers are gone, in headless agent cancel cleanup
                //timer, retain old config
                if (headless_mode)
                    agent->controller()->config_cleanup_timer().Cancel();
            }

            //Start a timer to flush off all old configs, in non headless mode
            if (!headless_mode) {
                // For old config peer increment sequence number and remove
                // entries
                AgentIfMapXmppChannel::NewSeqNumber();
                AgentXmppChannel::CleanConfigStale(peer);
            }
        }

        // Switch-over Multicast Tree Builder
        if (peer_is_agent_mcast_builder) {
            uint8_t idx = ((agent_mcast_builder->GetXmppServerIdx() == 0) 
                             ? 1: 0);
            AgentXmppChannel *new_mcast_builder = 
                agent->controller_xmpp_channel(idx);

            // Selection of new peer as mcast builder is dependant on following
            // criterias:
            // 1) Channel is present (new_mcast_builder is not null)
            // 2) Channel is in READY state
            // 3) BGP peer is commissioned for channel
            bool evaluate_new_mcast_builder = 
                IsBgpPeerActive(new_mcast_builder); 

            if (!evaluate_new_mcast_builder) {
                new_mcast_builder = NULL;
                CONTROLLER_TRACE(Session, peer->controller_ifmap_xmpp_server(), "NOT_READY", 
                                 "NULL", "No elected Multicast Tree Builder"); 
            }
            AgentXmppChannel::SetMulticastPeer(peer, new_mcast_builder);

            //Bring down old peer, new_mcast_builder NULL means all possible
            //builder are gone.
            AgentXmppChannel::MulticastPeerDown(peer, new_mcast_builder);

            if (evaluate_new_mcast_builder) {
                //Advertise subnet and all broadcast routes to
                //the new multicast tree builder
                new_mcast_builder->bgp_peer_id()->
                    PeerNotifyMulticastRoutes(true); 
                CONTROLLER_TRACE(Session, peer->controller_ifmap_xmpp_server(), "NOT_READY",
                                 agent->mulitcast_builder()->
                                 GetBgpPeerName(), 
                                 "Peer elected Multicast Tree Builder"); 
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
    CONTROLLER_TRACE(Trace, peer->GetBgpPeerName(), "",
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
    CONTROLLER_TRACE(Trace, peer->GetBgpPeerName(), "",
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
    CONTROLLER_TRACE(Trace, peer->GetBgpPeerName(), vrf->GetName(), 
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
    vrf_id << vrf->vrf_id();
    pugi->AddChildNode("instance-id", vrf_id.str());

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    
    // send data
    return (peer->SendUpdate(data_,datalen_));
}

bool AgentXmppChannel::ControllerSendV4UnicastRouteCommon(AgentRoute *route,
                                       std::string vn,
                                       const SecurityGroupList *sg_list,
                                       uint32_t mpls_label,
                                       TunnelType::TypeBmap bmap,
                                       const PathPreference &path_preference,
                                       bool associate) {

    static int id = 0;
    ItemType item;
    uint8_t data_[4096];
    size_t datalen_;
   
    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    item.entry.nlri.af = BgpAf::IPv4; 
    item.entry.nlri.safi = BgpAf::Unicast; 
    stringstream rstr;
    rstr << route->ToString();
    item.entry.nlri.address = rstr.str();

    string rtr(agent_->router_id().to_string());

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
    item.entry.next_hops.next_hop.push_back(nh);

    if (sg_list && sg_list->size()) {
        item.entry.security_group_list.security_group = *sg_list;
    }

    item.entry.version = 1; //TODO
    item.entry.virtual_network = vn;

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
    pubsub_id << "pubsub" << id++;
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
    return (SendUpdate(data_,datalen_));
}

bool AgentXmppChannel::ControllerSendEvpnRouteCommon(AgentRoute *route,
                                                     std::string vn,
                                                     uint32_t label,
                                                     uint32_t tunnel_bmap,
                                                     bool associate) {
    static int id = 0;
    EnetItemType item;
    uint8_t data_[4096];
    size_t datalen_;

    if (label == MplsTable::kInvalidLabel) return false;

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    if (route->is_multicast() && agent_->simulate_evpn_tor()) {
        item.entry.edge_replication_not_supported = true;
    } else {
        item.entry.edge_replication_not_supported = false;
    }
    item.entry.nlri.af = BgpAf::L2Vpn; 
    item.entry.nlri.safi = BgpAf::Enet; 
    stringstream rstr;
    rstr << route->ToString();
    item.entry.nlri.mac = rstr.str();
    Layer2RouteEntry *l2_route = static_cast<Layer2RouteEntry *>(route);
    rstr.str("");
    rstr << l2_route->GetVmIpAddress().to_string() << "/" 
        << l2_route->GetVmIpPlen();
    item.entry.nlri.address = rstr.str();
    assert(item.entry.nlri.address != "0.0.0.0");

    string rtr(agent_->router_id().to_string());

    autogen::EnetNextHopType nh;
    nh.af = Address::INET;
    nh.address = rtr;
    nh.label = label;

    item.entry.nlri.ethernet_tag = 0;
    TunnelType::Type tunnel_type = TunnelType::ComputeType(tunnel_bmap);
    const AgentPath *active_path = NULL;
    if (l2_route->is_multicast()) {
        active_path = l2_route->FindPath(agent_->multicast_peer());
    } else {
        active_path = l2_route->FindLocalVmPortPath();
    }

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
                item.entry.nlri.ethernet_tag = nh.label;
            } else {
                nh.label = 0;
            }
            nh.tunnel_encapsulation_list.tunnel_encapsulation.push_back("vxlan");
        }
    } else {
        if (tunnel_type != TunnelType::VXLAN) {
            item.entry.nlri.ethernet_tag = 0;
        } else {
            item.entry.nlri.ethernet_tag = label;
        }
    }

    item.entry.next_hops.next_hop.push_back(nh);
    //item.entry.version = 1; //TODO
    //item.entry.virtual_network = vn;
   
    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", "set");
    
    pugi->AddAttribute("from", channel_->FromString());
    std::string to(channel_->ToString());
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
        << route->ToString() << "," << item.entry.nlri.address;
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
    return (SendUpdate(data_,datalen_));
}

bool AgentXmppChannel::ControllerSendMcastRouteCommon(AgentRoute *route,
                                                      bool add_route) {

    static int id = 0;
    autogen::McastItemType item;
    uint8_t data_[4096];
    size_t datalen_;
   
    if (add_route && (agent_->mulitcast_builder() != this)) {
        CONTROLLER_TRACE(Trace, GetBgpPeerName(),
                         route->vrf()->GetName(),
                         "Peer not elected Multicast Tree Builder");
        return false;
    }

    CONTROLLER_TRACE(McastSubscribe, GetBgpPeerName(),
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
    if (add_route) {
        pugi->AddChildNode("associate", "");
    } else {
        pugi->AddChildNode("dissociate", "");
    }
    pugi->AddAttribute("node", node_id); 

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    return (SendUpdate(data_,datalen_));
}

bool AgentXmppChannel::ControllerSendEvpnRouteAdd(AgentXmppChannel *peer,
                                                  AgentRoute *route,
                                                  std::string vn,
                                                  uint32_t label,
                                                  uint32_t tunnel_bmap) {
    if (!peer) return false;

    CONTROLLER_TRACE(RouteExport, peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(), true, label);
    return (peer->ControllerSendEvpnRouteCommon(route,
                                                vn,
                                                label,
                                                tunnel_bmap,
                                                true));
}

bool AgentXmppChannel::ControllerSendEvpnRouteDelete(AgentXmppChannel *peer,
                                                     AgentRoute *route,
                                                     std::string vn,
                                                     uint32_t label,
                                                     uint32_t tunnel_bmap) {
    if (!peer) return false;

    CONTROLLER_TRACE(RouteExport, peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(), false, label);
    return (peer->ControllerSendEvpnRouteCommon(route,
                                                vn,
                                                label,
                                                tunnel_bmap,
                                                false));
}

bool AgentXmppChannel::ControllerSendRouteAdd(AgentXmppChannel *peer,
                                              AgentRoute *route,
                                              std::string vn,
                                              uint32_t label,
                                              TunnelType::TypeBmap bmap,
                                              const SecurityGroupList *sg_list,
                                              Agent::RouteTableType type,
                                              const PathPreference
                                              &path_preference)
{
    if (!peer) return false;

    CONTROLLER_TRACE(RouteExport,
                     peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(),
                     true, label);
    bool ret = false;
    if ((type == Agent::INET4_UNICAST) &&
        (peer->agent()->simulate_evpn_tor() == false)) {
        ret = peer->ControllerSendV4UnicastRouteCommon(route, vn,
                                                       sg_list, label, bmap,
                                                       path_preference, true);
    }
    if (type == Agent::LAYER2) {
        ret = peer->ControllerSendEvpnRouteCommon(route, vn,
                                                  label, bmap, true);
    }
    return ret;
}

bool AgentXmppChannel::ControllerSendRouteDelete(AgentXmppChannel *peer,
                                          AgentRoute *route,
                                          std::string vn,
                                          uint32_t label,
                                          TunnelType::TypeBmap bmap,
                                          const SecurityGroupList *sg_list,
                                          Agent::RouteTableType type,
                                          const PathPreference
                                          &path_preference)
{
    if (!peer) return false;

    CONTROLLER_TRACE(RouteExport,
                     peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(),
                     false, 0);
    bool ret = false;
    if ((type == Agent::INET4_UNICAST) &&
        (peer->agent()->simulate_evpn_tor() == false)) {
        ret = peer->ControllerSendV4UnicastRouteCommon(route, vn,
                                                       sg_list, label,
                                                       bmap,
                                                       path_preference,
                                                       false);
    }
    if (type == Agent::LAYER2) {
        ret = peer->ControllerSendEvpnRouteCommon(route, vn,
                                                  label, bmap, false);
    }
    return ret;
}

bool AgentXmppChannel::ControllerSendMcastRouteAdd(AgentXmppChannel *peer,
                                                   AgentRoute *route) {
    if (!peer) return false;

    CONTROLLER_TRACE(RouteExport, peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(), true, 0);
    return peer->ControllerSendMcastRouteCommon(route, true);
}

bool AgentXmppChannel::ControllerSendMcastRouteDelete(AgentXmppChannel *peer,
                                                      AgentRoute *route) {
    if (!peer) return false;

    CONTROLLER_TRACE(RouteExport, peer->GetBgpPeerName(),
                     route->vrf()->GetName(),
                     route->ToString(), false, 0);

    return peer->ControllerSendMcastRouteCommon(route, false);
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
