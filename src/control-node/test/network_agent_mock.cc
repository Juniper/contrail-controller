/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "network_agent_mock.h"

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <tr1/type_traits>

#include "base/logging.h"
#include "base/util.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_xmpp_channel.h"
#include "net/bgp_af.h"
#include "schema/xmpp_unicast_types.h"
#include "schema/xmpp_multicast_types.h"
#include "schema/xmpp_mvpn_types.h"
#include "schema/vnc_cfg_types.h"
#include "schema/xmpp_enet_types.h"
#include "xml/xml_pugi.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_factory.h"

using namespace std;
using namespace pugi;
using boost::asio::ip::address;
using boost::assign::list_of;
using std::string;
using std::vector;

namespace test {

const char *XmppDocumentMock::kControlNodeJID =
    "network-control@contrailsystems.com";
const char *XmppDocumentMock::kNetworkServiceJID =
    "network-control@contrailsystems.com/bgp-peer";
const char *XmppDocumentMock::kConfigurationServiceJID =
    "network-control@contrailsystems.com/config";
const char *XmppDocumentMock::kPubSubNS =
    "http://jabber.org/protocol/pubsub";

class NetworkAgentMock::AgentPeer : public BgpXmppChannel {
public:
    AgentPeer(NetworkAgentMock *parent, XmppChannel *channel)
        : BgpXmppChannel(channel), parent_(parent) {
        channel->RegisterReceive(xmps::CONFIG,
            boost::bind(&NetworkAgentMock::AgentPeer::ReceiveConfigUpdate,
                        this, _1));
    }
    virtual ~AgentPeer() {
        channel_->UnRegisterWriteReady(xmps::CONFIG);
        channel_->UnRegisterReceive(xmps::CONFIG);
        set_deleted(true);
        Close();
    }

    virtual void ReceiveConfigUpdate(const XmppStanza::XmppMessage *msg) {
        if (parent_->down()) return;
        tbb::mutex::scoped_lock lock(parent_->get_mutex());
        if (parent_->down()) return;

        XmlPugi *pugi = static_cast<XmlPugi *>(msg->dom.get());
        xml_node config = pugi->FindNode("config");

        for (xml_node node = config.first_child(); node;
            node = node.next_sibling()) {

            if (strcmp(node.name(), "update") == 0) {
            } else if (strcmp(node.name(), "delete") == 0) {
            } else {
                continue;
            }

            for(xml_node child = node.first_child(); child;
                    child = child.next_sibling()) {

                // Handle the links between the nodes
                if (strcmp(child.name(), "link") == 0) {
                    // LinkParse(child, oper, seq);
                    continue;
                }

                if (strcmp(child.name(), "node") == 0) {
                    const char *name = child.attribute("type").value();

                    if (!strcmp(name, "virtual-router")) {
                        string id_name = "virtual-router";
                        autogen::VirtualRouter *data =
                            new autogen::VirtualRouter();
                        assert(autogen::VirtualRouter::Decode(child, &id_name,
                                                              data));
                        id_name = "virtual-router:" + id_name;
                        parent_->vrouter_mgr_->Update(id_name, id_name, data);
                    }

                    if (!strcmp(name, "virtual-machine")) {
                        string id_name = "virtual-machine";
                        autogen::VirtualMachine *data =
                            new autogen::VirtualMachine();
                        assert(autogen::VirtualMachine::Decode(child, &id_name,
                                                               data));
                        id_name = "virtual-machine:" + id_name;
                        parent_->vm_mgr_->Update(id_name, id_name, data);
                    }
                }
            }
        }
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        if (parent_->down()) return;
        tbb::mutex::scoped_lock lock(parent_->get_mutex());
        if (parent_->down()) return;

        XmlPugi *pugi = static_cast<XmlPugi *>(msg->dom.get());
        xml_node items = pugi->FindNode("items");
        xml_attribute node = items.attribute("node");

        std::string nodename(node.value());
        bool inet_route = false;
        bool labeled_inet_route = false;
        bool inet6_route = false;
        bool enet_route = false;
        bool mcast_route = false;
        bool mvpn_route = false;
        const char *af = NULL, *safi = NULL, *network;
        char *str = const_cast<char *>(nodename.c_str());
        char *saveptr;
        af = strtok_r(str, "/", &saveptr);
        safi = strtok_r(NULL, "/", &saveptr);
        network = saveptr;

        if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Unicast) {
            inet_route = true;
        } else if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Mpls) {
            labeled_inet_route = true;
        } else if (atoi(af) == BgpAf::IPv6 && atoi(safi) == BgpAf::Unicast) {
            inet6_route = true;
        } else if (atoi(af) == BgpAf::L2Vpn && atoi(safi) == BgpAf::Enet) {
            enet_route = true;
        } else if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Mcast) {
            mcast_route = true;
        } else if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::MVpn) {
            mvpn_route = true;
        }

        xml_node retract_node = pugi->FindNode("retract");
        if (!pugi->IsNull(retract_node)) {
            while (retract_node) {
                std::string sid = retract_node.first_attribute().value();
                if (inet_route) {
                    if (parent_->skip_updates_processing()) {
                        parent_->route_mgr_->Update(node.value(), -1);
                    } else {
                        parent_->route_mgr_->Remove(network, sid);
                    }
                } else if (labeled_inet_route) {
                    if (parent_->skip_updates_processing()) {
                        parent_->labeled_inet_route_mgr_->Update(node.value(),
                                                                 -1);
                    } else {
                        parent_->labeled_inet_route_mgr_->Remove(network, sid);
                    }
                } else if (inet6_route) {
                    if (parent_->skip_updates_processing()) {
                        parent_->inet6_route_mgr_->Update(network, -1);
                    } else {
                        parent_->inet6_route_mgr_->Remove(network, sid);
                    }
                } else if (enet_route) {
                    if (parent_->skip_updates_processing()) {
                        parent_->enet_route_mgr_->Update(network, -1);
                    } else {
                        parent_->enet_route_mgr_->Remove(network, sid);
                    }
                } else if (mcast_route) {
                    if (parent_->skip_updates_processing()) {
                        parent_->mcast_route_mgr_->Update(network, -1);
                    } else {
                        string msid = sid;
                        char *mstr;
                        strtok_r(const_cast<char *>(msid.c_str()), ":", &mstr);
                        strtok_r(NULL, ":", &mstr);
                        parent_->mcast_route_mgr_->Remove(network, mstr);
                    }
                } else if (mvpn_route) {
                    if (parent_->skip_updates_processing()) {
                        parent_->mvpn_route_mgr_->Update(network, -1);
                    } else {
                        string msid = sid;
                        char *mstr;
                        strtok_r(const_cast<char *>(msid.c_str()), ":", &mstr);
                        strtok_r(NULL, ":", &mstr);
                        parent_->mvpn_route_mgr_->Remove(network, mstr);
                    }
                }
                retract_node = retract_node.next_sibling();
            }
        } else {
            for (xml_node item = items.first_child(); item;
                 item = item.next_sibling()) {
                xml_attribute id = item.attribute("id");
                std::string sid = id.value();
                if (inet_route) {
                    auto_ptr<autogen::ItemType> rt_entry(
                            new autogen::ItemType());
                    if (!rt_entry->XmlParse(item))
                        continue;

                    if (parent_->skip_updates_processing()) {
                        parent_->route_mgr_->Update(network, +1);
                    } else {
                        parent_->route_mgr_->Update(
                                network, sid, rt_entry.release());
                    }
                } else if (labeled_inet_route) {
                    auto_ptr<autogen::ItemType> rt_entry(
                            new autogen::ItemType());
                    if (!rt_entry->XmlParse(item))
                        continue;

                    if (parent_->skip_updates_processing()) {
                        parent_->labeled_inet_route_mgr_->Update(network, +1);
                    } else {
                        parent_->labeled_inet_route_mgr_->Update(network, sid,
                                                            rt_entry.release());
                    }
                } else if (inet6_route) {
                    auto_ptr<autogen::ItemType> rt_entry(
                            new autogen::ItemType());
                    if (!rt_entry->XmlParse(item)) {
                        continue;
                    }

                    if (parent_->skip_updates_processing()) {
                        parent_->inet6_route_mgr_->Update(network, +1);
                    } else {
                        parent_->inet6_route_mgr_->Update(network, sid,
                                                     rt_entry.release());
                    }
                } else if (enet_route) {
                    auto_ptr<autogen::EnetItemType> rt_entry(
                            new autogen::EnetItemType());
                    if (!rt_entry->XmlParse(item))
                        continue;

                    if (parent_->skip_updates_processing()) {
                        parent_->enet_route_mgr_->Update(network, +1);
                    } else {
                        parent_->enet_route_mgr_->Update(
                                network, sid, rt_entry.release());
                    }
                } else if (mcast_route) {
                    auto_ptr<autogen::McastItemType> rt_entry(
                            new autogen::McastItemType());
                    if (!rt_entry->XmlParse(item))
                        continue;

                    if (parent_->skip_updates_processing()) {
                        parent_->mcast_route_mgr_->Update(network, +1);
                    } else {
                        string msid = sid;
                        char *mstr;
                        strtok_r(const_cast<char *>(msid.c_str()), ":", &mstr);
                        strtok_r(NULL, ":", &mstr);
                        parent_->mcast_route_mgr_->Update(
                                network, mstr, rt_entry.release());
                    }
                } else if (mvpn_route) {
                    auto_ptr<autogen::MvpnItemType> rt_entry(
                            new autogen::MvpnItemType());
                    if (!rt_entry->XmlParse(item))
                        continue;

                    if (parent_->skip_updates_processing()) {
                        parent_->mvpn_route_mgr_->Update(network, +1);
                    } else {
                        string msid = sid;
                        char *mstr;
                        strtok_r(const_cast<char *>(msid.c_str()), ":", &mstr);
                        strtok_r(NULL, ":", &mstr);
                        parent_->mvpn_route_mgr_->Update(
                                network, mstr, rt_entry.release());
                    }
                }
            }
        }
    }

    void SendDocument(const pugi::xml_document *xdoc) {
        ostringstream oss;
        xdoc->save(oss);
        string msg = oss.str();
        boost::erase_all(msg, "\n");
        boost::erase_all(msg, "\t");
        Peer()->SendUpdate(reinterpret_cast<const uint8_t *>(msg.data()),
                           msg.length());
    }

