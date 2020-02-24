/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_parser.h"

#include <boost/uuid/name_generator.hpp>
#include <pugixml/pugixml.hpp>

#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/rtarget/rtarget_address.h"
#include "ifmap/ifmap_server_table.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using pugi::xml_attribute;
using pugi::xml_document;
using pugi::xml_node;
using pugi::xml_parse_result;
using std::auto_ptr;
using std::istringstream;
using std::list;
using std::ostringstream;
using std::make_pair;
using std::map;
using std::multimap;
using std::pair;
using std::set;
using std::string;
using std::vector;

typedef multimap<
    pair<string, string>,
    pair<autogen::BgpSessionAttributes, string> > SessionMap;

namespace {

static void MapObjectSetProperty(const string &ltype, const string &lname,
                                 const string &propname,
                                 AutogenProperty *property,
                                 BgpConfigParser::RequestList *requests) {
    DBRequest *request = new DBRequest;
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = ltype;
    key->id_name = lname;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = propname;
    data->content.reset(property);
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    requests->push_back(request);
}

static void MapObjectClearProperty(const string &ltype, const string &lname,
                                   const string &propname,
                                   BgpConfigParser::RequestList *requests) {
    DBRequest *request = new DBRequest;
    request->oper = DBRequest::DB_ENTRY_DELETE;
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = ltype;
    key->id_name = lname;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = propname;
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    requests->push_back(request);
}

static void MapObjectLink(const string &ltype, const string &lname,
                         const string &rtype, const string &rname,
                         const string &linkname,
                         BgpConfigParser::RequestList *requests) {
    DBRequest *request = new DBRequest;
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = ltype;
    key->id_name = lname;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = linkname;
    data->id_type = rtype;
    data->id_name = rname;
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    requests->push_back(request);
}

static void MapObjectUnlink(const string &ltype, const string &lname,
                            const string &rtype, const string &rname,
                            const string &linkname,
                            BgpConfigParser::RequestList *requests) {
    DBRequest *request = new DBRequest;
    request->oper = DBRequest::DB_ENTRY_DELETE;
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = ltype;
    key->id_name = lname;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = linkname;
    data->id_type = rtype;
    data->id_name = rname;
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    requests->push_back(request);
}

static void MapObjectLinkAttr(const string &ltype, const string &lname,
                             const string &rtype, const string &rname,
                             const string &linkname, AutogenProperty *attr,
                             BgpConfigParser::RequestList *requests) {
    DBRequest *request = new DBRequest;
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = ltype;
    key->id_name = lname;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = linkname;
    data->id_type = rtype;
    data->id_name = rname;
    data->content.reset(attr);
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    requests->push_back(request);
}

static autogen::BgpSessionAttributes *GetPeeringSessionAttribute(
        const pair<string, string> &key, autogen::BgpPeeringAttributes *peer,
        int session_id, const string config_uuid) {
    string uuid;
    if (!config_uuid.empty()) {
        uuid = config_uuid;
    } else {
        uuid = BgpConfigParser::session_uuid(key.first, key.second, session_id);
    }

    autogen::BgpSession *session = NULL;
    for (vector<autogen::BgpSession>::iterator iter =
            peer->session.begin();
         iter != peer->session.end(); ++iter) {
        if (iter->uuid == uuid) {
            session = iter.operator->();
            break;
        }
    }
    if (session == NULL) {
        peer->session.push_back(autogen::BgpSession());
        session = &peer->session.back();
        session->uuid = uuid;
    }
    session->attributes.push_back(autogen::BgpSessionAttributes());
    autogen::BgpSessionAttributes *sattr = &session->attributes.back();
    return sattr;
}

static void MaybeMergeBidirectionalSessionParams(
        autogen::BgpPeeringAttributes *peer) {
}

static void BuildPeeringLinks(const string &instance,
                              const SessionMap &sessions,
                              BgpConfigParser::RequestList *requests) {
    typedef map<pair<string, string>, autogen::BgpPeeringAttributes *>
        PeeringMap;
    PeeringMap peerings;

    pair<string, string> sprev;
    int session_id = 0;
    for (SessionMap::const_iterator iter = sessions.begin();
         iter != sessions.end(); ++iter) {
        const string &left = iter->first.first;
        const string &right = iter->first.second;
        pair<string, string> key;
        if (left <= right) {
            key = make_pair(left, right);
        } else {
            key = make_pair(right, left);
        }

        string config_uuid = iter->second.second;
        if ((sprev.first == left) && (sprev.second == right)) {
            session_id++;
        } else {
            session_id = 1;
            sprev = make_pair(left, right);
        }

        autogen::BgpPeeringAttributes *peer = NULL;
        PeeringMap::iterator loc = peerings.find(key);
        if (loc == peerings.end()) {
            peer = new autogen::BgpPeeringAttributes();
            peerings.insert(make_pair(key, peer));
        } else {
            peer = loc->second;
        }
        // add uni-directional attributes for this session.
        autogen::BgpSessionAttributes *attrp =
            GetPeeringSessionAttribute(key, peer, session_id, config_uuid);
        attrp->Copy(iter->second.first);
        attrp->bgp_router = left;
    }

    // generate the links.
    // merging uni-directional attributes into a common attribute when they
    // are the same.
    for (PeeringMap::iterator iter = peerings.begin(); iter != peerings.end();
         ++iter) {
        autogen::BgpPeeringAttributes *peer = iter->second;
        MaybeMergeBidirectionalSessionParams(peer);
        string left(instance + ':'), right(instance  + ':');
        left.append(iter->first.first);
        right.append(iter->first.second);
        MapObjectLinkAttr("bgp-router", left, "bgp-router", right,
                          "bgp-peering", peer, requests);
    }
}

static void RemovePeeringLinks(const string &instance,
                               const SessionMap &sessions,
                               BgpConfigParser::RequestList *requests) {
    set<pair<string, string> > key_set;

    for (SessionMap::const_iterator iter = sessions.begin();
         iter != sessions.end(); ++iter) {
        const string &left = iter->first.first;
        const string &right = iter->first.second;
        pair<string, string> key;
        if (left <= right) {
            key = make_pair(left, right);
        } else {
            key = make_pair(right, left);
        }
        if (key_set.count(key) > 0) {
            continue;
        }
        key_set.insert(key);
        string id_left(instance + ':'), id_right(instance  + ':');
        id_left.append(key.first);
        id_right.append(key.second);

        MapObjectUnlink("bgp-router", id_left, "bgp-router", id_right,
                        "bgp-peering", requests);
    }
}

static bool ParseSession(const string &identifier, const xml_node &node,
                         SessionMap *sessions) {
    autogen::BgpSessionAttributes attr;
    attr.Clear();
    xml_attribute to = node.attribute("to");
    assert(to);
    assert(attr.XmlParse(node));

    string to_value = to.value();
    string to_name, uuid;
    size_t pos = to_value.find(':');
    if (pos == string::npos) {
        to_name = to_value;
    } else {
        to_name = to_value.substr(0, pos);
        uuid = string(to_value, pos + 1);
    }

    sessions->insert(
        make_pair(make_pair(identifier, to_name), make_pair(attr, uuid)));
    return true;
}

static bool ParseServiceChain(const string &instance, const xml_node &node,
                              bool add_change, const string &sc_info,
                              BgpConfigParser::RequestList *requests) {
    auto_ptr<autogen::ServiceChainInfo> property(
        new autogen::ServiceChainInfo());
    property->sc_head = true;
    assert(property->XmlParse(node));

    if (add_change) {
        MapObjectSetProperty("routing-instance", instance,
            sc_info, property.release(), requests);
    } else {
        MapObjectClearProperty("routing-instance", instance,
            sc_info, requests);
    }

    return true;
}

static bool ParseInstanceRouteAggregate(const string &instance,
    const xml_node &node, bool add_change,
    BgpConfigParser::RequestList *requests) {

    xml_attribute to = node.attribute("to");
    assert(to);
    string aggregate_name = to.value();
    if (add_change) {
        MapObjectLinkAttr("routing-instance", instance,
            "route-aggregate", aggregate_name,
            "route-aggregate-routing-instance", NULL, requests);
    } else {
        MapObjectUnlink("routing-instance", instance,
            "route-aggregate", aggregate_name,
            "route-aggregate-routing-instance", requests);
    }

    return true;
}

static bool ParseInstanceRoutingPolicy(const string &instance,
    const xml_node &node, bool add_change,
    BgpConfigParser::RequestList *requests) {

    xml_attribute to = node.attribute("to");
    assert(to);
    string policy_name = to.value();
    auto_ptr<autogen::RoutingPolicyType> attr(
        new autogen::RoutingPolicyType());
    assert(attr->XmlParse(node));
    if (add_change) {
        MapObjectLinkAttr("routing-instance", instance,
            "routing-policy", policy_name,
            "routing-policy-routing-instance", attr.release(), requests);
    } else {
        MapObjectUnlink("routing-instance", instance,
            "routing-policy", policy_name,
            "routing-policy-routing-instance", requests);
    }

    return true;
}

static bool ParseStaticRoute(const string &instance, const xml_node &node,
                              bool add_change,
                              BgpConfigParser::RequestList *requests) {
    auto_ptr<autogen::StaticRouteEntriesType> property(
        new autogen::StaticRouteEntriesType());
    assert(property->XmlParse(node));

    if (add_change) {
        MapObjectSetProperty("routing-instance", instance,
            "static-route-entries", property.release(), requests);
    } else {
        MapObjectClearProperty("routing-instance", instance,
            "static-route-entries", requests);
    }

    return true;
}

static bool ParseInstanceHasPnf(const string &instance, const xml_node &node,
    bool add_change, BgpConfigParser::RequestList *requests) {
    auto_ptr<autogen::RoutingInstance::OolProperty> property(
        new autogen::RoutingInstance::OolProperty);
    property->data = (string(node.child_value()) == "true");
    if (add_change) {
        MapObjectSetProperty("routing-instance", instance,
            "routing-instance-has-pnf", property.release(), requests);
    } else {
        MapObjectClearProperty("routing-instance", instance,
            "routing-instance-has-pnf", requests);
    }

    return true;
}

static bool ParseBgpRouter(const string &instance, const xml_node &node,
                           bool add_change, string *nodename,
                           SessionMap *sessions,
                           BgpConfigParser::RequestList *requests) {
    auto_ptr<autogen::BgpRouterParams> property(
        new autogen::BgpRouterParams());
    xml_attribute name = node.attribute("name");
    assert(name);
    string identifier = name.value();
    assert(property->XmlParse(node));

    if (property->autonomous_system == 0) {
        property->autonomous_system =
            BgpConfigManager::kDefaultAutonomousSystem;
    }
    if (property->identifier.empty()) {
        property->identifier = property->address;
    }

    bool has_sessions = false;
    for (xml_node xsession = node.child("session"); xsession;
         xsession = xsession.next_sibling("session")) {
        has_sessions = true;
        ParseSession(identifier, xsession, sessions);
    }

    string fqname(instance + ":" + identifier);
    if (!has_sessions) {
        *nodename = fqname;
    }

    string subcluster_name;
    if (node.child("sub-cluster")) {
        xml_attribute sc = node.child("sub-cluster").attribute("name");
        subcluster_name = sc.value();
        assert(!subcluster_name.empty());
    }

    if (add_change) {
        MapObjectLink("routing-instance", instance,
            "bgp-router", fqname, "instance-bgp-router", requests);
        MapObjectSetProperty("bgp-router", fqname,
            "bgp-router-parameters", property.release(), requests);
        if (!subcluster_name.empty()) {
            MapObjectLink("bgp-router", fqname, "sub-cluster", subcluster_name,
                          "bgp-router-sub-cluster", requests);
        }
    } else {
        MapObjectClearProperty("bgp-router", fqname,
            "bgp-router-parameters", requests);
        MapObjectUnlink("routing-instance", instance,
            "bgp-router", fqname, "instance-bgp-router", requests);
        if (!subcluster_name.empty()) {
            MapObjectUnlink("bgp-router", fqname, "sub-cluster",
                    subcluster_name, "bgp-router-sub-cluster", requests);
        }
    }

    return true;
}

// Creates a full-mesh of links between all the routers that have been
// defined.
static void AddNeighborMesh(const list<string> &routers,
                            BgpConfigParser::RequestList *requests) {
    for (list<string>::const_iterator iter = routers.begin();
         iter != routers.end(); ) {
        const string &left = *iter;
        ++iter;
        for (list<string>::const_iterator tgt = iter;
             tgt != routers.end(); ++tgt) {
            MapObjectLink("bgp-router", left, "bgp-router", *tgt, "bgp-peering",
                         requests);
        }
    }
}

static void DeleteNeighborMesh(const list<string> &routers,
                               BgpConfigParser::RequestList *requests) {
    for (list<string>::const_iterator iter = routers.begin();
         iter != routers.end(); ) {
        const string &left = *iter;
        ++iter;
        for (list<string>::const_iterator tgt = iter;
             tgt != routers.end(); ++tgt) {
            MapObjectUnlink("bgp-router", left, "bgp-router", *tgt,
                "bgp-peering", requests);
        }
    }
}

static bool ParseInstanceTarget(const string &instance, const xml_node &node,
                                bool add_change,
                                BgpConfigParser::RequestList *requests) {
    string rtarget(node.child_value());
    boost::trim(rtarget);
    boost::system::error_code parse_err;
    RouteTarget::FromString(rtarget, &parse_err);
    assert(!parse_err);

    auto_ptr<autogen::InstanceTargetType> params(
        new autogen::InstanceTargetType());
    assert(params->XmlParse(node));

    if (add_change) {
        MapObjectLinkAttr("routing-instance", instance, "route-target", rtarget,
            "instance-target", params.release(), requests);
    } else {
        MapObjectUnlink("routing-instance", instance, "route-target", rtarget,
            "instance-target", requests);
    }

    return true;
}

static bool ParseInstanceVirtualNetwork(const string &instance,
    const xml_node &node, bool add_change,
    BgpConfigParser::RequestList *requests) {
    string vn_name = node.child_value();
    if (add_change) {
        MapObjectLink("routing-instance", instance,
            "virtual-network", vn_name,
            "virtual-network-routing-instance", requests);
    } else {
        MapObjectUnlink("routing-instance", instance,
            "virtual-network", vn_name,
            "virtual-network-routing-instance", requests);
    }

    return true;
}

}  // namespace

