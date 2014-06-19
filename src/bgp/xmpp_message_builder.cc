/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/xmpp_message_builder.h"

#include <boost/foreach.hpp>
#include <pugixml/pugixml.hpp>

#include "base/parse_object.h"
#include "base/logging.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/bgp_table.h"
#include "bgp/enet/enet_route.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/ermvpn/ermvpn_route.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/security_group/security_group.h"
#include "net/bgp_af.h"
#include "schema/xmpp_unicast_types.h"
#include "schema/xmpp_multicast_types.h"
#include "schema/xmpp_enet_types.h"
#include "xmpp/xmpp_init.h"

using namespace pugi;
using namespace std;

class BgpXmppMessage : public Message {
public:
    BgpXmppMessage(const BgpTable *table, const RibOutAttr *roattr)
        : table_(table),
          is_reachable_(roattr->IsReachable()),
          sequence_number_(0),
          virtual_network_("unresolved") {
    }
    virtual ~BgpXmppMessage() { }
    void Start(const RibOutAttr *roattr, const BgpRoute *route);
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr *roattr);
    virtual void Finish() { }
    virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp);

private:
    void EncodeNextHop(const BgpRoute *route, RibOutAttr::NextHop nexthop,
                       autogen::ItemType &item);
    void AddInetReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddInetUnreach(const BgpRoute *route);
    bool AddInetRoute(const BgpRoute *route, const RibOutAttr *roattr);

    void EncodeEnetNextHop(const BgpRoute *route, RibOutAttr::NextHop nexthop,
                           autogen::EnetItemType &item);
    void AddEnetReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddEnetUnreach(const BgpRoute *route);
    bool AddEnetRoute(const BgpRoute *route, const RibOutAttr *roattr);

    void AddMcastReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddMcastUnreach(const BgpRoute *route);
    bool AddMcastRoute(const BgpRoute *route, const RibOutAttr *roattr);

    void ProcessExtCommunity(const ExtCommunity *ext_community) {
        if (ext_community == NULL)
            return;

        for (ExtCommunity::ExtCommunityList::const_iterator iter =
             ext_community->communities().begin();
             iter != ext_community->communities().end(); ++iter) {
            if (ExtCommunity::is_security_group(*iter)) {
                SecurityGroup security_group(*iter);
                security_group_list_.push_back(security_group.security_group_id());
            }
            if (ExtCommunity::is_mac_mobility(*iter)) {
                MacMobility mm(*iter);
                sequence_number_ = mm.sequence_number();
            }
            if (ExtCommunity::is_origin_vn(*iter)) {
                OriginVn origin_vn(*iter);
                const RoutingInstanceMgr *manager =
                    table_->routing_instance()->manager();
                virtual_network_ =
                    manager->GetVirtualNetworkByVnIndex(origin_vn.vn_index());
            }
        }
    }

    const BgpTable *table_;
    bool is_reachable_;
    xml_document xdoc_;
    xml_node xitems_;
    uint32_t sequence_number_;
    std::string virtual_network_;
    std::vector<int> security_group_list_;
    string repr_;
    string repr_new_;
    size_t repr_part1_;
    size_t repr_part2_;
    DISALLOW_COPY_AND_ASSIGN(BgpXmppMessage);
};

void BgpXmppMessage::Start(const RibOutAttr *roattr, const BgpRoute *route) {
    // Build the DOM tree
    xml_node message = xdoc_.append_child("message");
    message.append_attribute("from") = XmppInit::kControlNodeJID;

    xml_node event = message.append_child("event");
    event.append_attribute("xmlns") = "http://jabber.org/protocol/pubsub";
    xitems_ = event.append_child("items");

    if (is_reachable_) {
        const BgpAttr *attr = roattr->attr();
        ProcessExtCommunity(attr->ext_community());
    }

    stringstream ss;
    ss << route->Afi() << "/" << int(route->XmppSafi()) << "/" <<
          table_->routing_instance()->name();
    std::string node(ss.str());
    if (table_->family() == Address::ERMVPN) {
        xitems_.append_attribute("node") = node.c_str();
        AddMcastRoute(route, roattr);
    } else if (table_->family() == Address::ENET) {
        xitems_.append_attribute("node") = node.c_str();
        AddEnetRoute(route, roattr);
    } else {
        xitems_.append_attribute("node") = node.c_str();
        AddInetRoute(route, roattr);
    }
}