private:
    NetworkAgentMock *parent_;
};

XmppDocumentMock::XmppDocumentMock(const std::string &hostname)
        : xmlns_(kPubSubNS), hostname_(hostname), label_alloc_(10000),
          xdoc_(new pugi::xml_document) {
    localaddr_ = "127.0.0.1";
}

pugi::xml_document *XmppDocumentMock::RouteAddXmlDoc(
        const std::string &network, const std::string &prefix,
        const NextHop &nh, const RouteAttributes &attributes,
        int primary_instance_index) {
    return RouteAddDeleteXmlDoc(network, prefix, true, nh, attributes,
                                primary_instance_index);
}

pugi::xml_document *XmppDocumentMock::RouteDeleteXmlDoc(
        const std::string &network, const std::string &prefix) {
    return RouteAddDeleteXmlDoc(network, prefix, false);
}

pugi::xml_document *XmppDocumentMock::LabeledInetRouteAddXmlDoc(
        const std::string &network, const std::string &prefix,
        const int label, const NextHop &nh) {
    return LabeledInetRouteAddDeleteXmlDoc(network, prefix, true, nh, label);
}

pugi::xml_document *XmppDocumentMock::LabeledInetRouteDeleteXmlDoc(
        const std::string &network, const std::string &prefix) {
    return LabeledInetRouteAddDeleteXmlDoc(network, prefix, false);
}


pugi::xml_document *XmppDocumentMock::Inet6RouteAddXmlDoc(
        const std::string &network, const std::string &prefix,
        const NextHop &nh, const RouteAttributes &attributes) {
    return Inet6RouteAddDeleteXmlDoc(network, prefix, ADD, nh, attributes);
}

pugi::xml_document *XmppDocumentMock::Inet6RouteChangeXmlDoc(
        const std::string &network, const std::string &prefix,
        const NextHop &nh, const RouteAttributes &attributes) {
    return Inet6RouteAddDeleteXmlDoc(network, prefix, CHANGE, nh, attributes);
}

pugi::xml_document *XmppDocumentMock::Inet6RouteDeleteXmlDoc(
        const std::string &network, const std::string &prefix) {
    return Inet6RouteAddDeleteXmlDoc(network, prefix, DELETE);
}

pugi::xml_document *XmppDocumentMock::RouteEnetAddXmlDoc(
        const std::string &network, const std::string &prefix,
        const NextHop &nh, const RouteAttributes &attributes) {
    return RouteEnetAddDeleteXmlDoc(network, prefix, true, nh, attributes);
}

pugi::xml_document *XmppDocumentMock::RouteEnetDeleteXmlDoc(
        const std::string &network, const std::string &prefix) {
    return RouteEnetAddDeleteXmlDoc(network, prefix, false);
}

pugi::xml_document *XmppDocumentMock::RouteMvpnAddXmlDoc(
        const std::string &network, const std::string &sg,
        int rt_type, const string &nh) {
    return RouteMvpnAddDeleteXmlDoc(network, sg, true, rt_type, nh);
}

pugi::xml_document *XmppDocumentMock::RouteMcastAddXmlDoc(
        const std::string &network, const std::string &sg,
        const std::string &nexthop, const std::string &label_range,
        const std::string &encap) {
    return RouteMcastAddDeleteXmlDoc(
            network, sg, true, nexthop, label_range, encap);
}

pugi::xml_document *XmppDocumentMock::RouteMcastDeleteXmlDoc(
        const std::string &network, const std::string &sg) {
    return RouteMcastAddDeleteXmlDoc(network, sg, false);
}

pugi::xml_document *XmppDocumentMock::RouteMvpnDeleteXmlDoc(
        const std::string &network, const std::string &sg, int rt_type) {
    return RouteMvpnAddDeleteXmlDoc(network, sg, false, rt_type);
}


pugi::xml_document *XmppDocumentMock::SubscribeXmlDoc(
        const std::string &network, int id, bool no_ribout, string type) {
    return SubUnsubXmlDoc(network, id, no_ribout, true, type);
}

pugi::xml_document *XmppDocumentMock::UnsubscribeXmlDoc(
        const std::string &network, int id, string type) {
    return SubUnsubXmlDoc(network, id, false, false, type);
}

xml_node XmppDocumentMock::PubSubHeader(string type) {
    xml_node iq = xdoc_->append_child("iq");
    iq.append_attribute("type") = "set";
    iq.append_attribute("from") = hostname().c_str();
    iq.append_attribute("to") = type.c_str();
    // TODO: iq.append_attribute("id") =
    xml_node pubsub = iq.append_child("pubsub");
    pubsub.append_attribute("xmlns") =
        xmlns_.empty() ? kPubSubNS : xmlns_.c_str();
    return pubsub;
}

pugi::xml_document *XmppDocumentMock::SubUnsubXmlDoc(const std::string &network,
    int id, bool no_ribout, bool sub, string type) {
    xdoc_->reset();
    xml_node pubsub = PubSubHeader(type);
    xml_node subscribe = pubsub.append_child(
            sub ? "subscribe" : "unsubscribe" );
    subscribe.append_attribute("node") = network.c_str();
    if (id >= 0) {
        xml_node options = pubsub.append_child("options");
        xml_node instance_id = options.append_child("instance-id");
        instance_id.text().set(id);
        if (no_ribout) {
            xml_node no_ribout_node = options.append_child("no-ribout");
            no_ribout_node.text().set(no_ribout);
        }
    }
    return xdoc_.get();
}

/*
 * Empty publish and collection nodes constitute eor marker.
 */
pugi::xml_document *XmppDocumentMock::AddEorMarker() {
    xdoc_->reset();
    xml_node pubsub = PubSubHeader(kNetworkServiceJID);
    pubsub.append_child("publish");

    pubsub = PubSubHeader(kNetworkServiceJID);
    pubsub.append_child("collection");
    return xdoc_.get();
}