BgpConfigParser::BgpConfigParser(DB *db)
        : db_(db) {
}

bool BgpConfigParser::ParseRoutingInstance(const xml_node &parent,
                                           bool add_change,
                                           RequestList *requests) const {
    string instance(parent.attribute("name").value());
    assert(!instance.empty());

    SessionMap sessions;
    list<string> routers;

    for (xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        if (strcmp(node.name(), "bgp-router") == 0) {
            string router_name;
            ParseBgpRouter(instance, node, add_change, &router_name,
                           &sessions, requests);
            if (!router_name.empty()) {
                routers.push_back(router_name);
            }
        } else if (strcmp(node.name(), "vrf-target") == 0) {
            ParseInstanceTarget(instance, node, add_change, requests);
        } else if (strcmp(node.name(), "virtual-network") == 0) {
            ParseInstanceVirtualNetwork(instance, node, add_change, requests);
        } else if (strcmp(node.name(), "service-chain-info") == 0) {
            ParseServiceChain(instance, node, add_change,
                              "service-chain-information", requests);
        } else if (strcmp(node.name(), "ipv6-service-chain-info") == 0) {
            ParseServiceChain(instance, node, add_change,
                              "ipv6-service-chain-information", requests);
        } else if (strcmp(node.name(), "evpn-service-chain-info") == 0) {
            ParseServiceChain(instance, node, add_change,
                              "evpn-service-chain-information", requests);
        } else if (strcmp(node.name(), "evpn-ipv6-service-chain-info") == 0) {
            ParseServiceChain(instance, node, add_change,
                              "evpn-ipv6-service-chain-information", requests);
        } else if (strcmp(node.name(), "route-aggregate") == 0) {
            ParseInstanceRouteAggregate(instance, node, add_change, requests);
        } else if (strcmp(node.name(), "routing-policy") == 0) {
            ParseInstanceRoutingPolicy(instance, node, add_change, requests);
        } else if (strcmp(node.name(), "static-route-entries") == 0) {
            ParseStaticRoute(instance, node, add_change, requests);
        } else if (strcmp(node.name(), "routing-instance-has-pnf") == 0) {
            ParseInstanceHasPnf(instance, node, add_change, requests);
        }
    }

    if (add_change) {
        BuildPeeringLinks(instance, sessions, requests);
        // Generate a full mesh of peering sessions for neighbors that do not
        // specify session attributes.
        AddNeighborMesh(routers, requests);
    } else {
        RemovePeeringLinks(instance, sessions, requests);
        DeleteNeighborMesh(routers, requests);
    }
    return true;
}

