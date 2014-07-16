/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DNS_XMPP_H__
#define __DNS_XMPP_H__

#include <map>
#include <string>

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>

#include <bind/bind_util.h>
#include <xmpp/xmpp_channel.h>

class XmppChannel;
class Agent;
class VmInterface;

class AgentDnsXmppChannel {
public:
    // Packet module is optional. Callback function to update the flow stats
    // for ACL. The callback is defined to avoid linking error
    // when flow is not enabled
    typedef boost::function<void(DnsUpdateData*, DnsAgentXmpp::XmppType,
                                 VmInterface const*, bool)> DnsMessageHandler;
    typedef boost::function<void(AgentDnsXmppChannel*)> DnsXmppEventHandler;

    explicit AgentDnsXmppChannel(Agent *agent, XmppChannel *channel, 
                                 std::string xmpp_server, uint8_t xs_idx);
    virtual ~AgentDnsXmppChannel();

    virtual std::string ToString() const;
    virtual bool SendMsg(uint8_t *msg, std::size_t len);
    virtual void ReceiveMsg(const XmppStanza::XmppMessage *msg);
    std::string controller_ifmap_xmpp_server() { return xmpp_server_; }
    uint8_t GetXmppServerIdx() { return xs_idx_; }
    XmppChannel *GetXmppChannel() { return channel_; }
    void UpdateConnectionInfo(xmps::PeerState state);
    static void HandleXmppClientChannelEvent(AgentDnsXmppChannel *peer,
                                             xmps::PeerState state);
    static void set_dns_message_handler_cb(DnsMessageHandler cb);
    static void set_dns_xmpp_event_handler_cb(DnsXmppEventHandler cb);
    Agent *agent() const {return agent_;}
protected:
    virtual void WriteReadyCb(uint8_t *msg, 
                              const boost::system::error_code &ec);

private:
    void ReceiveInternal(const XmppStanza::XmppMessage *msg);
    XmppChannel *channel_;
    std::string xmpp_server_;
    uint8_t xs_idx_;
    static DnsMessageHandler dns_message_handler_cb_;
    static DnsXmppEventHandler dns_xmpp_event_handler_cb_;
    Agent *agent_;
};

#endif // __DNS_XMPP_H__