pugi::xml_document *XmppDocumentMock::RouteAddDeleteXmlDoc(
        const std::string &network, const std::string &prefix, bool add,
        const NextHop &nh, const RouteAttributes &attributes,
        int primary_instance_index) {
    xdoc_->reset();
    xml_node pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node pub = pubsub.append_child("publish");
    stringstream header;
    header << BgpAf::IPv4 << "/" <<  BgpAf::Unicast << "/" <<
              network.c_str() << "/" << prefix.c_str();
    if (primary_instance_index)
        header << "/" << primary_instance_index;
    pub.append_attribute("node") = header.str().c_str();
    autogen::ItemType rt_entry;
    rt_entry.Clear();
    rt_entry.entry.nlri.af = BgpAf::IPv4;
    rt_entry.entry.nlri.safi = BgpAf::Unicast;
    rt_entry.entry.nlri.address = prefix;

    if (add) {
        rt_entry.entry.local_preference = attributes.local_pref;
        rt_entry.entry.med = attributes.med;
        rt_entry.entry.mobility.seqno = attributes.mobility.seqno;
        rt_entry.entry.mobility.sticky = attributes.mobility.sticky;
        if (attributes.sgids.size()) {
            rt_entry.entry.security_group_list.security_group =
                attributes.sgids;
        } else {
            rt_entry.entry.security_group_list.security_group.push_back(101);
        }

        if (attributes.communities.size()) {
            rt_entry.entry.community_tag_list.community_tag =
                attributes.communities;
        }

        // Encode LoadBalance attribute
        attributes.loadBalanceAttribute.Encode(&rt_entry.entry.load_balance);

        if (nh.address_.empty()) {
            autogen::NextHopType item_nexthop;
            item_nexthop.af = BgpAf::IPv4;
            item_nexthop.address = localaddr();
            if (!primary_instance_index)
                item_nexthop.label = label_alloc_++;
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation =
                list_of("gre").convert_to_container<vector<string> >();
            rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
        } else {
            autogen::NextHopType item_nexthop;
            item_nexthop.af = BgpAf::IPv4;
            assert(!nh.address_.empty());
            item_nexthop.address = nh.address_;
            if (!primary_instance_index && !nh.no_label_) {
                item_nexthop.label = nh.label_ ? nh.label_ : label_alloc_++;
            }
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation =
                nh.tunnel_encapsulations_;
            item_nexthop.tag_list.tag = nh.tag_list_;
            rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
        }
    }

    xml_node item = pub.append_child("item");
    rt_entry.Encode(&item);
    pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node collection = pubsub.append_child("collection");
    collection.append_attribute("node") = network.c_str();
    xml_node assoc = collection.append_child(
            add ? "associate" : "dissociate");
    assoc.append_attribute("node") = header.str().c_str();
    return xdoc_.get();
}

pugi::xml_document *XmppDocumentMock::LabeledInetRouteAddDeleteXmlDoc(
        const std::string &network, const std::string &prefix, bool add,
        const NextHop &nh, const int label) {
    xdoc_->reset();
    xml_node pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node pub = pubsub.append_child("publish");
    stringstream header;
    header << BgpAf::IPv4 << "/" <<  BgpAf::Mpls << "/" <<
              network.c_str() << "/" << prefix.c_str();
    pub.append_attribute("node") = header.str().c_str();
    autogen::ItemType rt_entry;
    rt_entry.Clear();
    rt_entry.entry.nlri.af = BgpAf::IPv4;
    rt_entry.entry.nlri.safi = BgpAf::Mpls;
    rt_entry.entry.nlri.address = prefix;

    if (add) {
        if (nh.address_.empty()) {
            autogen::NextHopType item_nexthop;
            item_nexthop.af = BgpAf::IPv4;
            item_nexthop.address = localaddr();
            item_nexthop.label = label;
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation =
                list_of("gre").convert_to_container<vector<string> >();
            rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
        } else {
            autogen::NextHopType item_nexthop;
            item_nexthop.af = BgpAf::IPv4;
            assert(!nh.address_.empty());
            item_nexthop.address = nh.address_;
            item_nexthop.label = label;
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation =
                nh.tunnel_encapsulations_;
            item_nexthop.tag_list.tag = nh.tag_list_;
            rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
        }
    }

    xml_node item = pub.append_child("item");
    rt_entry.Encode(&item);
    pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node collection = pubsub.append_child("collection");
    collection.append_attribute("node") = network.c_str();
    xml_node assoc = collection.append_child(
            add ? "associate" : "dissociate");
    assoc.append_attribute("node") = header.str().c_str();
    return xdoc_.get();
}

pugi::xml_document *XmppDocumentMock::Inet6RouteAddDeleteXmlDoc(
        const std::string &network, const std::string &prefix, Oper oper,
        const NextHop &nh, const RouteAttributes &attributes) {
    xdoc_->reset();
    xml_node pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node pub = pubsub.append_child("publish");
    stringstream header;
    header << BgpAf::IPv6 << "/" <<  BgpAf::Unicast << "/" <<
              network.c_str() << "/" << prefix.c_str();
    pub.append_attribute("node") = header.str().c_str();
    autogen::ItemType rt_entry;
    rt_entry.Clear();
    rt_entry.entry.nlri.af = BgpAf::IPv6;
    rt_entry.entry.nlri.safi = BgpAf::Unicast;
    rt_entry.entry.nlri.address = prefix;

    if (oper == ADD || oper == CHANGE) {
        rt_entry.entry.local_preference = attributes.local_pref;
        rt_entry.entry.med = attributes.med;
        rt_entry.entry.mobility.seqno = attributes.mobility.seqno;
        rt_entry.entry.mobility.sticky = attributes.mobility.sticky;
        if (attributes.sgids.size()) {
            rt_entry.entry.security_group_list.security_group =
                attributes.sgids;
        } else {
            rt_entry.entry.security_group_list.security_group.push_back(101);
        }

        if (attributes.communities.size()) {
            rt_entry.entry.community_tag_list.community_tag =
                attributes.communities;
        }

        // Encode LoadBalance attribute
        attributes.loadBalanceAttribute.Encode(&rt_entry.entry.load_balance);

        if (nh.address_.empty()) {
            autogen::NextHopType item_nexthop;
            item_nexthop.af = BgpAf::IPv4;
            item_nexthop.address = localaddr();
            item_nexthop.label = label_alloc_++;
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.
                push_back("gre");
            rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
        } else {
            autogen::NextHopType item_nexthop;
            item_nexthop.address = nh.address_;
            if (nh.address_.find(':') == string::npos) {
                item_nexthop.af = BgpAf::IPv4;
            } else {
                item_nexthop.af = BgpAf::IPv6;
            }
            if (!nh.no_label_) {
                if (oper == ADD) {
                    item_nexthop.label = (nh.label_ ?: label_alloc_++);
                } else {
                    item_nexthop.label = label_alloc_;
                }
            }
            item_nexthop.tag_list.tag = nh.tag_list_;
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation =
                nh.tunnel_encapsulations_;
            if (!nh.tunnel_encapsulations_.empty()) {
                if (nh.tunnel_encapsulations_[0] == "all_ipv6") {
                    item_nexthop.tunnel_encapsulation_list.
                    tunnel_encapsulation.push_back("gre");
                    item_nexthop.tunnel_encapsulation_list.
                    tunnel_encapsulation.push_back("udp");
                } else {
                    item_nexthop.tunnel_encapsulation_list.
                    tunnel_encapsulation.push_back(
                        nh.tunnel_encapsulations_[0]);
                }
            }
            rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
        }
    }

    xml_node item = pub.append_child("item");
    rt_entry.Encode(&item);
    pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node collection = pubsub.append_child("collection");
    collection.append_attribute("node") = network.c_str();
    xml_node assoc = collection.append_child(
        ((oper == ADD) || (oper == CHANGE)) ? "associate" : "dissociate");
    assoc.append_attribute("node") = header.str().c_str();
    return xdoc_.get();
}