bool BgpConfigParser::ParseVirtualNetwork(const xml_node &node,
                                          bool add_change,
                                          RequestList *requests) const {
    // vn name
    string vn_name(node.attribute("name").value());
    assert(!vn_name.empty());

    auto_ptr<autogen::VirtualNetwork::OolProperty> pbb_property(
        new autogen::VirtualNetwork::OolProperty);
    pbb_property->data = false;

    if (node.attribute("pbb-evpn-enable")) {
        pbb_property->data =
            (string(node.attribute("pbb-evpn-enable").value()) == "true");
    }

    auto_ptr<autogen::VirtualNetworkType> property(
        new autogen::VirtualNetworkType());
    assert(property->XmlParse(node));

    if (add_change) {
        MapObjectSetProperty("virtual-network", vn_name,
            "virtual-network-properties", property.release(), requests);
        MapObjectSetProperty("virtual-network", vn_name,
            "pbb-evpn-enable", pbb_property.release(), requests);
    } else {
        MapObjectClearProperty("virtual-network", vn_name,
            "virtual-network-properties", requests);
        MapObjectClearProperty("virtual-network", vn_name,
            "pbb-evpn-enable", requests);
    }

    return true;
}

bool BgpConfigParser::ParseSubCluster(const xml_node &node,
                                      bool add_change,
                                      RequestList *requests) const {
    string subcluster_name(node.attribute("name").value());
    assert(!subcluster_name.empty());

    auto_ptr<autogen::SubCluster::StringProperty> subcluster_property(
        new autogen::SubCluster::StringProperty);

    if (node.child("sub-cluster-asn"))
        subcluster_property->data = string(node.child_value("sub-cluster-asn"));

    if (add_change) {
        MapObjectSetProperty("sub-cluster", subcluster_name,
            "sub-cluster-asn", subcluster_property.release(), requests);
    } else {
        MapObjectClearProperty("sub-cluster", subcluster_name,
                               "sub-cluster-asn", requests);
    }

    auto_ptr<autogen::SubCluster::NtProperty> id_property(
        new autogen::SubCluster::NtProperty);
    if (node.child("sub-cluster-id")) {
        std::stringstream sub_cluster_id(
                string(node.child_value("sub-cluster-id")));
        sub_cluster_id >> id_property->data;
    }
    if (add_change) {
        MapObjectSetProperty("sub-cluster", subcluster_name,
            "sub-cluster-id", id_property.release(), requests);
    } else {
        MapObjectClearProperty("sub-cluster", subcluster_name,
                               "sub-cluster-id", requests);
    }
    return true;
}

