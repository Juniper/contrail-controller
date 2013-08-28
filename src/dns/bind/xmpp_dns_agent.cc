/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "base/logging.h"
#include "xmpp/xmpp_init.h"
#include <pugixml/pugixml.hpp>
#include "xml/xml_pugi.h"
#include "bind/bind_util.h"
#include "bind/xmpp_dns_agent.h"

SandeshTraceBufferPtr AgentXmppTraceBuf(SandeshTraceBufferCreate("Xmpp", 500));

std::size_t
DnsAgentXmpp::DnsAgentXmppEncode(XmppChannel *channel,
                                 XmppType type,
                                 uint32_t trans_id, 
                                 uint32_t response_code,
                                 DnsUpdateData *xmpp_data,
                                 uint8_t *data) {
    if (!channel)
        return 0;

    std::auto_ptr<XmlBase> impl(XmppStanza::AllocXmppXmlImpl());
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
    std::string iq_type = 
        (type == DnsQuery) ? "get" : 
        (type == Update) ? "set" : "result";

    pugi->AddNode("iq", "");
    pugi->AddAttribute("type", iq_type);
    pugi->AddAttribute("from", channel->FromString());
    std::string to(channel->ToString());
    to += "/";
    to += XmppInit::kDnsPeer;
    pugi->AddAttribute("to", to);
    std::stringstream id;
    id << trans_id;
    pugi->AddAttribute("id", id.str());

    pugi->AddChildNode("dns", "");
    pugi->AddAttribute("transid", id.str());

    std::stringstream code;
    code << response_code;
    pugi::xml_node node;
    switch (type) {
        case Update:  
            pugi->AddChildNode("update", "");
            node = pugi->FindNode("update");
            break;

        case UpdateResponse:
            pugi->AddChildNode("response", "");
            pugi->AddAttribute("code", code.str());
            pugi->AddChildNode("update", "");
            node = pugi->FindNode("update");
            break;

        case DnsQuery: 
            pugi->AddChildNode("query", "");
            node = pugi->FindNode("query");
            break;

        case DnsQueryResponse:
            pugi->AddChildNode("response", "");
            pugi->AddAttribute("code", code.str());
            pugi->AddChildNode("query", "");
            node = pugi->FindNode("query");
            break;

        default:
            assert(0);
    }

    EncodeDnsData(&node, xmpp_data);
    return XmppProto::EncodeMessage(impl.get(), data, sizeof(data));
}
 
void 
DnsAgentXmpp::EncodeDnsData(pugi::xml_node *node, DnsUpdateData *xmpp_data) {
    pugi::xml_node node_c;

    node_c = node->append_child("virtual-dns");
    node_c.text().set(xmpp_data->virtual_dns.c_str());

    node_c = node->append_child("zone");
    node_c.text().set(xmpp_data->zone.c_str());

    for (DnsItems::iterator item = xmpp_data->items.begin();
         item != xmpp_data->items.end(); ++item) {
        node_c = node->append_child("entry");
        EncodeDnsItem(&node_c, *item);
    }
}

void 
DnsAgentXmpp::EncodeDnsItem(pugi::xml_node *node, DnsItem &item) {
    pugi::xml_node node_c;

    node_c = node->append_child("class");
    node_c.text().set(item.eclass);

    node_c = node->append_child("type");
    node_c.text().set(item.type);

    node_c = node->append_child("name");
    node_c.text().set(item.name.c_str());

    if (item.data != "") {
        node_c = node->append_child("data");
        node_c.text().set(item.data.c_str());
    }

    if (item.ttl != (uint32_t) -1) {
        node_c = node->append_child("ttl");
        node_c.text().set(item.ttl);
    }

    if (item.priority != (uint16_t) -1) {
        node_c = node->append_child("priority");
        node_c.text().set(item.priority);
    }
}

bool 
DnsAgentXmpp::DnsAgentXmppDecode(const pugi::xml_node dns, 
                                 XmppType &type, uint32_t &xid,
                                 uint16_t &resp_code, DnsUpdateData *data) {
    std::stringstream id(dns.attribute("transid").value());
    id >> xid;

    pugi::xml_node resp = dns.first_child();
    if (strcmp(resp.name(), "response") == 0) {
        std::stringstream code(resp.attribute("code").value());
        code >> resp_code;
        return DecodeDns(resp.first_child(), type, true, data);
    } else
        return DecodeDns(resp, type, false, data);
}

bool 
DnsAgentXmpp::DecodeDns(const pugi::xml_node node, 
                              XmppType &type, 
                              bool is_resp, 
                              DnsUpdateData *data) {
    if (strcmp(node.name(), "update") == 0) {
        type = is_resp ? UpdateResponse : Update;
        return DecodeDnsItems(node, data);
    } else if (strcmp(node.name(), "query") == 0) {
        type = is_resp ? DnsQueryResponse : DnsQuery;
        return DecodeDnsItems(node, data);
    }

    DNS_XMPP_TRACE(DnsXmppTrace, "Dns XMPP response : unknown tag : " << 
                   node.name());
    return false;
}

bool 
DnsAgentXmpp::DecodeDnsItems(const pugi::xml_node dnsdata, 
                                   DnsUpdateData *data) {
    for (pugi::xml_node node = dnsdata.first_child(); node; 
         node = node.next_sibling()) {
        if (strcmp(node.name(), "virtual-dns") == 0) {
            data->virtual_dns = node.child_value();
        } else if (strcmp(node.name(), "zone") == 0) {
            data->zone = node.child_value();
        } else if (strcmp(node.name(), "entry") == 0) {
            DnsItem item;
            for (pugi::xml_node entry = node.first_child(); entry; 
                 entry = entry.next_sibling()) {
                if (strcmp(entry.name(), "class") == 0) {
                    std::stringstream cl(entry.child_value());
                    cl >> item.eclass;
                } else if (strcmp(entry.name(), "type") == 0) {
                    std::stringstream type(entry.child_value());
                    type >> item.type;
                } else if (strcmp(entry.name(), "name") == 0) {
                    item.name = entry.child_value();
                } else if (strcmp(entry.name(), "data") == 0) {
                    item.data = entry.child_value();
                } else if (strcmp(entry.name(), "ttl") == 0) {
                    std::stringstream ttl(entry.child_value());
                    ttl >> item.ttl;
                } else if (strcmp(entry.name(), "priority") == 0) {
                    std::stringstream prio(entry.child_value());
                    prio >> item.priority;
                } else {
                    DNS_XMPP_TRACE(DnsXmppTrace, 
                                   "Dns XMPP response : unknown tag : " << 
                                   node.name());
                    return false;
                }
            }
            data->items.push_back(item);
        } else {
            DNS_XMPP_TRACE(DnsXmppTrace, "Dns XMPP response : unknown tag : " <<
                           node.name());
            return false;
        }
    }

    return true;
}