bool BgpXmppMessage::AddRoute(const BgpRoute *route, const RibOutAttr *roattr) {
    if (table_->family() == Address::ERMVPN) {
        return AddMcastRoute(route, roattr);
    } else if (table_->family() == Address::ENET) {
        return AddEnetRoute(route, roattr);
    } else {
        return AddInetRoute(route, roattr);
    }
}

void BgpXmppMessage::EncodeNextHop(const BgpRoute *route,
                                   RibOutAttr::NextHop nexthop,
                                   autogen::ItemType &item) {
    autogen::NextHopType item_nexthop;

    item_nexthop.af = route->Afi();
    item_nexthop.address = nexthop.address().to_v4().to_string();
    item_nexthop.label = nexthop.label();
    if (nexthop.encap().empty()) {
        // If encap list is empty, routes from non-control-node, 
        // use mpls over gre as default encap
        item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back(std::string("gre"));
    } else {
        item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation= nexthop.encap();
    }

    item.entry.next_hops.next_hop.push_back(item_nexthop);
}

void BgpXmppMessage::AddInetReach(const BgpRoute *route, const RibOutAttr *roattr) {
    autogen::ItemType item;

    item.entry.nlri.af = route->Afi();
    item.entry.nlri.safi = route->XmppSafi();
    item.entry.nlri.address = route->ToString();
    item.entry.version = 1;
    item.entry.virtual_network = virtual_network_;
    item.entry.local_preference = roattr->attr()->local_pref();
    item.entry.sequence_number = sequence_number_;

    assert(!roattr->nexthop_list().empty());

    //
    // Encode all next-hops in the list
    //
    BOOST_FOREACH(RibOutAttr::NextHop nexthop, roattr->nexthop_list()) {
        EncodeNextHop(route, nexthop, item);
    }

    for (std::vector<int>::iterator it = security_group_list_.begin(); 
         it !=  security_group_list_.end(); it++) {
        item.entry.security_group_list.security_group.push_back(*it);
    }

    xml_node node = xitems_.append_child("item");
    node.append_attribute("id") = route->ToXmppIdString().c_str();
    item.Encode(&node);
}

void BgpXmppMessage::AddInetUnreach(const BgpRoute *route) {
    xml_node node = xitems_.append_child("retract");
    node.append_attribute("id") = route->ToXmppIdString().c_str();
}

bool BgpXmppMessage::AddInetRoute(const BgpRoute *route, const RibOutAttr *roattr) {
    if (is_reachable_) {
        num_reach_route_++;
        AddInetReach(route, roattr);
    } else {
        num_unreach_route_++;
        AddInetUnreach(route);
    }
    return true;
}

void BgpXmppMessage::EncodeEnetNextHop(const BgpRoute *route,
                                       RibOutAttr::NextHop nexthop,
                                       autogen::EnetItemType &item) {
    autogen::EnetNextHopType item_nexthop;

    item_nexthop.af = BgpAf::IPv4;
    item_nexthop.address = nexthop.address().to_v4().to_string();
    item_nexthop.label = nexthop.label();
    if (nexthop.encap().empty()) {
        // If encap list is empty, routes from non-control-node, 
        // use mpls over gre as default encap
        item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back(std::string("gre"));
    } else {
        item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation= nexthop.encap();
    }
    item.entry.next_hops.next_hop.push_back(item_nexthop);
}

void BgpXmppMessage::AddEnetReach(const BgpRoute *route, const RibOutAttr *roattr) {

    autogen::EnetItemType item;
    item.entry.nlri.af = route->Afi();
    item.entry.nlri.safi = route->XmppSafi();

    EnetRoute *enet_route =
        static_cast<EnetRoute *>(const_cast<BgpRoute *>(route));
    item.entry.nlri.mac = enet_route->GetPrefix().mac_addr().ToString();
    item.entry.nlri.address =  enet_route->GetPrefix().ip_prefix().ToString();
    item.entry.virtual_network = virtual_network_;

    assert(!roattr->nexthop_list().empty());
    BOOST_FOREACH(RibOutAttr::NextHop nexthop, roattr->nexthop_list()) {
        EncodeEnetNextHop(route, nexthop, item);
    }

    xml_node node = xitems_.append_child("item");
    node.append_attribute("id") = route->ToXmppIdString().c_str();
    item.Encode(&node);
}

