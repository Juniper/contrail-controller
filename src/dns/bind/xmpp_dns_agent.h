/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _xmpp_dns_agent_h_
#define _xmpp_dns_agent_h_

extern SandeshTraceBufferPtr AgentXmppTraceBuf;
#define DNS_XMPP_TRACE(obj, arg)                                              \
do {                                                                          \
    std::ostringstream _str;                                                  \
    _str << arg;                                                              \
    obj::TraceMsg(AgentXmppTraceBuf, __FILE__, __LINE__, _str.str());         \
} while (false)                                                               \

class XmppChannel;

namespace pugi {
    class xml_node;
}  // namespace pugi

struct DnsItem;
struct DnsUpdateData;

class DnsAgentXmpp {
public:
    static const uint16_t max_dns_xmpp_msg_len = 8192;

    enum XmppType {
        Update,
        UpdateResponse,
        DnsQuery,
        DnsQueryResponse
    };

    static std::size_t DnsAgentXmppEncode(XmppChannel *channel, XmppType type, 
                                          uint32_t trans_id, uint32_t resp_code, 
                                          DnsUpdateData *xmpp_data,
                                          uint8_t *data);
    static bool DnsAgentXmppDecode(const pugi::xml_node dns, 
                                   XmppType &type, uint32_t &xid,
                                   uint16_t &resp_code, DnsUpdateData *data);

private:
    static void EncodeDnsData(pugi::xml_node *node, DnsUpdateData *xmpp_data);
    static void EncodeDnsItem(pugi::xml_node *node, DnsItem &entry);
    static bool DecodeDns(const pugi::xml_node node, XmppType &type, 
                          bool is_resp, DnsUpdateData *data);
    static bool DecodeDnsItems(const pugi::xml_node dnsdata, 
                               DnsUpdateData *data);
};

#endif // _xmpp_dns_agent_h_