bool BgpConfigParser::ParseRouteAggregate(const xml_node &node,
                                          bool add_change,
                                          RequestList *requests) const {
    // policy name
    string aggregate_name(node.attribute("name").value());
    assert(!aggregate_name.empty());


    for (xml_node child = node.first_child(); child;
         child = child.next_sibling()) {
        if (strcmp(child.name(), "aggregate-route-entries") == 0) {
            if (add_change) {
                auto_ptr<autogen::RouteListType>
                    aggregate_routes(new autogen::RouteListType());
                assert(aggregate_routes->XmlParse(child));
                MapObjectSetProperty("route-aggregate", aggregate_name,
                                     "aggregate-route-entries",
                                     aggregate_routes.release(), requests);
            } else {
                MapObjectClearProperty("route-aggregate", aggregate_name,
                                       "aggregate-route-entries", requests);
            }
        } else if (strcmp(child.name(), "nexthop") == 0) {
            if (add_change) {
                string nexthop = child.child_value();

                autogen::RouteAggregate::StringProperty *nexthop_property =
                    new autogen::RouteAggregate::StringProperty();
                nexthop_property->data = nexthop;
                MapObjectSetProperty("route-aggregate", aggregate_name,
                                     "aggregate-route-nexthop",
                                     nexthop_property, requests);
            } else {
                MapObjectClearProperty("route-aggregate", aggregate_name,
                                       "aggregate-route-nexthop", requests);
            }
        }
    }

    return true;
}