pugi::xml_document *XmppDocumentMock::Inet6RouteAddBogusXmlDoc(
        const std::string &network, const std::string &prefix,
        NextHop nh, TestErrorType error_type) {
    xdoc_->reset();
    xml_node pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node pub = pubsub.append_child("publish");
    stringstream header;
    header << BgpAf::IPv6 << "/" <<  BgpAf::Unicast << "/" <<
              network.c_str() << "/" << prefix.c_str();
    pub.append_attribute("node") = header.str().c_str();
    autogen::ItemType rt_entry;
    rt_entry.Clear();
    if (error_type == ROUTE_AF_ERROR) {
        // Set incorrect afi value
        rt_entry.entry.nlri.af = BgpAf::IPv4;
    } else {
        rt_entry.entry.nlri.af = BgpAf::IPv6;
    }
    if (error_type == ROUTE_SAFI_ERROR) {
        // Set incorrect safi value
        rt_entry.entry.nlri.safi = BgpAf::EVpn;
    } else {
        rt_entry.entry.nlri.safi = BgpAf::Unicast;
    }
    rt_entry.entry.nlri.address = prefix;
    rt_entry.entry.security_group_list.security_group.push_back(101);

    if (!nh.address_.empty()) {
        autogen::NextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = nh.address_;
        item_nexthop.label = 0xfffff;
        item_nexthop.tag_list.tag = nh.tag_list_;
        item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.
        push_back(nh.tunnel_encapsulations_[0]);
        rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
    }

    xml_node item = pub.append_child("item");
    if (error_type == XML_TOKEN_ERROR) {
        xml_node n0 = item.append_child("entry");
        xml_node n1 = n0.append_child("nlri");
        xml_node n2 = n1.append_child("af");
        n2.text().set("some_junk");
    } else {
        rt_entry.Encode(&item);
    }
    pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node collection = pubsub.append_child("collection");
    collection.append_attribute("node") = network.c_str();
    xml_node assoc = collection.append_child("associate");
    assoc.append_attribute("node") = header.str().c_str();
    return xdoc_.get();
}

pugi::xml_document *XmppDocumentMock::RouteEnetAddDeleteXmlDoc(
        const std::string &network, const std::string &prefix, bool add,
        const NextHop &nh, const RouteAttributes &attributes) {
    xdoc_->reset();
    xml_node pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node pub = pubsub.append_child("publish");
    stringstream node_str;
    node_str << BgpAf::L2Vpn << "/" << BgpAf::Enet << "/"
             << network << "/" << prefix;
    pub.append_attribute("node") = node_str.str().c_str();
    autogen::EnetItemType rt_entry;
    rt_entry.Clear();

    std::string temp(prefix.c_str());
    char *str = const_cast<char *>(temp.c_str());
    char *saveptr;
    char *tag;
    char *mac;
    char *source = NULL;
    char *group = NULL;
    char *address;
    if (strchr(str, '-')) {
        bool has_ip = strchr(str, ',') != NULL;
        tag = strtok_r(str, "-", &saveptr);
        if (has_ip) {
            mac = strtok_r(NULL, ",", &saveptr);
            address = strtok_r(NULL, "", &saveptr);
        } else {
            mac = strtok_r(NULL, "", &saveptr);
            address = NULL;
        }
    } else {
        tag = NULL;
        bool has_ip = strchr(str, ',') != NULL;
        bool type6 = strchr(str, ',') != strrchr(str, ',');
        if (has_ip) {
            mac = strtok_r(str, ",", &saveptr);
            if (type6) {
                group = strtok_r(NULL, ",", &saveptr);
                source = strtok_r(NULL, ",", &saveptr);
            }
            address = strtok_r(NULL, "", &saveptr);
        } else {
            mac = strtok_r(str, "", &saveptr);
            address = NULL;
        }
    }

    rt_entry.entry.nlri.af = BgpAf::L2Vpn;
    rt_entry.entry.nlri.safi = BgpAf::Enet;
    rt_entry.entry.nlri.ethernet_tag = tag ? atoi(tag) : 0;
    rt_entry.entry.nlri.mac = std::string(mac) ;
    rt_entry.entry.nlri.address = address ? string(address) : string();
    rt_entry.entry.nlri.group = group ? string(group) : string();
    rt_entry.entry.nlri.source = source ? string(source) : string();

    if (add) {
        rt_entry.entry.local_preference = attributes.local_pref;
        rt_entry.entry.etree_leaf = attributes.etree_leaf;
        rt_entry.entry.mobility.seqno = attributes.mobility.seqno;
        rt_entry.entry.mobility.sticky = attributes.mobility.sticky;
        if (attributes.sgids.size()) {
            rt_entry.entry.security_group_list.security_group =
                attributes.sgids;
        } else {
            rt_entry.entry.security_group_list.security_group.push_back(101);
        }

        if (nh.address_.empty()) {
            autogen::EnetNextHopType item_nexthop;
            item_nexthop.af = BgpAf::IPv4;
            item_nexthop.address = localaddr();
            item_nexthop.label = label_alloc_++;
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation =
                list_of("gre").convert_to_container<vector<string> >();
            rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
        } else {
            autogen::EnetNextHopType item_nexthop;
            item_nexthop.af = BgpAf::IPv4;
            item_nexthop.address = nh.address_;
            item_nexthop.mac = nh.mac_;
            item_nexthop.label = nh.label_ ? nh.label_ : label_alloc_++;
            item_nexthop.l3_label = nh.l3_label_;
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation =
                nh.tunnel_encapsulations_;
            item_nexthop.tag_list.tag = nh.tag_list_;
            rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
        }

        if (attributes.params.edge_replication_not_supported)
            rt_entry.entry.edge_replication_not_supported = true;
        if (attributes.params.assisted_replication_supported)
            rt_entry.entry.assisted_replication_supported = true;
        if (!attributes.params.replicator_address.empty()) {
            rt_entry.entry.replicator_address =
                attributes.params.replicator_address;
        }
    }

    xml_node item = pub.append_child("item");
    rt_entry.Encode(&item);
    pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node collection = pubsub.append_child("collection");
    collection.append_attribute("node") = network.c_str();
    xml_node assoc = collection.append_child(
            add ? "associate" : "dissociate");
    assoc.append_attribute("node") = node_str.str().c_str();
    return xdoc_.get();
}

pugi::xml_document *XmppDocumentMock::RouteMcastAddDeleteXmlDoc(
        const std::string &network, const std::string &sg, bool add,
        const std::string &nexthop, const std::string &lrange,
        const std::string &encap) {
    xdoc_->reset();
    string sg_save(sg.c_str());
    xml_node pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node pub = pubsub.append_child("publish");
    stringstream node_str;
    node_str << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/"
             << network << "/" << sg_save;
    pub.append_attribute("node") = node_str.str().c_str();
    autogen::McastItemType rt_entry;
    rt_entry.Clear();

    char *str = const_cast<char *>(sg_save.c_str());
    char *saveptr;
    char *group = strtok_r(str, ",", &saveptr);
    char *source = NULL;
    if (group == NULL) {
        group = strtok_r(NULL, "", &saveptr);
    } else {
        source = strtok_r(NULL, "", &saveptr);
    }

    rt_entry.entry.nlri.af = BgpAf::IPv4;
    rt_entry.entry.nlri.safi = BgpAf::Mcast;
    rt_entry.entry.nlri.group = std::string(group) ;
    if (source != NULL) {
        rt_entry.entry.nlri.source = std::string(source);
    } else {
        rt_entry.entry.nlri.source = std::string("0.0.0.0");
    }

    autogen::McastNextHopType item_nexthop;

    if (!nexthop.empty()) {
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = nexthop.c_str();
        if (!lrange.empty()) {
            item_nexthop.label = lrange;
        }
        if (!encap.empty()) {
            if (encap == "all") {
                item_nexthop.tunnel_encapsulation_list.
                    tunnel_encapsulation.push_back("gre");
                item_nexthop.tunnel_encapsulation_list.
                    tunnel_encapsulation.push_back("udp");
            } else if (!encap.empty()) {
                item_nexthop.tunnel_encapsulation_list.
                    tunnel_encapsulation.push_back(encap);
            }
        }
        rt_entry.entry.next_hops.next_hop.push_back(item_nexthop);
    }

    xml_node item = pub.append_child("item");
    rt_entry.Encode(&item);
    pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node collection = pubsub.append_child("collection");
    collection.append_attribute("node") = network.c_str();
    xml_node assoc = collection.append_child(
            add ? "associate" : "dissociate");
    assoc.append_attribute("node") = node_str.str().c_str();
    return xdoc_.get();
}

