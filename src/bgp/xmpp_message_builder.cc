/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/xmpp_message_builder.h"

#include <boost/foreach.hpp>
#include <pugixml/pugixml.hpp>

#include <string>
#include <vector>

#include "bgp/ipeer.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/ermvpn/ermvpn_route.h"
#include "bgp/evpn/evpn_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/security_group/security_group.h"
#include "net/community_type.h"
#include "schema/xmpp_multicast_types.h"
#include "schema/xmpp_enet_types.h"
#include "xmpp/xmpp_init.h"

using pugi::xml_attribute;
using pugi::xml_document;
using pugi::xml_node;
using std::ostringstream;
using std::string;
using std::stringstream;
using std::vector;

class BgpXmppMessage : public Message {
public:
    BgpXmppMessage(const BgpTable *table, const RibOutAttr *roattr,
        bool cache_routes)
        : table_(table),
          writer_(&repr_),
          is_reachable_(roattr->IsReachable()),
          cache_routes_(cache_routes),
          repr_valid_(false),
          sequence_number_(0) {
    }
    virtual ~BgpXmppMessage() { }
    void Start(const RibOutAttr *roattr, const BgpRoute *route);
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr *roattr);
    virtual void Finish() { }
    virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp);

private:
    static const size_t kMaxFromToLength = 192;
    static const uint32_t kMaxReachCount = 32;
    static const uint32_t kMaxUnreachCount = 256;

    class XmlWriter : public pugi::xml_writer {
    public:
        explicit XmlWriter(string *repr) : repr_(repr) { }
        virtual void write(const void *data, size_t size) {
            repr_->append(static_cast<const char*>(data), size);
        }

    private:
        string *repr_;
    };

    void EncodeNextHop(const BgpRoute *route,
                       const RibOutAttr::NextHop &nexthop,
                       autogen::ItemType *item);
    void AddIpReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddIpUnreach(const BgpRoute *route);
    bool AddInetRoute(const BgpRoute *route, const RibOutAttr *roattr);

    bool AddInet6Route(const BgpRoute *route, const RibOutAttr *roattr);

    void EncodeEnetNextHop(const BgpRoute *route,
                           const RibOutAttr::NextHop &nexthop,
                           autogen::EnetItemType *item);
    void AddEnetReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddEnetUnreach(const BgpRoute *route);
    bool AddEnetRoute(const BgpRoute *route, const RibOutAttr *roattr);

    void AddMcastReach(const BgpRoute *route, const RibOutAttr *roattr);
    void AddMcastUnreach(const BgpRoute *route);
    bool AddMcastRoute(const BgpRoute *route, const RibOutAttr *roattr);

    void ProcessCommunity(const Community *community) {
        community_list_.clear();
        if (community == NULL)
            return;
        BOOST_FOREACH(uint32_t value, community->communities()) {
            community_list_.push_back(CommunityType::CommunityToString(value));
        }
    }

    void ProcessExtCommunity(const ExtCommunity *ext_community) {
        sequence_number_ = 0;
        security_group_list_.clear();
        load_balance_attribute_ = LoadBalance::LoadBalanceAttribute();
        if (ext_community == NULL)
            return;

        as_t as_number =  table_->server()->autonomous_system();
        for (ExtCommunity::ExtCommunityList::const_iterator iter =
             ext_community->communities().begin();
             iter != ext_community->communities().end(); ++iter) {
            if (ExtCommunity::is_security_group(*iter)) {
                SecurityGroup sg(*iter);
                if (sg.as_number() != as_number && !sg.IsGlobal())
                    continue;
                security_group_list_.push_back(sg.security_group_id());
            } else if (ExtCommunity::is_mac_mobility(*iter)) {
                MacMobility mm(*iter);
                sequence_number_ = mm.sequence_number();
            } else if (ExtCommunity::is_load_balance(*iter)) {
                LoadBalance load_balance(*iter);
                load_balance.FillAttribute(&load_balance_attribute_);
            }
        }
    }

    string GetVirtualNetwork(const RibOutAttr::NextHop &nexthop) const;
    string GetVirtualNetwork(const BgpRoute *route,
                             const RibOutAttr *roattr) const;

    const BgpTable *table_;
    XmlWriter writer_;
    bool is_reachable_;
    bool cache_routes_;
    bool repr_valid_;
    string repr_;
    uint32_t sequence_number_;
    vector<int> security_group_list_;
    vector<string> community_list_;
    LoadBalance::LoadBalanceAttribute load_balance_attribute_;

    DISALLOW_COPY_AND_ASSIGN(BgpXmppMessage);
};

