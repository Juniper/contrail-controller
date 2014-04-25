/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DNS_XMPP_H__
#define __DNS_XMPP_H__

#include <map>
#include <string>

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>

#include "xmpp/xmpp_channel.h"

class XmppChannel;
class Agent;

class AgentDnsXmppChannel {
public:
    explicit AgentDnsXmppChannel(Agent *agent, XmppChannel *channel, 
                                 std::string xmpp_server, uint8_t xs_idx);
    virtual ~AgentDnsXmppChannel();

    virtual std::string ToString() const;
    virtual bool SendMsg(uint8_t *msg, std::size_t len);
    virtual void ReceiveMsg(const XmppStanza::XmppMessage *msg);
    std::string GetXmppServer() { return xmpp_server_; }
    uint8_t GetXmppServerIdx() { return xs_idx_; }
    XmppChannel *GetXmppChannel() { return channel_; }
    static void HandleXmppClientChannelEvent(AgentDnsXmppChannel *peer,
                                             xmps::PeerState state);
    Agent *agent() const {return agent_;}
protected:
    virtual void WriteReadyCb(uint8_t *msg, 
                              const boost::system::error_code &ec);

private:
    void ReceiveInternal(const XmppStanza::XmppMessage *msg);
    XmppChannel *channel_;
    std::string xmpp_server_;
    uint8_t xs_idx_;
    Agent *agent_;
};

#endif // __DNS_XMPP_H__
