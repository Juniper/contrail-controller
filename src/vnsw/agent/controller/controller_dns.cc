/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "base/logging.h"
#include "controller/controller_dns.h"
#include "xmpp/xmpp_channel.h"
#include "cmn/agent_cmn.h"
#include "pugixml/pugixml.hpp"
#include "xml/xml_pugi.h"
#include "services/dns_proto.h"
#include "bind/xmpp_dns_agent.h"

AgentDnsXmppChannel::AgentDnsXmppChannel(XmppChannel *channel, 
      std::string xmpp_server, uint8_t xs_idx) 
    : channel_(channel), xmpp_server_(xmpp_server), xs_idx_(xs_idx) {
    channel_->RegisterReceive(xmps::DNS, 
            boost::bind(&AgentDnsXmppChannel::ReceiveInternal, this, _1));
}

AgentDnsXmppChannel::~AgentDnsXmppChannel() {
    channel_->UnRegisterReceive(xmps::DNS);
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
            Agent::GetInstance()->GetDnsProto()->SendDnsUpdateIpc(
                                  xmpp_data.release(), xmpp_type, NULL);
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

void AgentDnsXmppChannel::HandleXmppClientChannelEvent(
        AgentDnsXmppChannel *peer, xmps::PeerState state) {
    if (state == xmps::READY) {
        if (Agent::GetInstance()->GetXmppDnsCfgServerIdx() == -1)
            Agent::GetInstance()->SetXmppDnsCfgServer(peer->xs_idx_);
        Agent::GetInstance()->GetDnsProto()->SendDnsUpdateIpc(peer);
    } else {
        if (Agent::GetInstance()->GetXmppDnsCfgServerIdx() == peer->xs_idx_) {
            Agent::GetInstance()->SetXmppDnsCfgServer(-1);
            uint8_t o_idx = ((peer->xs_idx_ == 0) ? 1 : 0);
            AgentDnsXmppChannel *o_chn = Agent::GetInstance()->GetAgentDnsXmppChannel(o_idx);
            if (o_chn && o_chn->GetXmppChannel() &&
                o_chn->GetXmppChannel()->GetPeerState() == xmps::READY)
                Agent::GetInstance()->SetXmppDnsCfgServer(o_idx);
        }
    }
}
