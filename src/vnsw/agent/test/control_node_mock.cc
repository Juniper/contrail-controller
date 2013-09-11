/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <xml/xml_base.h>
#include "xml/xml_pugi.h"
#include "xmpp_unicast_types.h"

#include <base/logging.h>
#include <boost/bind.hpp>

#include <net/bgp_af.h>
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "control_node_mock.h"

using namespace std;
using namespace pugi;
using namespace autogen;

namespace test {

ControlNodeMock::ControlNodeMock(EventManager *evm, string address) :
                    evm_(evm), address_(address), 
                    channel_(NULL) {

    xs = new XmppServer(evm_, XmppInit::kControlNodeJID);
    xs->Initialize(0, false);
    server_port_ = xs->GetPort();

    xs->RegisterConnectionEvent(xmps::BGP,
                     boost::bind(&ControlNodeMock::XmppChannelEvent, this, _1, _2));
}

ControlNodeMock::~ControlNodeMock() {

    for(vector<VrfEntry *>::iterator it = vrf_list_.begin(); it != vrf_list_.end(); ) {
        VrfEntry *ent = *it;
        std::map<std::string, RouteEntry *>::iterator x;
        for(x = ent->route_list_.begin(); x != ent->route_list_.end();) {
            std::string address = x->first;
            delete x->second;
            x++;
            ent->route_list_.erase(address);
        }

        delete *it;
        it = vrf_list_.erase(it);
    }

    TcpServerManager::DeleteServer(xs);
}

void ControlNodeMock::Shutdown() {
    xs->Shutdown();
}

ControlNodeMock::VrfEntry* ControlNodeMock::GetVrf(const string &vrf) {

    VrfEntry *ent = NULL;

    for(vector<VrfEntry *>::iterator it = vrf_list_.begin(); it != vrf_list_.end(); ++it) {
        if ((*it)->name == vrf) {
            ent = *it;
            break;
        }
    }

    return ent;
}

ControlNodeMock::VrfEntry* ControlNodeMock::AddVrf(const string &vrf) {

    VrfEntry *ent;

    ent = GetVrf(vrf);

    if (!ent) {
        ent = new VrfEntry;
        ent->name = vrf;
        ent->subscribed = false;
        vrf_list_.push_back(ent);
    }

    return ent;
    
}

void ControlNodeMock::SubscribeVrf(const string &vrf) {
    VrfEntry *ent;
    ent = AddVrf(vrf);
    ent->subscribed = true;

    //Replay all routes now
    std::map<std::string, RouteEntry *>::iterator x;
    for(x = ent->route_list_.begin();x != ent->route_list_.end(); x++) {
        RouteEntry *rt = x->second;
        SendRoute(ent->name, rt, true);
    }

}

void ControlNodeMock::UnSubscribeVrf(const string &vrf) {

    vector<VrfEntry *>::iterator it;
    VrfEntry *ent = NULL;

    for(it = vrf_list_.begin(); it != vrf_list_.end(); ++it) {
        if ((*it)->name == vrf) {
            ent = *it;
            break;
        }
    }

    if (!ent) {
        return;
    }

    ent->subscribed = false;

}

ControlNodeMock::RouteEntry* 
ControlNodeMock::InsertRoute(string &vrf_name, string &address, 
                             string &nh, int label, string &vn) {

    VrfEntry *vrf = AddVrf(vrf_name);

    RouteEntry *ent =  vrf->route_list_[address];
    if (!ent) {
        ent = new RouteEntry;
    }

    ent->address = address;
    ent->vn = vn;

    //Populate the nexthop 
    NHEntry nh_entry;
    nh_entry.nh = nh;
    nh_entry.label = label;

    std::vector<NHEntry>::iterator it = ent->nh_list_.begin();
    while (it != ent->nh_list_.end()) {
        //If entry is present overwrite the entry
        if (nh_entry.nh == it->nh) {
            it->label = label;
            break;
        }
        it++;
    }

    if (it == ent->nh_list_.end()) {
        ent->nh_list_.push_back(nh_entry);
    }

    vrf->route_list_[address] = ent;
    return ent;
}

ControlNodeMock::RouteEntry* 
ControlNodeMock::RemoveRoute(string &vrf_name, string &address, 
                             string &nh, int label, string &vn, 
                             bool &send_delete) {

    VrfEntry *vrf = AddVrf(vrf_name);

    RouteEntry *ent =  vrf->route_list_[address];
    if (!ent) {
        return NULL;
    }

    ent->address = address;
    ent->vn = vn;

    //Populate the nexthop 
    NHEntry nh_entry;
    nh_entry.nh = nh;
    nh_entry.label = label;

    std::vector<NHEntry>::iterator it = ent->nh_list_.begin();
    while (it != ent->nh_list_.end()) {
        //If entry is present overwrite the entry
        if (nh_entry.nh == it->nh) {
            ent->nh_list_.erase(it);
            break;
        }
        it++;
    }

    if (ent->nh_list_.size() == 0) {
        send_delete = true;
    }

    vrf->route_list_[address] = ent;
    return ent;
}

void ControlNodeMock::GetRoutes(string vrf, const XmppStanza::XmppMessage *msg) {
    XmlBase *impl = msg->dom.get();
    const XmppStanza::XmppMessageIq *iq =
        static_cast<const XmppStanza::XmppMessageIq *>(msg);


    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);
    for (xml_node node = pugi->FindNode("item"); node;
                            node = node.next_sibling()) {
        if (strcmp(node.name(), "item") == 0) {

            //process route. For time being consider only add
            ItemType item;
            item.Clear();

            if (!item.XmlParse(node)) {
                return;
            }
            bool add = false;
            if (iq->is_as_node) {
                add = true;
            }

            RouteEntry *rt = InsertRoute(vrf, item.entry.nlri.address, 
                                 item.entry.next_hops.next_hop[0].address,
                                 item.entry.next_hops.next_hop[0].label,
                                 item.entry.virtual_network);
            SendRoute(vrf, rt, add);
        }
    }
}