bool BgpConfigParser::ParseRoutingPolicy(const xml_node &node,
                                          bool add_change,
                                          RequestList *requests) const {
    // policy name
    string policy_name(node.attribute("name").value());
    assert(!policy_name.empty());

    auto_ptr<autogen::PolicyStatementType> policy_statement(
        new autogen::PolicyStatementType());
    assert(policy_statement->XmlParse(node));

    if (add_change) {
        MapObjectSetProperty("routing-policy", policy_name,
            "routing-policy-entries", policy_statement.release(), requests);
    } else {
        MapObjectClearProperty("routing-policy", policy_name,
            "routing-policy-entries", requests);
    }

    return true;
}

bool BgpConfigParser::ParseGlobalSystemConfig(const xml_node &node,
                                              bool add_change,
                                              RequestList *requests) const {
    for (xml_node child = node.first_child(); child;
            child = child.next_sibling()) {
        if (strcmp(child.name(), "graceful-restart-parameters") == 0) {
            auto_ptr<autogen::GracefulRestartParametersType> gr_config(
                    new autogen::GracefulRestartParametersType());
            assert(gr_config->XmlParse(child));

            if (add_change) {
                MapObjectSetProperty("global-system-config", "",
                    "graceful-restart-parameters",
                    gr_config.release(), requests);
            } else {
                MapObjectClearProperty("global-system-config", "",
                                       "graceful-restart-parameters", requests);
            }
        }
        if (strcmp(child.name(), "bgpaas-parameters") == 0) {
            auto_ptr<autogen::BGPaaServiceParametersType> bgpaas_config(
                    new autogen::BGPaaServiceParametersType());
            assert(bgpaas_config->XmlParse(child));

            if (add_change) {
                MapObjectSetProperty("global-system-config", "",
                    "bgpaas-parameters", bgpaas_config.release(), requests);
            } else {
                MapObjectClearProperty("global-system-config", "",
                                       "bgpaas-parameters", requests);
            }
        }
        if (strcmp(child.name(), "bgp-always-compare-med") == 0) {
            auto_ptr<autogen::GlobalSystemConfig::OolProperty> property(
                new autogen::GlobalSystemConfig::OolProperty);
            property->data = (string(child.child_value()) == "true");
            if (add_change) {
                MapObjectSetProperty("global-system-config", "",
                    "bgp-always-compare-med", property.release(), requests);
            } else {
                MapObjectClearProperty("global-system-config", "",
                    "bgp-always-compare-med", requests);
            }
        }
        if (strcmp(child.name(), "enable-4byte-as") == 0) {
            auto_ptr<autogen::GlobalSystemConfig::OolProperty> property(
                new autogen::GlobalSystemConfig::OolProperty);
            property->data = (string(child.child_value()) == "true");
            if (add_change) {
                MapObjectSetProperty("global-system-config", "",
                    "enable-4byte-as", property.release(), requests);
            } else {
                MapObjectClearProperty("global-system-config", "",
                    "enable-4byte-as", requests);
            }
        }
        if (strcmp(child.name(), "rd-cluster-seed") == 0) {
            auto_ptr<autogen::GlobalSystemConfig::NtProperty> property(
                new autogen::GlobalSystemConfig::NtProperty);
            property->data = atoi(child.child_value());
            if (add_change) {
                MapObjectSetProperty("global-system-config", "",
                    "rd-cluster-seed", property.release(), requests);
            } else {
                MapObjectClearProperty("global-system-config", "",
                    "rd-cluster-seed", requests);
            }
        }
    }
    return true;
}

