/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "dns_config_parser.h"

#include <sstream>
#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/uuid/name_generator.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml/pugixml.hpp>
#include "base/logging.h"
#include "bind/bind_util.h"
#include "cfg/dns_config.h"
#include "ifmap/ifmap_server_table.h"

using namespace std;
using namespace pugi;

namespace {

static void MapObjectLink(const string &ltype, const string &lname,
                         const string &rtype, const string &rname,
                         const string &linkname,
                         DnsConfigParser::RequestList *requests,
                         DBRequest::DBOperation oper) {
    DBRequest *request = new DBRequest;
    request->oper = oper;
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
                              DnsConfigParser::RequestList *requests,
                              DBRequest::DBOperation oper) {
    DBRequest *request = new DBRequest;
    request->oper = oper;
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

static void SetData(const string &identifier, const string &id_type,
                    const string &metadata, AutogenProperty *params,
                    DnsConfigParser::RequestList *requests) {
    DBRequest *request = new DBRequest;
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = id_type;
    key->id_name = identifier;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = metadata;
    data->content.reset(params);
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    requests->push_back(request);
}

static void ClearData(const string &identifier, const string &id_type,
                      const string &metadata, 
                      DnsConfigParser::RequestList *requests) {
    DBRequest *request = new DBRequest;
    request->oper = DBRequest::DB_ENTRY_DELETE;
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = id_type;
    key->id_name = identifier;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = metadata;
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    requests->push_back(request);
}

static bool ParseVnNetworkIpam(const xml_node &node, bool add_change, 
                               DnsConfigParser::RequestList *requests) {
    xml_attribute name_attr = node.attribute("name");
    xml_attribute ipam_attr = node.attribute("ipam");
    xml_attribute vn_attr = node.attribute("vn");
    string name;
    string ipam;
    string vn;

    if (name_attr)
        name = name_attr.value();
    if (ipam_attr)
        ipam = ipam_attr.value();
    if (vn_attr)
        vn = vn_attr.value();

    auto_ptr<autogen::VnSubnetsType> params(new autogen::VnSubnetsType());
    if (!params->XmlParse(node)) {
        assert(0);
    }

    if (add_change) {
        MapObjectLinkAttr("virtual-network", vn, "network-ipam", ipam, 
                          "virtual-network-network-ipam", params.release(), requests, 
                          DBRequest::DB_ENTRY_ADD_CHANGE);
    } else {
        MapObjectLinkAttr("virtual-network", vn, "network-ipam", ipam, 
                          "virtual-network-network-ipam", params.release(), requests,
                          DBRequest::DB_ENTRY_DELETE);
    }

    return true;
}

static bool ParseNetworkIpam(const xml_node &node, bool add_change, 
                             DnsConfigParser::RequestList *requests) {
    xml_attribute name = node.attribute("name");
    string identifier;
    if (name)
        identifier = name.value();

    auto_ptr<autogen::IpamType> params(new autogen::IpamType());
    if (!params->XmlParse(node)) {
        assert(0);
    }

    if (add_change) {
        SetData(identifier, "network-ipam", "network-ipam-mgmt", 
                params.release(), requests);
    } else {
        ClearData(identifier, "network-ipam", "network-ipam-mgmt", requests);
    }

    return true;
}

static bool ParseVirtualDNS(const xml_node &node, bool add_change, 
                            DnsConfigParser::RequestList *requests) {
    xml_attribute name = node.attribute("name");
    xml_attribute domain = node.attribute("domain");
    string identifier;
    string view;

    if (name)
        identifier = name.value();
    if (domain)
        view = domain.value();

    auto_ptr<autogen::VirtualDnsType> params(new autogen::VirtualDnsType());
    if (!params->XmlParse(node)) {
        assert(0);
    }

    if (add_change) {
        SetData(identifier, "virtual-DNS", "virtual-DNS-data",
                params.release(), requests);
        MapObjectLink("domain", view, "virtual-DNS", identifier, 
                      "domain-virtual-DNS", requests, 
                      DBRequest::DB_ENTRY_ADD_CHANGE);
    } else {
        ClearData(identifier, "virtual-DNS", "virtual-DNS-data", requests);
        MapObjectLink("domain", view, "virtual-DNS", identifier, 
                      "domain-virtual-DNS", requests,
                      DBRequest::DB_ENTRY_DELETE);
    }

    return true;
}

static bool ParseVirtualDNSRecord(const xml_node &node, bool add_change, 
                                  DnsConfigParser::RequestList *requests) {
    xml_attribute name = node.attribute("name");
    xml_attribute dns = node.attribute("dns");
    string identifier;
    string virtual_dns;

    if (name)
        identifier = name.value();
    if (dns)
        virtual_dns = dns.value();

    auto_ptr<autogen::VirtualDnsRecordType> params(new autogen::VirtualDnsRecordType());
    if (!params->XmlParse(node)) {
        assert(0);
    }

    if (add_change) {
        SetData(identifier, "virtual-DNS-record", "virtual-DNS-record-data",
                params.release(), requests);
        MapObjectLink("virtual-DNS", virtual_dns,
                      "virtual-DNS-record", identifier, 
                      "virtual-DNS-virtual-DNS-record", requests,
                      DBRequest::DB_ENTRY_ADD_CHANGE);
    } else {
        ClearData(identifier, "virtual-DNS-record", 
                  "virtual-DNS-record-data", requests);
        MapObjectLink("virtual-DNS", virtual_dns,
                        "virtual-DNS-record", identifier, 
                        "virtual-DNS-virtual-DNS-record", requests,
                        DBRequest::DB_ENTRY_DELETE);
    }

    return true;
}

static bool ParseGlobalQosConfig(const xml_node &node, bool add_change,
                                 DnsConfigParser::RequestList *requests) {
    xml_attribute name = node.attribute("name");
    string identifier;
    if (name)
        identifier = name.value();

    for (xml_node child = node.first_child(); child;
            child = child.next_sibling()) {
        if (strcmp(child.name(), "control-traffic-dscp") == 0) {
            auto_ptr<autogen::ControlTrafficDscpType> cfg(
                    new autogen::ControlTrafficDscpType());
            assert(cfg->XmlParse(child));

            if (add_change) {
                SetData(identifier, "global-qos-config",
                        "control-traffic-dscp", cfg.release(), requests);
            } else {
                ClearData(identifier, "global-qos-config",
                          "control-traffic-dscp", requests);
            }
        }
    }
    return true;
}

}  // namespace

DnsConfigParser::DnsConfigParser(DB *db) : db_(db) {
}

bool DnsConfigParser::ParseConfig(const xml_node &root, bool add_change,
                                  RequestList *requests) const {

    for (xml_node node = root.first_child(); node; node = node.next_sibling()) {
        if (strcmp(node.name(), "network-ipam") == 0) {
            ParseNetworkIpam(node, add_change, requests);
        }
        else if (strcmp(node.name(), "virtual-network-network-ipam") == 0) {
            ParseVnNetworkIpam(node, add_change, requests);
        }
        else if (strcmp(node.name(), "virtual-DNS") == 0) {
            ParseVirtualDNS(node, add_change, requests);
        }
        else if (strcmp(node.name(), "virtual-DNS-record") == 0) {
            ParseVirtualDNSRecord(node, add_change, requests);
        }
        else if (strcmp(node.name(), "global-qos-config") == 0) {
            ParseGlobalQosConfig(node, add_change, requests);
        }
    }

    return true;
}

bool DnsConfigParser::Parse(const std::string &content)  {
    istringstream sstream(content);
    xml_document xdoc;
    xml_parse_result result = xdoc.load(sstream);
    if (!result) {
        std::stringstream str;
        str << "Unable to load XML document. (status="
            << result.status << ", offset=" << result.offset << ")";
        DNS_TRACE(DnsError, str.str());
        return false;
    }

    RequestList requests;
    for (xml_node node = xdoc.first_child(); node; node = node.next_sibling()) {
        bool add_change;
        if (strcmp(node.name(), "config") == 0) {
            add_change = true;
        } else if (strcmp(node.name(), "delete") == 0) {
            add_change = false;
        } else {
            continue;
        }
        if (!ParseConfig(node, add_change, &requests)) {
            STLDeleteValues(&requests);
            return false;
        }
    }

    while (!requests.empty()) {
        auto_ptr<DBRequest> req(requests.front());
        requests.pop_front();

        IFMapTable::RequestKey *key =
                static_cast<IFMapTable::RequestKey *>(req->key.get());

        IFMapTable *table = IFMapTable::FindTable(db_, key->id_type);
        if (table == NULL) {
            continue;
        }
        table->Enqueue(req.get());
    }

    return true;
}
