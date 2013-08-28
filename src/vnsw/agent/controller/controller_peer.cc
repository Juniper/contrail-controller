/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/util.h>
#include <base/logging.h>
#include <net/address.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include "cmn/agent_cmn.h"
#include "controller/controller_peer.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_vrf_export.h"
#include "controller/controller_init.h"
#include "oper/vrf.h"
#include "oper/inet4_ucroute.h"
#include "oper/nexthop.h"
#include "oper/mirror_table.h"
#include "oper/multicast.h"
#include "oper/peer.h"
#include "oper/vm_path.h"
#include <pugixml/pugixml.hpp>
#include "xml/xml_pugi.h"
#include "bgp_l3vpn_multicast_types.h"
#include "bgp_l3vpn_multicast_msg_types.h"
#include "xmpp/xmpp_init.h"
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
        Agent::GetVrfTable()->Register(boost::bind(&VrfExport::Notify,
                                       this, _1, _2)); 
    bgp_peer_id_ = new BgpPeer(Agent::GetXmppServer(xs_idx_), this, id);
}

AgentXmppChannel::~AgentXmppChannel() {

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(bgp_peer_id_);
    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();

    Agent::GetVrfTable()->Unregister(id);
    delete bgp_peer_id_;
    channel_->UnRegisterReceive(xmps::BGP);
}

bool AgentXmppChannel::SendUpdate(uint8_t *msg, size_t size) {

    if (channel_ && 
        (channel_->GetPeerState() == xmps::READY)) {
        AgentStats::IncrXmppOutMsgs(xs_idx_);
	    return channel_->Send(msg, size, xmps::BGP,
			  boost::bind(&AgentXmppChannel::WriteReadyCb, this, _1));
    } else {
        return false; 
    }
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

    //Call Auto-generated Code to return struct
    auto_ptr<AutogenProperty> xparser(new AutogenProperty());
    if (McastMessageItemsType::XmlParseProperty(node, &xparser) == false) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name, 
                        "Xml Parsing for Multicast Message Failed");
        return;
    }

    McastMessageItemsType *items;
    McastMessageItemType *item;

    items = (static_cast<McastMessageItemsType *>(xparser.get()));
    std::vector<McastMessageItemType>::iterator items_iter;
    boost::system::error_code ec;
    for (items_iter = items->item.begin(); items_iter != items->item.end();  
            items_iter++) {

        item = &*items_iter;

        IpAddress g_addr =
            IpAddress::from_string(item->entry.group, ec);
        if (ec.value() != 0) {
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                             "Error parsing multicast group address");
            return;
        }

        IpAddress s_addr =
            IpAddress::from_string(item->entry.source, ec);
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

            olist.push_back(OlistTunnelEntry(nh.label, addr.to_v4(), 
                                             TunnelType::DefaultTypeBmap()));
        }

        MulticastHandler::ModifyFabricMembers(vrf, g_addr.to_v4(),
                s_addr.to_v4(), item->entry.label,
                olist);
    }
}

static TunnelType::TypeBmap 
GetTypeBitmap(const TunnelEncapsulationListType *encap) {
    TunnelType::TypeBmap bmap = 0;
    for (TunnelEncapsulationListType::const_iterator iter = encap->begin();
         iter != encap->end(); iter++) {
        TunnelEncapType::Encap encap = 
            TunnelEncapType::TunnelEncapFromString(*iter);
        if (encap == TunnelEncapType::MPLS_O_GRE)
            bmap |= (1 << TunnelType::MPLS_GRE);
        if (encap == TunnelEncapType::MPLS_O_UDP)
            bmap |= (1 << TunnelType::MPLS_UDP);
    }
    return bmap;
}