void ControlNodeMock::ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
    if (msg->type == XmppStanza::IQ_STANZA) {
        const XmppStanza::XmppMessageIq *iq =
                   static_cast<const XmppStanza::XmppMessageIq *>(msg);
        if (iq->iq_type.compare("set") == 0) {

            if (iq->action.compare("subscribe") == 0) {
                SubscribeVrf(iq->node);
            } else if (iq->action.compare("unsubscribe") == 0) {
                UnSubscribeVrf(iq->node);
            } else if (iq->action.compare("publish") == 0) {
                GetRoutes(iq->node, msg);
            }
        }
    }
}

void ControlNodeMock::XmppChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
    if (state == xmps::READY) {
        channel_ = channel; 
        channel_->RegisterReceive(xmps::BGP,
                   boost::bind(&ControlNodeMock::ReceiveUpdate, this, _1));
    } else if (state == xmps::NOT_READY) {
        if (channel_) {
            channel_->UnRegisterReceive(xmps::BGP);
        }
        channel_ = NULL;
    }
}

xml_node ControlNodeMock::AddXmppHdr() {
    xdoc_.reset();
    xml_node msg = xdoc_.append_child("message");
    string str(channel_->connection()->ToString().c_str());
    str += "/";
    str += XmppInit::kBgpPeer;
    msg.append_attribute("from")  = XmppInit::kControlNodeJID;
    msg.append_attribute("to") = str.c_str();

    xml_node event = msg.append_child("event");
    event.append_attribute("xmlns") = "http://jabber.org/protocol/pubsub";
    return event;
}

void ControlNodeMock::SendUpdate(xmps::PeerId id) {
    ostringstream oss;
    xdoc_.save(oss);
    string msg = oss.str();
    channel_->Send(reinterpret_cast<const uint8_t *>(msg.data()), msg.size(), id,
                   boost::bind(&ControlNodeMock::WriteReadyCb, this, _1));

}

void ControlNodeMock::WriteReadyCb(const boost::system::error_code &ec) {
}

bool ControlNodeMock::IsEstablished() {

    if (channel_ && channel_->GetPeerState() == xmps::READY) {
        return true;
    }

    return false;
}


void ControlNodeMock::AddRoute(string vrf, string address, 
                               string nh, int label, string vn)  {

    RouteEntry *rt = InsertRoute(vrf, address, nh, label, vn);

    VrfEntry *ent = GetVrf(vrf);
    if (ent->subscribed == false) {
        return;
    }

    SendRoute(vrf, rt, true);

}

void ControlNodeMock::DeleteRoute(string vrf, string address, 
                               string nh, int label, string vn)  {

    bool send_delete = false;
    RouteEntry *rt = RemoveRoute(vrf, address, nh, label, vn, send_delete);

    VrfEntry *ent = GetVrf(vrf);
    if (ent->subscribed == false) {
        return;
    }

    if (rt) {
        SendRoute(vrf, rt, !send_delete);
    }
}

void ControlNodeMock::SendRoute(string vrf, RouteEntry *rt, bool add) {

    xml_node event = AddXmppHdr();
    xml_node items = event.append_child("items");
    stringstream nodestr;
    nodestr << BgpAf::IPv4 << "/" << BgpAf::Unicast << "/" << vrf.c_str();
    items.append_attribute("node") = nodestr.str().c_str();  

    autogen::ItemType item;

    autogen::NextHopType item_nexthop;
    std::vector<NHEntry>::iterator it = rt->nh_list_.begin();
    for (;it != rt->nh_list_.end(); it++) {
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.safi = BgpAf::Unicast;
        item_nexthop.address = it->nh;
        item_nexthop.label = it->label;
        item.entry.next_hops.next_hop.push_back(item_nexthop);
    }

    item.entry.nlri.af = BgpAf::IPv4;
    item.entry.nlri.safi = BgpAf::Unicast;
    item.entry.nlri.address = rt->address;
    item.entry.version = 1;
    item.entry.virtual_network = rt->vn;

    if (add) {
        xdoc_.append_child("associate");
    } else {
        xdoc_.append_child("dissociate");
        xml_node id = items.append_child("retract");
        id.append_attribute("id") = rt->address.c_str();
    }

    xml_node node = items.append_child("item");
    node.append_attribute("id") = rt->address.c_str();
    item.Encode(&node);
    SendUpdate(xmps::BGP);
}

void ControlNodeMock::Clear() {
    for(vector<VrfEntry *>::iterator it = vrf_list_.begin(); it != vrf_list_.end(); ) {
        VrfEntry *ent = *it;
        std::map<std::string, RouteEntry *>::iterator x;
        for(x = ent->route_list_.begin(); x != ent->route_list_.end();) {
            std::string address = x->first;
            delete x->second;
            x++;
            ent->route_list_.erase(address);
        }

        delete *it;
        it = vrf_list_.erase(it);
    }
}
} // namespace test