pugi::xml_document *XmppDocumentMock::RouteMvpnAddDeleteXmlDoc(
        const std::string &network, const std::string &sg, bool add,
        int rt_type, const std::string &nexthop) {
    xdoc_->reset();
    string sg_save(sg.c_str());
    xml_node pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node pub = pubsub.append_child("publish");
    stringstream node_str;
    node_str << BgpAf::IPv4 << "/" << BgpAf::MVpn << "/"
             << network << "/" << sg_save;
    pub.append_attribute("node") = node_str.str().c_str();
    autogen::MvpnItemType rt_entry;
    rt_entry.Clear();

    char *str = const_cast<char *>(sg_save.c_str());
    char *saveptr;
    char *group = strtok_r(str, ",", &saveptr);
    char *source = NULL;
    if (group == NULL) {
        group = strtok_r(NULL, "", &saveptr);
    } else {
        source = strtok_r(NULL, "", &saveptr);
    }

    rt_entry.entry.nlri.af = BgpAf::IPv4;
    rt_entry.entry.nlri.safi = BgpAf::MVpn;
    rt_entry.entry.nlri.route_type = rt_type;
    rt_entry.entry.nlri.group = std::string(group) ;
    if (source != NULL) {
        rt_entry.entry.nlri.source = std::string(source);
    } else {
        rt_entry.entry.nlri.source = std::string("0.0.0.0");
    }

    autogen::MvpnNextHopType item_nexthop;
    if (!nexthop.empty()) {
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = nexthop.c_str();
        rt_entry.entry.next_hop = item_nexthop;
    }

    xml_node item = pub.append_child("item");
    rt_entry.Encode(&item);
    pubsub = PubSubHeader(kNetworkServiceJID);
    xml_node collection = pubsub.append_child("collection");
    collection.append_attribute("node") = network.c_str();
    xml_node assoc = collection.append_child(
            add ? "associate" : "dissociate");
    assoc.append_attribute("node") = node_str.str().c_str();
    return xdoc_.get();
}

// XmppChannelMuxTest mock test class to protect rxmap_ from parallel accesses
// between main and XmppStateMachine tasks.
class XmppChannelMuxTest : public XmppChannelMux {
public:
    XmppChannelMuxTest(XmppConnection *conn) : XmppChannelMux(conn) { }

    // Protext XmppChannelMux::rxmap_ from parallel threads, as it directly
    // used off main() by NetworkAgentMock clients. Production code always
    // accesses them off xmpp::XmppStateMachine task or bgp::Config task, which
    // are mutually exclusive.
    void RegisterReceive(xmps::PeerId id, ReceiveCb cb) {
        tbb::mutex::scoped_lock lock(mutex_);
        XmppChannelMux::RegisterReceive(id, cb);
    }
    void UnRegisterReceive(xmps::PeerId id) {
        tbb::mutex::scoped_lock lock(mutex_);
        XmppChannelMux::UnRegisterReceive(id);
    }
    void ProcessXmppMessage(const XmppStanza::XmppMessage *msg) {
        tbb::mutex::scoped_lock lock(mutex_);
        XmppChannelMux::ProcessXmppMessage(msg);
    }

private:
    tbb::mutex mutex_;
};

void NetworkAgentMock::Initialize() {
    static bool init_ = false;

    if (init_) return;
    init_ = true;

    XmppObjectFactory::Register<XmppChannelMux>(
        boost::factory<XmppChannelMuxTest *>());
}

NetworkAgentMock::NetworkAgentMock(EventManager *evm, const string &hostname,
                                   int server_port, string local_address,
                                   string server_address,
                                   bool xmpp_auth_enabled,
                                   const string &xmlns)
    : impl_(new XmppDocumentMock(hostname)),
      server_address_(server_address), local_address_(local_address),
      server_port_(server_port), skip_updates_processing_(false), down_(false),
      xmpp_auth_enabled_(xmpp_auth_enabled), id_(0) {

    // Static initialization of NetworkAgentMock class.
    Initialize();

    route_mgr_.reset(new InstanceMgr<RouteEntry>(this,
            XmppDocumentMock::kNetworkServiceJID));
    labeled_inet_route_mgr_.reset(new InstanceMgr<LabeledInetRouteEntry>(this,
            XmppDocumentMock::kNetworkServiceJID));
    inet6_route_mgr_.reset(new InstanceMgr<Inet6RouteEntry>(this,
            XmppDocumentMock::kNetworkServiceJID));
    inet6_route_mgr_->set_ipv6(true);
    enet_route_mgr_.reset(new InstanceMgr<EnetRouteEntry>(this,
            XmppDocumentMock::kNetworkServiceJID));
    mcast_route_mgr_.reset(new InstanceMgr<McastRouteEntry>(this,
            XmppDocumentMock::kNetworkServiceJID));
    mvpn_route_mgr_.reset(new InstanceMgr<MvpnRouteEntry>(this,
            XmppDocumentMock::kNetworkServiceJID));
    vrouter_mgr_.reset(new InstanceMgr<VRouterEntry>(this,
            XmppDocumentMock::kConfigurationServiceJID));
    vm_mgr_.reset(new InstanceMgr<VMEntry>(this,
            XmppDocumentMock::kConfigurationServiceJID));

    XmppConfigData *data = new XmppConfigData();
    XmppChannelConfig *cfg = CreateXmppConfig();
    cfg->xmlns = xmlns;
    impl_->set_xmlns(cfg->xmlns);
    data->AddXmppChannelConfig(cfg);
    client_ = new XmppClient(evm, cfg);
    client_->ConfigUpdate(data);
    if (!local_address.empty()) {
        impl_->set_localaddr(local_address);
    }
    client_->RegisterConnectionEvent(xmps::BGP,
        boost::bind(&NetworkAgentMock::XmppHandleChannelEvent, this, _1, _2));
    down_ = false;
}

void NetworkAgentMock::XmppHandleChannelEvent(XmppChannel *channel,
                                              xmps::PeerState state) {
    if (state == xmps::READY)
        return;
    tbb::mutex::scoped_lock lock(mutex_);
    ClearInstances();
}

void NetworkAgentMock::DisableRead(bool disable_read) {
    XmppConnection *connection;

    connection = client_->FindConnection("network-control@contrailsystems.com");
    if (connection) connection->set_disable_read(disable_read);
}

const string NetworkAgentMock::ToString() const {
    ostringstream ostr;

    ostr << hostname() << " " << localaddr() << " > " << server_address_;
    return ostr.str();
}

void NetworkAgentMock::ClearInstances() {
    route_mgr_->Clear();
    labeled_inet_route_mgr_->Clear();
    inet6_route_mgr_->Clear();
    enet_route_mgr_->Clear();
    mcast_route_mgr_->Clear();
    mvpn_route_mgr_->Clear();
    vrouter_mgr_->Clear();
    vm_mgr_->Clear();
}

NetworkAgentMock::~NetworkAgentMock() {
    down_ = true;
    ClearInstances();
    peer_.reset();
    assert(!client_);
}

bool NetworkAgentMock::ConnectionDestroyed() const {
    return (client_->ConnectionCount() == 0);
}

void NetworkAgentMock::Delete() {
    peer_.reset();
    client_->Shutdown();
    task_util::WaitForIdle();
    client_->WaitForEmpty();
    task_util::WaitForIdle();
    for (int idx = 0; idx < 5000 && !ConnectionDestroyed(); ++idx)
        usleep(1000);
    assert(ConnectionDestroyed());
    TcpServerManager::DeleteServer(client_);
    client_ = NULL;
}

XmppChannelConfig *NetworkAgentMock::CreateXmppConfig() {
    XmppChannelConfig *config = new XmppChannelConfig(true);
    config->endpoint.address(address::from_string(server_address_));

#ifdef __APPLE__

    //
    // XXX On darwin, for non 127.0.0.1, explicitly add the address to lo0
    //
    // e.g. sudo /sbin/ifconfig lo0 alias 127.0.0.2
    //
#endif

    if (xmpp_auth_enabled_) {
        config->auth_enabled = true;
        config->path_to_server_cert =
            "controller/src/xmpp/testdata/server-build02.pem";
        config->path_to_server_priv_key =
            "controller/src/xmpp/testdata/server-build02.key";
    } else {
        config->auth_enabled = false;
    }
    config->local_endpoint.address(address::from_string(local_address_));
    config->local_endpoint.port(0);
    config->endpoint.port(server_port_);
    config->FromAddr = hostname();
    config->ToAddr = XmppDocumentMock::kControlNodeJID;
    return config;
}

NetworkAgentMock::AgentPeer *NetworkAgentMock::GetAgent() {
    AgentPeer *peer = peer_.get();
    if (peer == NULL) {
        XmppChannel *channel = client_->FindChannel(
                XmppDocumentMock::kControlNodeJID);
        assert(channel != NULL);
        peer = new AgentPeer(this, channel);
        peer_.reset(peer);
    }
    return peer;
}