void BgpXmppMessage::Start(const RibOutAttr *roattr, const BgpRoute *route) {
    if (is_reachable_) {
        const BgpAttr *attr = roattr->attr();
        ProcessCommunity(attr->community());
        ProcessExtCommunity(attr->ext_community());
    }

    // Reserve space for the begin line that contains the message opening tag
    // with from and to attributes. Actual value gets patched in when GetData
    // is called.
    repr_.append(kMaxFromToLength, ' ');

    // Add opening tags for event and items. The closing tags are added when
    // GetData is called.
    repr_ += "\n\t<event xmlns=\"http://jabber.org/protocol/pubsub\">";
    repr_ += "\n\t\t<items node=\"";
    repr_ += integerToString(route->Afi());
    repr_ += "/";
    repr_ += integerToString(route->XmppSafi());
    repr_ += "/";
    repr_ += table_->routing_instance()->name();
    repr_ += "\">\n";

    if (table_->family() == Address::ERMVPN) {
        AddMcastRoute(route, roattr);
    } else if (table_->family() == Address::EVPN) {
        AddEnetRoute(route, roattr);
    } else if (table_->family() == Address::INET6) {
        AddInet6Route(route, roattr);
    } else {
        AddInetRoute(route, roattr);
    }
}

bool BgpXmppMessage::AddRoute(const BgpRoute *route, const RibOutAttr *roattr) {
    assert(is_reachable_ == roattr->IsReachable());
    if (is_reachable_ && num_reach_route_ >= kMaxReachCount)
        return false;
    if (!is_reachable_ && num_unreach_route_ >= kMaxUnreachCount)
        return false;

    if (is_reachable_) {
        const BgpAttr *attr = roattr->attr();
        ProcessCommunity(attr->community());
        ProcessExtCommunity(attr->ext_community());
    }

    if (table_->family() == Address::ERMVPN) {
        return AddMcastRoute(route, roattr);
    } else if (table_->family() == Address::EVPN) {
        return AddEnetRoute(route, roattr);
    } else if (table_->family() == Address::INET6) {
        return AddInet6Route(route, roattr);
    } else {
        return AddInetRoute(route, roattr);
    }
}

void BgpXmppMessage::EncodeNextHop(const BgpRoute *route,
                                   const RibOutAttr::NextHop &nexthop,
                                   autogen::ItemType *item) {
    autogen::NextHopType item_nexthop;

    item_nexthop.af = route->NexthopAfi();
    item_nexthop.address = nexthop.address().to_v4().to_string();
    item_nexthop.label = nexthop.label();
    item_nexthop.virtual_network = GetVirtualNetwork(nexthop);

    // If encap list is empty use mpls over gre as default encap.
    vector<string> &encap_list =
        item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation;
    if (nexthop.encap().empty()) {
        encap_list.push_back(string("gre"));
    } else {
        encap_list = nexthop.encap();
    }

    item->entry.next_hops.next_hop.push_back(item_nexthop);
}

void BgpXmppMessage::AddIpReach(const BgpRoute *route,
                                const RibOutAttr *roattr) {
    if (!roattr->repr().empty()) {
        repr_ += roattr->repr();
        return;
    }

    autogen::ItemType item;
    item.entry.nlri.af = route->Afi();
    item.entry.nlri.safi = route->XmppSafi();
    item.entry.nlri.address = route->ToString();
    item.entry.version = 1;
    item.entry.virtual_network = GetVirtualNetwork(route, roattr);
    item.entry.local_preference = roattr->attr()->local_pref();
    item.entry.med = roattr->attr()->med();
    item.entry.sequence_number = sequence_number_;

    assert(!roattr->nexthop_list().empty());

    // Encode all next-hops in the list.
    BOOST_FOREACH(const RibOutAttr::NextHop &nexthop, roattr->nexthop_list()) {
        EncodeNextHop(route, nexthop, &item);
    }

    for (vector<int>::const_iterator it = security_group_list_.begin();
         it != security_group_list_.end(); ++it) {
        item.entry.security_group_list.security_group.push_back(*it);
    }

    for (vector<string>::const_iterator it = community_list_.begin();
         it != community_list_.end(); ++it) {
        item.entry.community_tag_list.community_tag.push_back(*it);
    }

    // Encode load balance attribute.
    if (!load_balance_attribute_.IsDefault())
        load_balance_attribute_.Encode(&item.entry.load_balance);

    xml_document doc;
    xml_node node = doc.append_child("item");
    node.append_attribute("id") = route->ToXmppIdString().c_str();

    // Remember the previous size.
    size_t pos = repr_.size();
    item.Encode(&node);
    doc.print(writer_, "\t", pugi::format_default, pugi::encoding_auto, 3);

    // Cache the substring starting at the previous size.
    if (cache_routes_)
        roattr->set_repr(repr_, pos);
}

void BgpXmppMessage::AddIpUnreach(const BgpRoute *route) {
    repr_ += "\t\t\t<retract id=\"" + route->ToXmppIdString() + "\" />\n";
}

