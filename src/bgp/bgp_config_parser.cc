/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_config_parser.h"

#include <sstream>
#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/uuid/name_generator.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml/pugixml.hpp>
#include "base/logging.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/rtarget/rtarget_address.h"
#include "ifmap/ifmap_origin.h"
#include "ifmap/ifmap_server_table.h"

using namespace std;
using namespace pugi;

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
                              bool add_change, 
                              BgpConfigParser::RequestList *requests) {
    auto_ptr<autogen::ServiceChainInfo> property(
        new autogen::ServiceChainInfo());
    assert(property->XmlParse(node));

    if (add_change) {
        MapObjectSetProperty("routing-instance", instance,
            "service-chain-information", property.release(), requests);
    } else {
        MapObjectClearProperty("routing-instance", instance,
            "service-chain-information", requests);
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

    if (property->autonomous_system == 0)
        property->autonomous_system = BgpConfigManager::kDefaultAutonomousSystem;
    if (property->identifier.empty())
        property->identifier = property->address;

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

    if (add_change) {
        MapObjectSetProperty("bgp-router", fqname,
            "bgp-router-parameters", property.release(), requests);
        MapObjectLink("routing-instance", instance,
            "bgp-router", identifier, "instance-bgp-router", requests);
    } else {
        MapObjectClearProperty("bgp-router", fqname,
            "bgp-router-parameters", requests);
        MapObjectUnlink("routing-instance", instance,
            "bgp-router", identifier, "instance-bgp-router", requests);
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
            MapObjectUnlink("bgp-router", left, "bgp-router", *tgt, "bgp-peering",
                            requests);
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
            ParseServiceChain(instance, node, add_change, requests);
        } else if (strcmp(node.name(), "static-route-entries") == 0) {
            ParseStaticRoute(instance, node, add_change, requests);
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

    auto_ptr<autogen::VirtualNetworkType> property(
        new autogen::VirtualNetworkType());
    assert(property->XmlParse(node));

    if (add_change) {
        MapObjectSetProperty("virtual-network", vn_name,
            "virtual-network-properties", property.release(), requests);
    } else {
        MapObjectClearProperty("virtual-network", vn_name,
            "virtual-network-properties", requests);
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

bool BgpConfigParser::Parse(const std::string &content)  {
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