void NetworkAgentMock::SessionDown() {
    tbb::mutex::scoped_lock lock(mutex_);
    down_ = true;
    XmppConnection *connection =
        client_->FindConnection("network-control@contrailsystems.com");
    if (connection)
        connection->SetAdminState(true);
}

void NetworkAgentMock::SessionUp() {
    tbb::mutex::scoped_lock lock(mutex_);
    down_ = false;
    XmppConnection *connection =
        client_->FindConnection("network-control@contrailsystems.com");
    if (connection)
        connection->SetAdminState(false);
}

size_t NetworkAgentMock::get_sm_connect_attempts() {
    XmppConnection *connection =
        client_->FindConnection("network-control@contrailsystems.com");
    return (connection ? connection->get_sm_connect_attempts() : 0);
}

size_t NetworkAgentMock::get_sm_keepalive_count() {
    XmppConnection *connection =
        client_->FindConnection("network-control@contrailsystems.com");
    return (connection ? connection->get_sm_keepalive_count() : 0);
}

size_t NetworkAgentMock::get_connect_error() {
    XmppConnection *connection =
        client_->FindConnection("network-control@contrailsystems.com");
    return (connection ? connection->get_connect_error() : 0);
}

size_t NetworkAgentMock::get_session_close() {
    XmppConnection *connection =
        client_->FindConnection("network-control@contrailsystems.com");
    return (connection ? connection->get_session_close() : 0);
}

uint32_t NetworkAgentMock::flap_count() {
    XmppConnection *connection =
        client_->FindConnection("network-control@contrailsystems.com");
    return (connection ? connection->flap_count() : 0);
}

bool NetworkAgentMock::IsSessionEstablished(bool *is_established) {
    CHECK_CONCURRENCY("bgp::Config");
    XmppChannel *channel = client_->FindChannel(
            XmppDocumentMock::kControlNodeJID);
    bool est = (channel != NULL && channel->GetPeerState() == xmps::READY);
    if (is_established)
        *is_established = est;
    return est;
}

bool NetworkAgentMock::IsEstablished() {
    bool is_established;
    task_util::TaskFire(boost::bind(&NetworkAgentMock::IsSessionEstablished,
        this, &is_established), "bgp::Config");
    return is_established;
}

bool NetworkAgentMock::IsChannelReady(bool *is_ready) {
    CHECK_CONCURRENCY("bgp::Config");
    AgentPeer *peer = GetAgent();
    bool ready = (peer != NULL && peer->channel() != NULL &&
                  peer->channel()->GetPeerState() == xmps::READY);
    if (is_ready)
        *is_ready = ready;
    return ready;
}

bool NetworkAgentMock::IsReady() {
    bool is_ready;
    task_util::TaskFire(boost::bind(&NetworkAgentMock::IsChannelReady,
        this, &is_ready), "bgp::Config");
    return is_ready;
}

void NetworkAgentMock::SendEorMarker() {
    AgentPeer *peer = GetAgent();
    peer->SendDocument(impl_->AddEorMarker());
}

void NetworkAgentMock::AddRoute(const string &network_name,
                                const string &prefix, const string nexthop,
                                int local_pref, int med,
                                int primary_instance_index) {
    NextHop nh(nexthop);
    RouteAttributes attributes(
        local_pref, med, RouteAttributes::GetDefaultSequence());

    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->RouteAddXmlDoc(network_name, prefix, nh,
            attributes, primary_instance_index);
    peer->SendDocument(xdoc);
    route_mgr_->AddOriginated(network_name, prefix);
}

void NetworkAgentMock::AddRoute(const string &network_name,
                                const string &prefix,
                                const NextHop &nh,
                                const RouteAttributes &attributes,
                                int primary_instance_index) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->RouteAddXmlDoc(network_name, prefix, nh,
            attributes, primary_instance_index);
    peer->SendDocument(xdoc);
    route_mgr_->AddOriginated(network_name, prefix);
}

void NetworkAgentMock::AddRoute(const string &network_name,
                                const string &prefix,
                                const string &nexthop,
                                const RouteAttributes &attributes,
                                int primary_instance_index) {
    NextHop nh(nexthop);
    AddRoute(network_name, prefix, nh, attributes, primary_instance_index);
}

void NetworkAgentMock::DeleteRoute(const string &network_name,
                                   const string &prefix) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->RouteDeleteXmlDoc(network_name, prefix);
    peer->SendDocument(xdoc);
    route_mgr_->DeleteOriginated(network_name, prefix);
}

void NetworkAgentMock::AddLabeledInetRoute(const string &network_name,
                                const string &prefix, const int label,
                                const string nexthop) {
    NextHop nh(nexthop);
    AgentPeer *peer = GetAgent();
    xml_document *xdoc =
        impl_->LabeledInetRouteAddXmlDoc(network_name, prefix, label, nh);
    peer->SendDocument(xdoc);
    route_mgr_->AddOriginated(network_name, prefix);
}

void NetworkAgentMock::DeleteLabeledInetRoute(const string &network_name,
                                   const string &prefix) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->LabeledInetRouteDeleteXmlDoc(network_name,
                                                             prefix);
    peer->SendDocument(xdoc);
    route_mgr_->DeleteOriginated(network_name, prefix);
}

void NetworkAgentMock::AddInet6Route(const string &network,
        const string &prefix, const string &nexthop,
        const RouteAttributes &attributes) {
    NextHop nh(nexthop);
    AddInet6Route(network, prefix, nh, attributes);
}

void NetworkAgentMock::AddInet6Route(const string &network,
                                     const string &prefix,
                                     const NextHop &nh,
                                     const RouteAttributes &attributes) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc =
        impl_->Inet6RouteAddXmlDoc(network, prefix, nh, attributes);
    peer->SendDocument(xdoc);
    inet6_route_mgr_->AddOriginated(network, prefix);
}

void NetworkAgentMock::AddInet6Route(const string &network,
        const string &prefix, const string &nexthop, int local_pref,
        int med) {
    NextHop nh(nexthop);
    RouteAttributes attributes(
        local_pref, med, RouteAttributes::GetDefaultSequence());
    AddInet6Route(network, prefix, nh, attributes);
}

void NetworkAgentMock::ChangeInet6Route(const string &network,
        const string &prefix, const NextHop &nh,
        const RouteAttributes &attributes) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->Inet6RouteChangeXmlDoc(network, prefix,
                                                       nh, attributes);
    peer->SendDocument(xdoc);
}

void NetworkAgentMock::DeleteInet6Route(const string &network,
        const string &prefix) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->Inet6RouteDeleteXmlDoc(network, prefix);
    peer->SendDocument(xdoc);
    inet6_route_mgr_->DeleteOriginated(network, prefix);
}

void NetworkAgentMock::AddBogusInet6Route(const string &network,
        const string &prefix, const string &nexthop, TestErrorType error_type) {
    NextHop nh(nexthop);
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->Inet6RouteAddBogusXmlDoc(network, prefix,
                                                         nh, error_type);
    peer->SendDocument(xdoc);
    inet6_route_mgr_->AddOriginated(network, prefix);
}

void NetworkAgentMock::AddEnetRoute(const string &network_name,
        const string &prefix, const string nexthop, const RouteParams *params) {
    NextHop nh(nexthop);
    AddEnetRoute(network_name, prefix, nh, params);
}

void NetworkAgentMock::AddEnetRoute(const string &network_name,
        const string &prefix, const NextHop &nh, const RouteParams *params) {
    AgentPeer *peer = GetAgent();
    RouteAttributes attributes;
    if (params)
        attributes = RouteAttributes(*params);
    xml_document *xdoc =
        impl_->RouteEnetAddXmlDoc(network_name, prefix, nh, attributes);
    peer->SendDocument(xdoc);
    enet_route_mgr_->AddOriginated(network_name, prefix);
}

void NetworkAgentMock::AddEnetRoute(const string &network_name,
        const string &prefix, const string &nexthop,
        const RouteAttributes &attributes) {
    NextHop nh(nexthop);
    AddEnetRoute(network_name, prefix, nh, attributes);
}

void NetworkAgentMock::AddEnetRoute(const string &network_name,
        const string &prefix, const NextHop &nh,
        const RouteAttributes &attributes) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc =
        impl_->RouteEnetAddXmlDoc(network_name, prefix, nh, attributes);
    peer->SendDocument(xdoc);
    enet_route_mgr_->AddOriginated(network_name, prefix);
}