bool BgpXmppMessage::AddInetRoute(const BgpRoute *route,
                                  const RibOutAttr *roattr) {
    if (is_reachable_) {
        num_reach_route_++;
        AddIpReach(route, roattr);
    } else {
        num_unreach_route_++;
        AddIpUnreach(route);
    }
    return true;
}

bool BgpXmppMessage::AddInet6Route(const BgpRoute *route,
                                   const RibOutAttr *roattr) {
    if (is_reachable_) {
        num_reach_route_++;
        AddIpReach(route, roattr);
    } else {
        num_unreach_route_++;
        AddIpUnreach(route);
    }
    return true;
}

void BgpXmppMessage::EncodeEnetNextHop(const BgpRoute *route,
                                       const RibOutAttr::NextHop &nexthop,
                                       autogen::EnetItemType *item) {
    autogen::EnetNextHopType item_nexthop;

    item_nexthop.af = BgpAf::IPv4;
    item_nexthop.address = nexthop.address().to_v4().to_string();
    item_nexthop.label = nexthop.label();

    // If encap list is empty use mpls over gre as default encap.
    vector<string> &encap_list =
        item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation;
    if (nexthop.encap().empty()) {
        encap_list.push_back(string("gre"));
    } else {
        encap_list = nexthop.encap();
    }
    item->entry.next_hops.next_hop.push_back(item_nexthop);
}

void BgpXmppMessage::AddEnetReach(const BgpRoute *route,
                                  const RibOutAttr *roattr) {
    if (!roattr->repr().empty()) {
        repr_ += roattr->repr();
        return;
    }

    autogen::EnetItemType item;
    item.entry.nlri.af = route->Afi();
    item.entry.nlri.safi = route->XmppSafi();

    EvpnRoute *evpn_route =
        static_cast<EvpnRoute *>(const_cast<BgpRoute *>(route));
    const EvpnPrefix &evpn_prefix = evpn_route->GetPrefix();
    item.entry.nlri.ethernet_tag = evpn_prefix.tag();
    item.entry.nlri.mac = evpn_prefix.mac_addr().ToString();
    item.entry.nlri.address = evpn_prefix.ip_address().to_string() + "/" +
        integerToString(evpn_prefix.ip_address_length());
    item.entry.virtual_network = GetVirtualNetwork(route, roattr);
    item.entry.local_preference = roattr->attr()->local_pref();
    item.entry.med = roattr->attr()->med();
    item.entry.sequence_number = sequence_number_;

    for (vector<int>::const_iterator it = security_group_list_.begin();
         it != security_group_list_.end(); ++it) {
        item.entry.security_group_list.security_group.push_back(*it);
    }

    const BgpOList *olist = roattr->attr()->olist().get();
    assert((olist == NULL) != roattr->nexthop_list().empty());

    if (olist) {
        assert(olist->olist().subcode == BgpAttribute::OList);
        BOOST_FOREACH(const BgpOListElem *elem, olist->elements()) {
            autogen::EnetNextHopType nh;
            nh.af = BgpAf::IPv4;
            nh.address = elem->address.to_string();
            nh.label = elem->label;
            nh.tunnel_encapsulation_list.tunnel_encapsulation = elem->encap;
            item.entry.olist.next_hop.push_back(nh);
        }
    }

    const BgpOList *leaf_olist = roattr->attr()->leaf_olist().get();
    assert((leaf_olist == NULL) != roattr->nexthop_list().empty());

    if (leaf_olist) {
        assert(leaf_olist->olist().subcode == BgpAttribute::LeafOList);
        BOOST_FOREACH(const BgpOListElem *elem, leaf_olist->elements()) {
            autogen::EnetNextHopType nh;
            nh.af = BgpAf::IPv4;
            nh.address = elem->address.to_string();
            nh.label = elem->label;
            nh.tunnel_encapsulation_list.tunnel_encapsulation = elem->encap;
            item.entry.leaf_olist.next_hop.push_back(nh);
        }
    }

    BOOST_FOREACH(const RibOutAttr::NextHop &nexthop, roattr->nexthop_list()) {
        EncodeEnetNextHop(route, nexthop, &item);
    }

    xml_document doc;
    xml_node node = doc.append_child("item");
    node.append_attribute("id") = route->ToXmppIdString().c_str();

    // Remember the previous size.
    size_t pos = repr_.size();
    item.Encode(&node);
    doc.print(writer_, "\t", pugi::format_default, pugi::encoding_auto, 3);

    // Cache the substring starting at the previous size.
    if (cache_routes_)
        roattr->set_repr(repr_, pos);
}

void BgpXmppMessage::AddEnetUnreach(const BgpRoute *route) {
    repr_ += "\t\t\t<retract id=\"" + route->ToXmppIdString() + "\" />\n";
}