void BgpXmppMessage::AddEnetUnreach(const BgpRoute *route) {
    xml_node node = xitems_.append_child("retract");
    node.append_attribute("id") = route->ToXmppIdString().c_str();
}

bool BgpXmppMessage::AddEnetRoute(const BgpRoute *route, const RibOutAttr *roattr) {
    if (is_reachable_) {
        num_reach_route_++;
        AddEnetReach(route, roattr);
    } else {
        num_unreach_route_++;
        AddEnetUnreach(route);
    }
    return true;
}

void BgpXmppMessage::AddMcastReach(const BgpRoute *route, const RibOutAttr *roattr) {

    autogen::McastItemType item;
    item.entry.nlri.af = route->Afi();
    item.entry.nlri.safi = route->XmppSafi();

    ErmVpnRoute *ermvpn_route =
        static_cast<ErmVpnRoute *>(const_cast<BgpRoute *>(route));
    item.entry.nlri.group = ermvpn_route->GetPrefix().group().to_string();
    item.entry.nlri.source =  ermvpn_route->GetPrefix().source().to_string();
    item.entry.nlri.source_label = roattr->label();

    BgpOList *olist = roattr->attr()->olist().get();
    std::vector<BgpOListElem>::const_iterator iterator;
    for (iterator = olist->elements.begin();
         iterator != olist->elements.end(); ++iterator) {
        BgpOListElem elem = static_cast<BgpOListElem>(*iterator);

        autogen::McastNextHopType nh;
        nh.af = BgpAf::IPv4;
        nh.address = elem.address.to_string();
        stringstream label;
        label << elem.label;
        nh.label = label.str();
        nh.tunnel_encapsulation_list.tunnel_encapsulation = elem.encap;
        item.entry.olist.next_hop.push_back(nh);
    }

    xml_node node = xitems_.append_child("item");
    node.append_attribute("id") = route->ToXmppIdString().c_str();
    item.Encode(&node);
}

void BgpXmppMessage::AddMcastUnreach(const BgpRoute *route) {
    xml_node node = xitems_.append_child("retract");
    node.append_attribute("id") = route->ToXmppIdString().c_str();
}

bool BgpXmppMessage::AddMcastRoute(const BgpRoute *route, const RibOutAttr *roattr) {
    if (is_reachable_) {
        num_reach_route_++;
        AddMcastReach(route, roattr);
    } else {
        num_unreach_route_++;
        AddMcastUnreach(route);
    }
    return true;
}

const uint8_t *BgpXmppMessage::GetData(IPeerUpdate *peer, size_t *lenp) {
    std::string str = peer->ToString() + "/" + XmppInit::kBgpPeer;

    // If the message has already been constructed, just replace the 'to' part.
    if (!repr_.empty()) {
        repr_new_ = string(repr_, 0, repr_part1_) + "to=\"" + str + "\">" +
                    string(repr_, repr_part2_);

        *lenp = repr_new_.size();
        return reinterpret_cast<const uint8_t *>(repr_new_.c_str());
    }

    xml_node message =  xdoc_.child("message");
    xml_attribute attr_to = message.attribute("to");
    if (!attr_to) {
        attr_to = message.append_attribute("to");
    }
    attr_to.set_value(str.c_str());
    ostringstream oss;
    xdoc_.save(oss);
    repr_ = oss.str();

    repr_part1_ = repr_.find("to=", 0);
    assert(repr_part1_ != string::npos);
    repr_part2_ = repr_.find("\n\t<event xmlns");
    assert(repr_part2_ != string::npos);

    *lenp = repr_.size();
    return reinterpret_cast<const uint8_t *>(repr_.c_str());
}

Message *BgpXmppMessageBuilder::Create(const BgpTable *table,
                                       const RibOutAttr *roattr,
                                       const BgpRoute *route) const {
    BgpXmppMessage *msg = new BgpXmppMessage(table, roattr);
    msg->Start(roattr, route);
    return msg;
}

BgpXmppMessageBuilder BgpXmppMessageBuilder::instance_;

BgpXmppMessageBuilder::BgpXmppMessageBuilder() {
}

BgpXmppMessageBuilder *BgpXmppMessageBuilder::GetInstance() {
    return &instance_;
}