void NetworkAgentMock::DeleteEnetRoute(const string &network_name,
        const string &prefix) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc =
        impl_->RouteEnetDeleteXmlDoc(network_name, prefix);
    peer->SendDocument(xdoc);
    enet_route_mgr_->DeleteOriginated(network_name, prefix);
}

void NetworkAgentMock::AddMcastRoute(const string &network_name,
                                     const string &sg,
                                     const string &nexthop,
                                     const string &label_range,
                                     const string &encap) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->RouteMcastAddXmlDoc(
            network_name, sg, nexthop, label_range, encap);
    peer->SendDocument(xdoc);
    mcast_route_mgr_->AddOriginated(network_name, sg);
}

void NetworkAgentMock::AddType5MvpnRoute(const string &network_name,
                                         const string &sg,
                                         const string &mvpn_nexthop) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc;
    xdoc = impl_->RouteMvpnAddXmlDoc(network_name, sg, 5, mvpn_nexthop);
    peer->SendDocument(xdoc);
    mvpn_route_mgr_->AddOriginated(network_name, sg);
}

void NetworkAgentMock::AddType7MvpnRoute(const string &network_name,
                                         const string &sg,
                                         const string &nexthop,
                                         const string &label_range,
                                         const string &encap) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->RouteMvpnAddXmlDoc(network_name, sg, 7);
    peer->SendDocument(xdoc);
    mvpn_route_mgr_->AddOriginated(network_name, sg);
    AddMcastRoute(BgpConfigManager::kFabricInstance, sg, nexthop,
            label_range, encap);
}

void NetworkAgentMock::DeleteMvpnRoute(const string &network_name,
                                        const string &sg, int rt_type) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->RouteMvpnDeleteXmlDoc(network_name, sg,
            rt_type);
    peer->SendDocument(xdoc);
    mvpn_route_mgr_->DeleteOriginated(network_name, sg);
}

void NetworkAgentMock::DeleteMcastRoute(const string &network_name,
                                        const string &sg) {
    AgentPeer *peer = GetAgent();
    xml_document *xdoc = impl_->RouteMcastDeleteXmlDoc(network_name, sg);
    peer->SendDocument(xdoc);
    enet_route_mgr_->DeleteOriginated(network_name, sg);
}

template<typename T>
NetworkAgentMock::Instance<T>::Instance() {
    count_ = 0;
}

template<typename T>
NetworkAgentMock::Instance<T>::~Instance() {
    STLDeleteElements(&table_);
}

template<typename T>
void NetworkAgentMock::Instance<T>::Update(long count) {
    count_ += count;
}

template<typename T>
void NetworkAgentMock::Instance<T>::AddOriginated(
        const std::string &prefix) {
    originated_.insert(prefix);
}

template<typename T>
void NetworkAgentMock::Instance<T>::DeleteOriginated(
        const std::string &prefix) {
    originated_.erase(prefix);
}

template<typename T>
void NetworkAgentMock::Instance<T>::Update(const std::string &node, T *entry) {
    count_++;

    //
    // If a route already exists, delete it
    //
    Remove(node);
    table_.insert(make_pair(node, entry));
}

template<typename T>
void NetworkAgentMock::Instance<T>::Remove(const std::string &node) {
    typename TableMap::iterator iter = table_.find(node);

    if (iter != table_.end()) {
        T *entry = iter->second;
        table_.erase(node);
        delete entry;
        count_--;
    }
}

template<typename T>
void NetworkAgentMock::Instance<T>::Clear() {
    count_ = 0;
    STLDeleteElements(&table_);
    originated_.clear();
}

template<typename T>
int NetworkAgentMock::Instance<T>::Count() const {
    return count_;
    return table_.size();
}

template<typename T>
const T *NetworkAgentMock::Instance<T>::Lookup(const std::string &node) const {
    typename TableMap::const_iterator loc = table_.find(node);
    if (loc != table_.end()) {
        return loc->second;
    }
    return NULL;
}

template<typename T>
void NetworkAgentMock::InstanceMgr<T>::Subscribe(const std::string &network,
                                                 int id, bool no_ribout,
                                                 bool wait_for_established,
                                                 bool send_subscribe) {
    if (wait_for_established) {
        TASK_UTIL_EXPECT_EQ_MSG(true, parent_->IsEstablished(),
                                "Waiting for agent " << parent_->ToString() <<
                                " to become established");
    }
    tbb::mutex::scoped_lock lock(parent_->get_mutex());

    typename InstanceMap::iterator loc = instance_map_.find(network);
    if (loc == instance_map_.end()) {
        Instance<T> *rti = new Instance<T>();
        instance_map_.insert(make_pair(network, rti));
    }

    if (!send_subscribe)
        return;

    xml_document *xdoc;
    xdoc = parent_->GetXmlHandler()->SubscribeXmlDoc(
        network, id, no_ribout, type_);

    AgentPeer *peer = parent_->GetAgent();
    assert(peer != NULL);
    peer->SendDocument(xdoc);
}

template<typename T>
bool NetworkAgentMock::InstanceMgr<T>::HasSubscribed(
        const std::string &network) {
    // if (!parent_->IsEstablished()) return false;

    tbb::mutex::scoped_lock lock(parent_->get_mutex());

    typename InstanceMap::iterator loc = instance_map_.find(network);
    return (loc != instance_map_.end());
}

template<typename T>
void NetworkAgentMock::InstanceMgr<T>::Unsubscribe(const std::string &network,
                                                   int id,
                                                   bool wait_for_established,
                                                   bool send_unsubscribe,
                                                   bool withdraw_routes) {
    if (wait_for_established) {
        TASK_UTIL_EXPECT_EQ_MSG(true, parent_->IsEstablished(),
                                "Waiting for agent " << parent_->ToString() <<
                                " to become established");
    }

    tbb::mutex::scoped_lock lock(parent_->get_mutex());
    AgentPeer *peer = parent_->GetAgent();

    //
    // Delete all entries locally, as we do not get an route
    // retract messages as a result of unsubscribe
    //
    Instance<T> *rti;
    typename InstanceMap::iterator loc = instance_map_.find(network);
    if (loc == instance_map_.end()) {
        peer->SendDocument(parent_->GetXmlHandler()->UnsubscribeXmlDoc(network,
                                                                       id,
                                                                       type_));
        return;
    }

    rti = loc->second;

    // Withdraw all entries before sending unsubscribe.
    if (withdraw_routes) {
        typename NetworkAgentMock::Instance<T>::OriginatedSet::const_iterator
            iter = rti->originated().begin();
        while (iter != rti->originated().end()) {
            xml_document *xdoc = NULL;
            if (std::tr1::is_same<T, RouteEntry>::value) {
                if (ipv6())
                    xdoc = parent_->impl_->Inet6RouteDeleteXmlDoc(network,
                                                                  *iter);
                else
                    xdoc = parent_->impl_->RouteDeleteXmlDoc(network, *iter);
            } else if (std::tr1::is_same<T, EnetRouteEntry>::value) {
                xdoc = parent_->impl_->RouteEnetDeleteXmlDoc(network, *iter);
            } else if (std::tr1::is_same<T, McastRouteEntry>::value) {
                xdoc = parent_->impl_->RouteMcastDeleteXmlDoc(network, *iter);
            } else if (std::tr1::is_same<T, MvpnRouteEntry>::value) {
                // Delete all types of routes
                xdoc = parent_->impl_->RouteMvpnDeleteXmlDoc(network, *iter, 5);
                xdoc = parent_->impl_->RouteMvpnDeleteXmlDoc(network, *iter, 7);
            } else if (std::tr1::is_same<T, LabeledInetRouteEntry>::value) {
                xdoc = parent_->impl_->LabeledInetRouteDeleteXmlDoc(network,
                                                                    *iter);
            } else {
                assert(false);
            }
            peer->SendDocument(xdoc);
            iter++;
        }
    }

    if (send_unsubscribe) {
        pugi::xml_document *xdoc;
        xdoc = parent_->GetXmlHandler()->UnsubscribeXmlDoc(network, id, type_);

        assert(peer != NULL);
        peer->SendDocument(xdoc);
    }

    rti->Clear();
    instance_map_.erase(loc);
    delete rti;
}

