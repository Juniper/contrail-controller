/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "base/logging.h"
#include "base/connection_info.h"
#include "controller/controller_dns.h"
#include "xmpp/xmpp_channel.h"
#include "cmn/agent_cmn.h"
#include "pugixml/pugixml.hpp"
#include "xml/xml_pugi.h"
#include "bind/xmpp_dns_agent.h"

using process::ConnectionState;
using process::ConnectionType;
using process::ConnectionStatus;

AgentDnsXmppChannel::DnsMessageHandler AgentDnsXmppChannel::dns_message_handler_cb_;
AgentDnsXmppChannel::DnsXmppEventHandler AgentDnsXmppChannel::dns_xmpp_event_handler_cb_;

AgentDnsXmppChannel::AgentDnsXmppChannel(Agent *agent,
      std::string xmpp_server, uint8_t xs_idx)
    : channel_(NULL), xmpp_server_(xmpp_server), xs_idx_(xs_idx),
    agent_(agent) {
}

AgentDnsXmppChannel::~AgentDnsXmppChannel() {
    if (channel_) {
        channel_->UnRegisterReceive(xmps::DNS);
    }
}

void AgentDnsXmppChannel::RegisterXmppChannel(XmppChannel *channel) {
    if (channel == NULL)
        return;

    channel_ = channel;
    channel->RegisterReceive(xmps::DNS,
            boost::bind(&AgentDnsXmppChannel::ReceiveInternal, this, _1));
}

bool AgentDnsXmppChannel::SendMsg(uint8_t *msg, std::size_t len) {
    if (!channel_ || channel_->GetPeerState() != xmps::READY) 
        return false;

    return channel_->Send((const uint8_t *)msg, len, xmps::DNS, 
            boost::bind(&AgentDnsXmppChannel::WriteReadyCb, this, msg, _1));
}

void AgentDnsXmppChannel::ReceiveMsg(const XmppStanza::XmppMessage *msg) {
    if (msg && msg->type == XmppStanza::IQ_STANZA) {
        XmlBase *impl = msg->dom.get();
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);
        pugi::xml_node node = pugi->FindNode("dns");
        DnsAgentXmpp::XmppType xmpp_type;
        uint32_t xid;
        uint16_t code;
        std::auto_ptr<DnsUpdateData> xmpp_data(new DnsUpdateData);
        if (DnsAgentXmpp::DnsAgentXmppDecode(node, xmpp_type, xid, 
                                             code, xmpp_data.get())) {
            dns_message_handler_cb_(xmpp_data.release(), xmpp_type, NULL,
                                    false);
        }
    }
}

void AgentDnsXmppChannel::ReceiveInternal(const XmppStanza::XmppMessage *msg) {
    ReceiveMsg(msg);
}

std::string AgentDnsXmppChannel::ToString() const {
    return channel_->ToString();
}

void AgentDnsXmppChannel::WriteReadyCb(uint8_t *msg, 
                                       const boost::system::error_code &ec) {
    delete [] msg;
}

void AgentDnsXmppChannel::HandleXmppClientChannelEvent(AgentDnsXmppChannel *peer,
                                                       xmps::PeerState state) {
    Agent *agent = peer->agent();
    peer->UpdateConnectionInfo(state);
    if (state == xmps::READY) {
        if (agent->dns_xmpp_server_index() == -1)
            agent->set_dns_xmpp_server_index(peer->xs_idx_);
        peer->dns_xmpp_event_handler_cb_(peer);
    } else {
        if (agent->dns_xmpp_server_index() == peer->xs_idx_) {
            agent->set_dns_xmpp_server_index(-1);
            uint8_t o_idx = ((peer->xs_idx_ == 0) ? 1 : 0);
            AgentDnsXmppChannel *o_chn = agent->dns_xmpp_channel(o_idx);
            if (o_chn && o_chn->GetXmppChannel() &&
                o_chn->GetXmppChannel()->GetPeerState() == xmps::READY)
                agent->set_dns_xmpp_server_index(o_idx);
        }
    }
}

void AgentDnsXmppChannel::set_dns_message_handler_cb(DnsMessageHandler cb) {
    dns_message_handler_cb_ = cb;
}

void AgentDnsXmppChannel::set_dns_xmpp_event_handler_cb(DnsXmppEventHandler cb){
    dns_xmpp_event_handler_cb_ = cb;
}

void AgentDnsXmppChannel::UpdateConnectionInfo(xmps::PeerState state) {
    boost::asio::ip::tcp::endpoint ep;
    boost::system::error_code ec;
    std::string last_state_name;
    ep.address(boost::asio::ip::address::from_string(agent_->dns_server
                                                             (xs_idx_), ec));
    ep.port(agent_->dns_server_port(xs_idx_));
    const std::string name = agent_->xmpp_dns_server_prefix() +
                             ep.address().to_string();
    XmppChannel *xc = GetXmppChannel();
    if (xc) {
        last_state_name = xc->LastStateName();
    }
    if (state == xmps::READY) {
        agent_->connection_state()->Update(ConnectionType::XMPP, name,
                                           ConnectionStatus::UP, ep,
                                           last_state_name);
    } else {
        agent_->connection_state()->Update(ConnectionType::XMPP, name,
                                           ConnectionStatus::DOWN, ep,
                                           last_state_name);
    }
}
