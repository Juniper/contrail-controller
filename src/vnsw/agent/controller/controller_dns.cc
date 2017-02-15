/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "base/logging.h"
#include "base/connection_info.h"
#include "cmn/agent_cmn.h"
#include "controller/controller_dns.h"
#include "controller/controller_init.h"
#include "xmpp/xmpp_channel.h"
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
        channel_->UnRegisterWriteReady(xmps::DNS);
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
            boost::bind(&AgentDnsXmppChannel::WriteReadyCb, this, _1));
}

void AgentDnsXmppChannel::ReceiveMsg(const XmppStanza::XmppMessage *msg) {
    if (msg && msg->type == XmppStanza::IQ_STANZA) {
        std::auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
        XmlPugi *msg_pugi = reinterpret_cast<XmlPugi *>(msg->dom.get());
        pugi->LoadXmlDoc(msg_pugi->doc()); //Verify Xmpp message format 
        boost::shared_ptr<ControllerXmppData> data(new ControllerXmppData(xmps::DNS,
                                                                          xmps::UNKNOWN,
                                                                          xs_idx_,
                                                                          impl,
                                                                          true));
        agent_->controller()->Enqueue(data);
    }
}

void AgentDnsXmppChannel::ReceiveInternal(const XmppStanza::XmppMessage *msg) {
    ReceiveMsg(msg);
}

void AgentDnsXmppChannel::ReceiveDnsMessage(std::auto_ptr<XmlBase> impl) {
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
    pugi::xml_node node = pugi->FindNode("dns");
    DnsAgentXmpp::XmppType xmpp_type;
    uint32_t xid;
    uint16_t code;
    std::auto_ptr<DnsUpdateData> xmpp_data(new DnsUpdateData);
    if (DnsAgentXmpp::DnsAgentXmppDecode(node, xmpp_type, xid,
                                         code, xmpp_data.get())) {
        if (!dns_message_handler_cb_.empty())
            dns_message_handler_cb_(xmpp_data.release(), xmpp_type,
                                    NULL, false);
    }
}

std::string AgentDnsXmppChannel::ToString() const {
    return channel_->ToString();
}

void AgentDnsXmppChannel::WriteReadyCb(const boost::system::error_code &ec) {
}

void AgentDnsXmppChannel::XmppClientChannelEvent(AgentDnsXmppChannel *peer,
                                                 xmps::PeerState state) {
    std::auto_ptr<XmlBase> dummy_dom;
    boost::shared_ptr<ControllerXmppData> data(new ControllerXmppData(xmps::DNS,
                                                                      state,
                                                                      peer->GetXmppServerIdx(),
                                                                      dummy_dom,
                                                                      false));
    peer->agent()->controller()->Enqueue(data);
}

void AgentDnsXmppChannel::HandleXmppClientChannelEvent(AgentDnsXmppChannel *peer,
                                                       xmps::PeerState state) {
    peer->UpdateConnectionInfo(state);
    if (state == xmps::READY) {
        if (!peer->dns_xmpp_event_handler_cb_.empty())
            peer->dns_xmpp_event_handler_cb_(peer);
    }
}

void AgentDnsXmppChannel::set_dns_message_handler_cb(DnsMessageHandler cb) {
    dns_message_handler_cb_ = cb;
}

void AgentDnsXmppChannel::set_dns_xmpp_event_handler_cb(DnsXmppEventHandler cb){
    dns_xmpp_event_handler_cb_ = cb;
}

void AgentDnsXmppChannel::UpdateConnectionInfo(xmps::PeerState state) {
    if (agent_->connection_state() == NULL)
        return;

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