template<typename T>
void NetworkAgentMock::InstanceMgr<T>::DeleteOriginated(
        const std::string &network, const std::string &prefix) {
    Instance<T> *rti;
    typename InstanceMgr<T>::InstanceMap::iterator loc;

    loc = instance_map_.find(network);

    if (loc != instance_map_.end()) {
        rti = loc->second;
        rti->DeleteOriginated(prefix);
    }
}

template<typename T>
void NetworkAgentMock::InstanceMgr<T>::AddOriginated(
        const std::string &network, const std::string &prefix) {
    Instance<T> *rti;
    typename InstanceMgr<T>::InstanceMap::iterator loc;

    loc = instance_map_.find(network);

    if (loc != instance_map_.end()) {
        rti = loc->second;
        rti->AddOriginated(prefix);
    }
}

template<typename T>
void NetworkAgentMock::InstanceMgr<T>::Update(const std::string &network,
                                              long count) {
    Instance<T> *rti;
    typename InstanceMgr<T>::InstanceMap::iterator loc;

    loc = instance_map_.find(network);

    if (loc != instance_map_.end()) {
        rti = loc->second;
        rti->Update(count);
    }
}

template<typename T>
void NetworkAgentMock::InstanceMgr<T>::Update(const std::string &network,
                                              const std::string &node_name,
                                              T *rt_entry) {
    Instance<T> *rti;
    typename InstanceMgr<T>::InstanceMap::iterator loc;

    loc = instance_map_.find(network);

    //
    // Ignore updates for instances to which the agent has not subscribed
    //
    if (loc == instance_map_.end()) {
        delete rt_entry;
        return;
    }

    rti = loc->second;
    rti->Update(node_name, rt_entry);
}

template<typename T>
void NetworkAgentMock::InstanceMgr<T>::Remove(const std::string &network,
        const std::string &node_name) {
    Instance<T> *rti;
    typename InstanceMgr<T>::InstanceMap::iterator loc;

    loc = instance_map_.find(network);

    if (loc == instance_map_.end()) return;

    rti = loc->second;
    rti->Remove(node_name);
}

template<typename T>
int NetworkAgentMock::InstanceMgr<T>::Count(const std::string &network) const {
    typename InstanceMgr<T>::InstanceMap::const_iterator loc;

    tbb::mutex::scoped_lock lock(parent_->get_mutex());
    loc = instance_map_.find(network);
    if (loc == instance_map_.end()) {
        return 0;
    }
    return loc->second->Count();
}

template<typename T>
int NetworkAgentMock::InstanceMgr<T>::Count() const {
    int count = 0;

    tbb::mutex::scoped_lock lock(parent_->get_mutex());
    for (typename InstanceMgr<T>::InstanceMap::const_iterator iter =
            instance_map_.begin(); iter != instance_map_.end(); ++iter) {
        count += iter->second->Count();
    }

    return count;
}

template<typename T>
void NetworkAgentMock::InstanceMgr<T>::Clear() {
    STLDeleteElements(&instance_map_);
}

template<typename T>
const T *NetworkAgentMock::InstanceMgr<T>::Lookup(const std::string &network,
        const std::string &prefix, bool take_lock) const {
    typename InstanceMgr<T>::InstanceMap::const_iterator loc;
    tbb::mutex::scoped_lock lock;

    if (take_lock)
        lock.acquire(parent_->get_mutex());

    loc = instance_map_.find(network);
    if (loc == instance_map_.end()) {
        return NULL;
    }
    return loc->second->Lookup(prefix);
}

#define INSTANTIATE_INSTANCE_TEMPLATES(T) \
template NetworkAgentMock::Instance<T>::Instance(); \
template NetworkAgentMock::Instance<T>::~Instance(); \
template void NetworkAgentMock::Instance<T>::Update(long count); \
template void NetworkAgentMock::Instance<T>::AddOriginated(const std::string &prefix); \
template void NetworkAgentMock::Instance<T>::DeleteOriginated(const std::string &prefix); \
template void NetworkAgentMock::Instance<T>::Update(const std::string &node, \
                                                    T *entry); \
template void NetworkAgentMock::Instance<T>::Remove(const std::string &node); \
template void NetworkAgentMock::Instance<T>::Clear(); \
template int NetworkAgentMock::Instance<T>::Count() const; \
template const T *NetworkAgentMock::Instance<T>::Lookup(const std::string &node) const; \
 \
template void NetworkAgentMock::InstanceMgr<T>::Subscribe(const std::string &network, int id, bool no_ribout, bool wait_for_established, bool send_subscribe); \
template bool NetworkAgentMock::InstanceMgr<T>::HasSubscribed(const std::string &network); \
template void NetworkAgentMock::InstanceMgr<T>::Unsubscribe(const std::string &network, int id, \
        bool wait_for_established, bool withdraw_routes, bool send_unsubscribe); \
template void NetworkAgentMock::InstanceMgr<T>::AddOriginated(const std::string &network, const std::string &prefix); \
template void NetworkAgentMock::InstanceMgr<T>::DeleteOriginated(const std::string &network, const std::string &prefix); \
template void NetworkAgentMock::InstanceMgr<T>::Update(const std::string &network, long count); \
template void NetworkAgentMock::InstanceMgr<T>::Remove(const std::string &network, const std::string &node_name); \
template int NetworkAgentMock::InstanceMgr<T>::Count(const std::string &network) const; \
template int NetworkAgentMock::InstanceMgr<T>::Count() const; \
template void NetworkAgentMock::InstanceMgr<T>::Clear(); \
template const T *NetworkAgentMock::InstanceMgr<T>::Lookup( \
    const std::string &network, const std::string &prefix, \
    const bool take_lock) const;

// RouteEntry is the same type as Inet6RouteEntry, used for both inet and inet6
INSTANTIATE_INSTANCE_TEMPLATES(NetworkAgentMock::RouteEntry)
INSTANTIATE_INSTANCE_TEMPLATES(NetworkAgentMock::EnetRouteEntry)
INSTANTIATE_INSTANCE_TEMPLATES(NetworkAgentMock::McastRouteEntry)
INSTANTIATE_INSTANCE_TEMPLATES(NetworkAgentMock::MvpnRouteEntry)
INSTANTIATE_INSTANCE_TEMPLATES(NetworkAgentMock::VRouterEntry)
INSTANTIATE_INSTANCE_TEMPLATES(NetworkAgentMock::VMEntry)

int NetworkAgentMock::RouteCount(const std::string &network) const {
    return route_mgr_->Count(network);
}

int NetworkAgentMock::RouteCount() const {
    return route_mgr_->Count();
}

bool NetworkAgentMock::HasSubscribed(const std::string &network) const {
    return route_mgr_->HasSubscribed(network);
}

// Return number of nexthops associated with a given route
int NetworkAgentMock::RouteNextHopCount(const std::string &network,
                                        const std::string &prefix) {
    tbb::mutex::scoped_lock lock(get_mutex());

    const RouteEntry *entry = route_mgr_->Lookup(network, prefix, false);
    if (!entry)
        return 0;

    return entry->entry.next_hops.next_hop.size();
}

int NetworkAgentMock::LabeledInetRouteCount(const std::string &network) const {
    return labeled_inet_route_mgr_->Count(network);
}

int NetworkAgentMock::LabeledInetRouteCount() const {
    return labeled_inet_route_mgr_->Count();
}

int NetworkAgentMock::Inet6RouteCount(const std::string &network) const {
    return inet6_route_mgr_->Count(network);
}

int NetworkAgentMock::Inet6RouteCount() const {
    return inet6_route_mgr_->Count();
}

int NetworkAgentMock::EnetRouteCount(const std::string &network) const {
    return enet_route_mgr_->Count(network);
}

int NetworkAgentMock::EnetRouteCount() const {
    return enet_route_mgr_->Count();
}

int NetworkAgentMock::McastRouteCount(const std::string &network) const {
    return mcast_route_mgr_->Count(network);
}

int NetworkAgentMock::McastRouteCount() const {
    return mcast_route_mgr_->Count();
}

int NetworkAgentMock::MvpnRouteCount(const std::string &network) const {
    return mvpn_route_mgr_->Count(network);
}

int NetworkAgentMock::MvpnRouteCount() const {
    return mvpn_route_mgr_->Count();
}

} // namespace test