bool BgpConfigParser::ParseGlobalQosConfig(const xml_node &node,
                                           bool add_change,
                                           RequestList *requests) const {
    for (xml_node child = node.first_child(); child;
            child = child.next_sibling()) {
        if (strcmp(child.name(), "control-traffic-dscp") == 0) {
            auto_ptr<autogen::ControlTrafficDscpType> cfg(
                    new autogen::ControlTrafficDscpType());
            assert(cfg->XmlParse(child));

            if (add_change) {
                MapObjectSetProperty("global-qos-config", "",
                    "control-traffic-dscp", cfg.release(), requests);
            } else {
                MapObjectClearProperty("global-qos-config", "",
                                       "control-traffic-dscp", requests);
            }
        }
    }
    return true;
}

bool BgpConfigParser::ParseConfig(const xml_node &root, bool add_change,
                                  RequestList *requests) const {
    SessionMap sessions;
    list<string> routers;

    for (xml_node node = root.first_child(); node; node = node.next_sibling()) {
        if (strcmp(node.name(), "bgp-router") == 0) {
            string router_name;
            ParseBgpRouter(BgpConfigManager::kMasterInstance, node, add_change,
                           &router_name, &sessions, requests);
            if (!router_name.empty()) {
                routers.push_back(router_name);
            }
        }
        if (strcmp(node.name(), "routing-instance") == 0) {
            ParseRoutingInstance(node, add_change, requests);
        }
        if (strcmp(node.name(), "virtual-network") == 0) {
            ParseVirtualNetwork(node, add_change, requests);
        }
        if (strcmp(node.name(), "sub-cluster") == 0) {
            ParseSubCluster(node, add_change, requests);
        }
        if (strcmp(node.name(), "route-aggregate") == 0) {
            ParseRouteAggregate(node, add_change, requests);
        }
        if (strcmp(node.name(), "routing-policy") == 0) {
            ParseRoutingPolicy(node, add_change, requests);
        }
        if (strcmp(node.name(), "global-system-config") == 0) {
            ParseGlobalSystemConfig(node, add_change, requests);
        }
        if (strcmp(node.name(), "global-qos-config") == 0) {
            ParseGlobalQosConfig(node, add_change, requests);
        }
    }

    if (add_change) {
        BuildPeeringLinks(BgpConfigManager::kMasterInstance, sessions,
                          requests);
        // Generate a full mesh of peering sessions for neighbors that do not
        // specify session attributes.
        AddNeighborMesh(routers, requests);
    } else {
        RemovePeeringLinks(BgpConfigManager::kMasterInstance, sessions,
                           requests);
        DeleteNeighborMesh(routers, requests);
    }

    return true;
}