void AgentXmppChannel::AddEcmpRoute(string vrf_name, Ip4Address prefix_addr, 
                                    uint32_t prefix_len, ItemType *item) {
    Inet4UcRouteTable *rt_table = 
        Agent::GetVrfTable()->GetInet4UcRouteTable(vrf_name);

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
        if (Agent::GetRouterId() == addr.to_v4()) {
            //Get local list of interface and append to the list
            MplsLabel *mpls = Agent::GetMplsTable()->FindMplsLabel(label);
            if (mpls != NULL) {
                DBEntryBase::KeyPtr key = mpls->GetNextHop()->GetDBRequestKey();
                NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
                ComponentNHData nh_data(label, nh_key);
                comp_nh_list.push_back(nh_data);
            }
        } else {
            TunnelType::TypeBmap encap = GetTypeBitmap
                (&item->entry.next_hops.next_hop[i].tunnel_encapsulation_list);
            ComponentNHData nh_data(label, Agent::GetDefaultVrf(),
                                    Agent::GetRouterId(), addr.to_v4(), false,
                                    encap);
            comp_nh_list.push_back(nh_data);
        }
    }
    //ECMP create component NH
    rt_table->AddRemoteVmRoute(bgp_peer_id_, vrf_name,
                               prefix_addr, prefix_len, comp_nh_list, -1,
                               item->entry.virtual_network,
                               false);
}