bool BgpXmppMessage::AddEnetRoute(const BgpRoute *route,
    const RibOutAttr *roattr) {
    if (is_reachable_) {
        num_reach_route_++;
        AddEnetReach(route, roattr);
    } else {
        num_unreach_route_++;
        AddEnetUnreach(route);
    }
    return true;
}

//
// Note that there's no need to cache the string representation since a given
// mcast route is sent to exactly one xmpp peer.
//
void BgpXmppMessage::AddMcastReach(const BgpRoute *route,
                                   const RibOutAttr *roattr) {
    autogen::McastItemType item;
    item.entry.nlri.af = route->Afi();
    item.entry.nlri.safi = route->XmppSafi();

    ErmVpnRoute *ermvpn_route =
        static_cast<ErmVpnRoute *>(const_cast<BgpRoute *>(route));
    item.entry.nlri.group = ermvpn_route->GetPrefix().group().to_string();
    item.entry.nlri.source =  ermvpn_route->GetPrefix().source().to_string();
    item.entry.nlri.source_label = roattr->label();

    const BgpOList *olist = roattr->attr()->olist().get();
    assert(olist->olist().subcode == BgpAttribute::OList);
    BOOST_FOREACH(const BgpOListElem *elem, olist->elements()) {
        autogen::McastNextHopType nh;
        nh.af = BgpAf::IPv4;
        nh.address = elem->address.to_string();
        nh.label = integerToString(elem->label);
        nh.tunnel_encapsulation_list.tunnel_encapsulation = elem->encap;
        item.entry.olist.next_hop.push_back(nh);
    }

    xml_document doc;
    xml_node node = doc.append_child("item");
    node.append_attribute("id") = route->ToXmppIdString().c_str();
    item.Encode(&node);
    doc.print(writer_, "\t", pugi::format_default, pugi::encoding_auto, 3);
}

void BgpXmppMessage::AddMcastUnreach(const BgpRoute *route) {
    repr_ += "\t\t\t<retract id=\"" + route->ToXmppIdString() + "\" />\n";
}

bool BgpXmppMessage::AddMcastRoute(const BgpRoute *route,
                                   const RibOutAttr *roattr) {
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
    // Build begin line that contains message opening tag with from and to
    // attributes.
    string msg_begin;
    msg_begin.reserve(kMaxFromToLength);
    msg_begin += "\n<message from=\"";
    msg_begin += XmppInit::kControlNodeJID;
    msg_begin += "\" to=\"";
    msg_begin += peer->ToString();
    msg_begin += "/";
    msg_begin += XmppInit::kBgpPeer;
    msg_begin += "\">";

    // Add closing tags if this is the first peer to which the message will
    // be sent.
    if (!repr_valid_) {
        repr_ += "\t\t</items>\n\t</event>\n</message>\n";
        repr_valid_ = true;
    }

    // Replace the begin line if it fits in the space reserved at the start
    // of repr_.  Otherwise build a new string with the begin line and rest
    // of the message in repr_.
    if (msg_begin.size() <= kMaxFromToLength) {
        size_t extra = kMaxFromToLength - msg_begin.size();
        repr_.replace(extra, msg_begin.size(), msg_begin);
        *lenp = repr_.size() - extra;
        return reinterpret_cast<const uint8_t *>(repr_.c_str()) + extra;
    } else {
        string temp = msg_begin + string(repr_, kMaxFromToLength);
        *lenp = temp.size();
        return reinterpret_cast<const uint8_t *>(temp.c_str());
    }
}

string BgpXmppMessage::GetVirtualNetwork(
    const RibOutAttr::NextHop &nexthop) const {
    int index = nexthop.origin_vn_index();
    if (index > 0) {
        const RoutingInstanceMgr *manager =
            table_->routing_instance()->manager();
        return manager->GetVirtualNetworkByVnIndex(index);
    } else if (index == 0) {
        return table_->routing_instance()->GetVirtualNetworkName();
    } else {
        return "unresolved";
    }
}

string BgpXmppMessage::GetVirtualNetwork(const BgpRoute *route,
    const RibOutAttr *roattr) const {
    if (!is_reachable_) {
        return "unresolved";
    } else if (roattr->nexthop_list().empty()) {
        if (roattr->vrf_originated()) {
            return table_->routing_instance()->GetVirtualNetworkName();
        } else {
            return "unresolved";
        }
    } else {
        return GetVirtualNetwork(roattr->nexthop_list().front());
    }
}

Message *BgpXmppMessageBuilder::Create(const RibOut *ribout, bool cache_routes,
                                       const RibOutAttr *roattr,
                                       const BgpRoute *route) const {
    const BgpTable *table = ribout->table();
    BgpXmppMessage *msg = new BgpXmppMessage(table, roattr, cache_routes);
    msg->Start(roattr, route);
    return msg;
}

BgpXmppMessageBuilder::BgpXmppMessageBuilder() {
}