bool BgpConfigParser::Parse(const string &content)  {
    istringstream sstream(content);
    xml_document xdoc;
    xml_parse_result result = xdoc.load(sstream);
    assert(result);

    RequestList requests;
    for (xml_node node = xdoc.first_child(); node; node = node.next_sibling()) {
        const char *oper = node.name();
        assert((strcmp(oper, "config") == 0) || (strcmp(oper, "delete") == 0));
        bool add_change = (strcmp(oper, "config") == 0);
        assert(ParseConfig(node, add_change, &requests));
    }

    while (!requests.empty()) {
        auto_ptr<DBRequest> req(requests.front());
        requests.pop_front();

        IFMapTable::RequestKey *key =
                static_cast<IFMapTable::RequestKey *>(req->key.get());

        IFMapTable *table = IFMapTable::FindTable(db_, key->id_type);
        assert(table);
        table->Enqueue(req.get());
    }

    return true;
}

string BgpConfigParser::session_uuid(const string &left, const string &right,
                                     int index) {
    boost::uuids::nil_generator nil;
    boost::uuids::name_generator gen(nil());
    boost::uuids::uuid uuid;
    ostringstream oss;
    oss << left << ":" << right << ":" << index;
    uuid = gen(oss.str());
    return boost::uuids::to_string(uuid);
}