void AgentXmppChannel::AddRemoteRoute(string vrf_name, Ip4Address prefix_addr, 
                                      uint32_t prefix_len, ItemType *item) {
    Inet4UcRouteTable *rt_table = 
        Agent::GetVrfTable()->GetInet4UcRouteTable(vrf_name);

    boost::system::error_code ec; 
    string nexthop_addr = item->entry.next_hops.next_hop[0].address;
    uint32_t label = item->entry.next_hops.next_hop[0].label;
    IpAddress addr = IpAddress::from_string(nexthop_addr, ec);
    TunnelType::TypeBmap encap = GetTypeBitmap
        (&item->entry.next_hops.next_hop[0].tunnel_encapsulation_list);

    if (ec.value() != 0) {
        CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
                         "Error parsing nexthop ip address");
        return;
    }

    CONTROLLER_TRACE(RouteImport, bgp_peer_id_->GetName(), vrf_name, 
                     prefix_addr.to_string(), prefix_len, 
                     addr.to_v4().to_string(), label, 
                     item->entry.virtual_network);

    if (Agent::GetRouterId() != addr.to_v4()) {
        rt_table->AddRemoteVmRoute(bgp_peer_id_, vrf_name,
                                   prefix_addr, prefix_len, addr.to_v4(), 
                                   encap, label, item->entry.virtual_network,  
                                   item->entry.security_group_list.security_group);
        return;
    }

    MplsLabel *mpls = Agent::GetMplsTable()->FindMplsLabel(label);
    if (mpls != NULL) {
        const NextHop *nh = mpls->GetNextHop();
        switch(nh->GetType()) {
        case NextHop::INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            rt_table->AddLocalVmRoute(bgp_peer_id_, vrf_name, prefix_addr, 
                                      prefix_len, intf_nh->GetIfUuid(),
                                      item->entry.virtual_network, label,
                                      item->entry.security_group_list.security_group);
            break;
            }

        case NextHop::VLAN: {
            const VlanNH *vlan_nh = static_cast<const VlanNH *>(nh);
            const VmPortInterface *vm_port =
                static_cast<const VmPortInterface *>(vlan_nh->GetInterface());
            std::vector<int> sg_l;
            vm_port->SgIdList(sg_l);
            rt_table->AddVlanNHRoute(bgp_peer_id_, vrf_name, prefix_addr, 
                                     prefix_len, vlan_nh->GetIfUuid(), 
                                     vlan_nh->GetVlanTag(), label,
                                     item->entry.virtual_network, sg_l);
            break;
            }
        case NextHop:: COMPOSITE: {
            AddEcmpRoute(vrf_name, prefix_addr, prefix_len, item);
            break;
            }

        default:
            CONTROLLER_TRACE(Trace, bgp_peer_id_->GetName(), vrf_name,
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
    
    AgentStats::IncrXmppInMsgs(xs_idx_);
    if (msg && msg->type == XmppStanza::MESSAGE_STANZA) {
      
        XmlBase *impl = msg->dom.get();
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);        
        pugi::xml_node node = pugi->FindNode("items");
        pugi->ReadNode("items"); //sets the context
        std::string vrf_name = pugi->ReadAttrib("node");

        if (vrf_name.find("/") != string::npos) { 
            ReceiveMulticastUpdate(pugi);
            return;
        }

        VrfKey vrf_key(vrf_name);
        VrfEntry *vrf = 
            static_cast<VrfEntry *>(Agent::GetVrfTable ()->FindActiveEntry(&vrf_key));
        if (!vrf) {
            CONTROLLER_TRACE (Trace, bgp_peer_id_->GetName (), vrf_name,
                    "VRF not found");
            return;
        }

        Inet4UcRouteTable *rt_table = vrf->GetInet4UcRouteTable();
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
}

void AgentXmppChannel::HandleXmppClientChannelEvent(AgentXmppChannel *peer,
                                                    xmps::PeerState state) {
    if (state == xmps::READY) {
        Agent::SetAgentXmppChannelSetupTime(UTCTimestampUsec(), 
                                            peer->GetXmppServerIdx());
        // Switch-over Config Control-node
        if (Agent::GetXmppCfgServer().empty()) {
            if (ControllerSendCfgSubscribe(peer)) {
                Agent::SetXmppCfgServer(peer->GetXmppServer(), 
                                        peer->GetXmppServerIdx() );
                //Generate a new sequence number for the configuration
                AgentIfMapXmppChannel::NewSeqNumber();
                AgentIfMapVmExport::NotifyAll(peer);
            } 
        }

        // Switch-over Multicast Tree Builder
        AgentXmppChannel *agent_mcast_builder = 
            Agent::GetControlNodeMulticastBuilder();
        if (agent_mcast_builder == NULL) {
            Agent::SetControlNodeMulticastBuilder(peer);
        } else if (agent_mcast_builder != peer) {
            boost::system::error_code ec;
            IpAddress ip1 = ip::address::from_string(peer->GetXmppServer(),ec);
            IpAddress ip2 = ip::address::from_string(agent_mcast_builder->GetXmppServer(),ec);
            if (ip1.to_v4().to_ulong() < ip2.to_v4().to_ulong()) {
                // Cleanup sub-nh list and mpls learnt from older peer
                MulticastHandler::HandlePeerDown();
                // Walk route-tables and send dissociate to older peer
                // for subnet and broadcast routes
                agent_mcast_builder->GetBgpPeer()->PeerNotifyMcastBcastRoutes(false); 
                // Reset Multicast Tree Builder
                Agent::SetControlNodeMulticastBuilder(peer);
            }
        } else {
            //same peer
            return;
        }

        // Walk route-tables and notify unicast routes
        // and notify subnet and broadcast if TreeBuilder  
        peer->GetBgpPeer()->PeerNotifyRoutes();
        AgentStats::IncrXmppReconnect(peer->GetXmppServerIdx());

        CONTROLLER_TRACE(Session, peer->GetXmppServer(), "READY",
                         Agent::GetControlNodeMulticastBuilder()->GetBgpPeer()->GetName(),
                         "Peer elected Multicast Tree Builder"); 

    } else {

        //Enqueue cleanup of unicast routes
        peer->GetBgpPeer()->DelPeerRoutes(
            boost::bind(&AgentXmppChannel::BgpPeerDelDone, peer));

        //Enqueue cleanup of multicast routes
        AgentXmppChannel *agent_mcast_builder = 
            Agent::GetControlNodeMulticastBuilder();
        if (agent_mcast_builder == peer) {
            // Cleanup sub-nh list and mpls learnt from peer
            MulticastHandler::HandlePeerDown();
        }
        
        // Switch-over Config Control-node
        if (Agent::GetXmppCfgServer().compare(peer->GetXmppServer()) == 0) {
            //send cfg subscribe to other peer if exists
            uint8_t o_idx = ((Agent::GetXmppCfgServerIdx() == 0) ? 1: 0);
            Agent::ResetXmppCfgServer();
            AgentXmppChannel *new_cfg_peer = 
                Agent::GetAgentXmppChannel(o_idx);
            AgentIfMapXmppChannel::NewSeqNumber();
            if (new_cfg_peer && ControllerSendCfgSubscribe(new_cfg_peer)) {
                Agent::SetXmppCfgServer(new_cfg_peer->GetXmppServer(),
                                        new_cfg_peer->GetXmppServerIdx());
                AgentIfMapVmExport::NotifyAll(new_cfg_peer);
            }  
            //Start a timer
            Agent::GetIfMapAgentStaleCleaner()->
                    StaleCleanup(AgentIfMapXmppChannel::GetSeqNumber());
        }

        // Switch-over Multicast Tree Builder
        if (agent_mcast_builder == peer) {
            uint8_t o_idx = ((agent_mcast_builder->GetXmppServerIdx() == 0) ? 1: 0);
            AgentXmppChannel *new_mcast_builder = 
                Agent::GetAgentXmppChannel(o_idx);
            if (new_mcast_builder && 
                new_mcast_builder->GetXmppChannel()->GetPeerState() == xmps::READY) {

                Agent::SetControlNodeMulticastBuilder(new_mcast_builder);
                //Advertise subnet and all broadcast routes to
                //the new multicast tree builder
                new_mcast_builder->GetBgpPeer()->PeerNotifyMcastBcastRoutes(true); 

                CONTROLLER_TRACE(Session, peer->GetXmppServer(), "NOT_READY",
                                 Agent::GetControlNodeMulticastBuilder()->GetBgpPeer()->GetName(),
                                 "Peer elected Multicast Tree Builder"); 

            } else {
                Agent::SetControlNodeMulticastBuilder(NULL);

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

bool AgentXmppChannel::ControllerSendRoute(AgentXmppChannel *peer,
                                           Inet4UcRoute *route, 
                                           std::string vn, 
                                           uint32_t mpls_label,
                                           const SecurityGroupList *sg_list,
                                           bool add_route) {

    static int id = 0;
    ItemType item;
    uint8_t data_[4096];
    size_t datalen_;
   
    if (!peer) return false;

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    item.entry.nlri.af = Address::INET; 
    stringstream rstr;
    rstr << route->GetIpAddress().to_string() << "/" << route->GetPlen();
    item.entry.nlri.address = rstr.str();

    string rtr(Agent::GetRouterId().to_string());

    autogen::NextHopType nh;
    nh.af = Address::INET;
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

    pugi->AddAttribute("node", route->GetIpAddress().to_string());
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
    pugi->AddAttribute("node", route->GetIpAddress().to_string());

    datalen_ = XmppProto::EncodeMessage(impl.get(), data_, sizeof(data_));
    // send data
    return (peer->SendUpdate(data_,datalen_));
}

bool AgentXmppChannel::ControllerSendMcastRoute(AgentXmppChannel *peer,
                                                Inet4Route *route, 
                                                bool add_route) {

    static int id = 0;
    McastItemType item;
    uint8_t data_[4096];
    size_t datalen_;
   
    if (!peer) return false;
    if (add_route && (Agent::GetControlNodeMulticastBuilder() != peer)) {
        CONTROLLER_TRACE(Trace, peer->GetBgpPeer()->GetName(),
                         route->GetVrfEntry()->GetName(),
                         "Peer not elected Multicast Tree Builder");
        return false;
    }

    CONTROLLER_TRACE(McastSubscribe, peer->GetBgpPeer()->GetName(),
                     route->GetVrfEntry()->GetName(), " ",
                     route->GetIpAddress().to_string());

    //Build the DOM tree
    auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

    item.entry.nlri.af = Address::INET; 
    item.entry.nlri.safi = Address::INETMCAST; 
    item.entry.nlri.group = route->GetIpAddress().to_string();
    item.entry.nlri.source = "0.0.0.0";

    item.entry.next_hop.af = Address::INET;
    string rtr(Agent::GetRouterId().to_string());
    item.entry.next_hop.address = rtr;

    item.entry.label = peer->GetMcastLabelRange();

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
    ss_node << item.entry.nlri.af << "/" << item.entry.nlri.safi << "/" 
            << route->GetIpAddress().to_string();
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
