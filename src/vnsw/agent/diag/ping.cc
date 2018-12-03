/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include "base/os.h"
#include "vr_defs.h"
#include "cmn/agent_cmn.h"
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/mirror_table.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "diag/diag_types.h"
#include "diag/diag_pkt_handler.h"
#include "diag/diag.h"
#include "diag/ping.h"

using namespace boost::posix_time;

Ping::Ping(const PingReq *ping_req, DiagTable *diag_table):
    DiagEntry(ping_req->get_source_ip(), ping_req->get_dest_ip(),
              ping_req->get_protocol(), ping_req->get_source_port(),
              ping_req->get_dest_port(), ping_req->get_vrf_name(),
              ping_req->get_interval() * 100, ping_req->get_count(), diag_table),
    data_len_(ping_req->get_packet_size()), context_(ping_req->context()),
    pkt_lost_count_(0) {

}

Ping::~Ping() {
}

void
Ping::FillAgentHeader(AgentDiagPktData *ad) {
    ad->op_ = htonl(AgentDiagPktData::DIAG_REQUEST);
    ad->key_ = htons(key_);
    ad->seq_no_ = htonl(seq_no_);
    ad->rtt_ = microsec_clock::universal_time();
    memset(ad->data_, 0, sizeof(ad->data_));
}

DiagPktHandler*
Ping::CreateTcpPkt(Agent *agent) {
    //Allocate buffer to hold packet
    if (sip_.is_v4()) {
        len_ = KPingTcpHdr + data_len_;
    } else {
        len_ = KPing6TcpHdr + data_len_;
    }
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(agent, len_,
                                                    PktHandler::DIAG, 0));
    uint8_t *msg = pkt_info->packet_buffer()->data();
    memset(msg, 0, len_);

    AgentDiagPktData *ad;
    if (sip_.is_v4()) {
        ad = (AgentDiagPktData *)(msg + KPingTcpHdr);
    } else {
        ad = (AgentDiagPktData *)(msg + KPing6TcpHdr);
    }

    FillAgentHeader(ad);
    DiagPktHandler *pkt_handler = new DiagPktHandler(diag_table_->agent(), pkt_info,
                                   *(diag_table_->agent()->event_manager())->io_service());

    //Update pointers to ethernet header, ip header and l4 header
    if (sip_.is_v4()) {
        pkt_info->UpdateHeaderPtr();
        pkt_handler->TcpHdr(htonl(sip_.to_v4().to_ulong()), sport_,
                            htonl(dip_.to_v4().to_ulong()), dport_, false, rand(),
                            data_len_ + sizeof(tcphdr));
        pkt_handler->IpHdr(data_len_ + sizeof(tcphdr) + sizeof(struct ip),
                           ntohl(sip_.to_v4().to_ulong()),
                           ntohl(dip_.to_v4().to_ulong()), IPPROTO_TCP,
                           DEFAULT_IP_ID, DEFAULT_IP_TTL);
        pkt_handler->EthHdr(agent->vhost_interface()->mac(),
                            agent->vrrp_mac(), ETHERTYPE_IP);
    }else {
        pkt_info->eth = (struct ether_header *)(pkt_info->pkt);
        pkt_info->ip6 = (struct ip6_hdr *)(pkt_info->eth + 1);
        pkt_info->transp.tcp = (struct tcphdr *)(pkt_info->ip6 + 1);
        pkt_handler->TcpHdr(data_len_ + sizeof(tcphdr),
                            (uint8_t *)sip_.to_v6().to_string().c_str(), sport_,
                            (uint8_t *)dip_.to_v6().to_string().c_str(), dport_,
                            false, rand(), IPPROTO_TCP);
        pkt_handler->Ip6Hdr(pkt_info->ip6,
                            data_len_ + sizeof(tcphdr) + sizeof(struct ip6_hdr),
                            IPPROTO_TCP, DEFAULT_IP_TTL,
                            sip_.to_v6().to_bytes().data(),
                            dip_.to_v6().to_bytes().data());
        pkt_handler->EthHdr(agent->vhost_interface()->mac(),
                            agent->vrrp_mac(), ETHERTYPE_IPV6);
    }

    return pkt_handler;
}

DiagPktHandler*
Ping::CreateUdpPkt(Agent *agent) {
    //Allocate buffer to hold packet
    if (sip_.is_v4()) {
        len_ = KPingUdpHdr + data_len_;
    } else {
        len_ = KPing6UdpHdr + data_len_;
    }
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(agent, len_,
                                                    PktHandler::DIAG, 0));
    uint8_t *msg = pkt_info->packet_buffer()->data();
    memset(msg, 0, len_);

    AgentDiagPktData *ad;
    if (sip_.is_v4()) {
        ad = (AgentDiagPktData *)(msg + KPingUdpHdr);
    } else {
        ad = (AgentDiagPktData *)(msg + KPing6UdpHdr);
    }

    FillAgentHeader(ad);

    DiagPktHandler *pkt_handler = new DiagPktHandler(diag_table_->agent(), pkt_info,
                                    *(diag_table_->agent()->event_manager())->io_service());

    //Update pointers to ethernet header, ip header and l4 header
    if (sip_.is_v4()) {
        pkt_info->UpdateHeaderPtr();
        pkt_handler->UdpHdr(data_len_+ sizeof(udphdr), sip_.to_v4().to_ulong(), sport_,
                            dip_.to_v4().to_ulong(), dport_);
        pkt_handler->IpHdr(data_len_ + sizeof(udphdr) + sizeof(struct ip),
                           ntohl(sip_.to_v4().to_ulong()),
                           ntohl(dip_.to_v4().to_ulong()), IPPROTO_UDP,
                           DEFAULT_IP_ID, DEFAULT_IP_TTL);
        pkt_handler->EthHdr(agent->vhost_interface()->mac(),
                            agent->vrrp_mac(), ETHERTYPE_IP);
    } else {
        pkt_info->eth = (struct ether_header *)(pkt_info->pkt);
        pkt_info->ip6 = (struct ip6_hdr *)(pkt_info->eth + 1);
        pkt_info->transp.udp = (struct udphdr *)(pkt_info->ip6 + 1);
        pkt_handler->UdpHdr(data_len_ + sizeof(udphdr),
                            sip_.to_v6().to_bytes().data(), sport_,
                            dip_.to_v6().to_bytes().data(), dport_,
                            IPPROTO_UDP);
        pkt_handler->Ip6Hdr(pkt_info->ip6,
                            data_len_ + sizeof(udphdr) + sizeof(struct ip6_hdr),
                            IPPROTO_UDP, DEFAULT_IP_TTL,
                            sip_.to_v6().to_bytes().data(),
                            dip_.to_v6().to_bytes().data());
        pkt_handler->EthHdr(agent->vhost_interface()->mac(),
                            agent->vrrp_mac(), ETHERTYPE_IPV6);
    }
    return pkt_handler;
}

void Ping::SendRequest() {
    Agent *agent = Agent::GetInstance();
    DiagPktHandler *pkt_handler = NULL;
    //Increment the attempt count
    seq_no_++;
    switch(proto_) {
    case IPPROTO_TCP:
        pkt_handler = CreateTcpPkt(agent);
        break;

    case IPPROTO_UDP:
        pkt_handler = CreateUdpPkt(agent);
        break;
    }

    InetUnicastAgentRouteTable *table;
    if (sip_.is_v4()) {
        table = agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name_);
    } else {
        table = agent->vrf_table()->GetInet6UnicastRouteTable(vrf_name_);
    }
    AgentRoute *rt = table->FindRoute(sip_);
    if (!rt) {
        delete pkt_handler;
        return;
    }

    const NextHop *nh;
    nh = rt->GetActiveNextHop();
    if (!nh || nh->GetType() != NextHop::INTERFACE) {
        delete pkt_handler;
        return;
    }

    const InterfaceNH *intf_nh;
    intf_nh = static_cast<const InterfaceNH *>(nh);

    uint32_t intf_id = intf_nh->GetInterface()->id();
    uint32_t vrf_id = diag_table_->agent()->vrf_table()->FindVrfFromName(vrf_name_)->vrf_id();
    //Send request out
    pkt_handler->SetDiagChkSum();
    pkt_handler->pkt_info()->set_len(len_);
    pkt_handler->Send(intf_id, vrf_id, AgentHdr::TX_ROUTE,
                      CMD_PARAM_PACKET_CTRL, CMD_PARAM_1_DIAG, PktHandler::DIAG);
    delete pkt_handler;
    return;
}

void Ping::RequestTimedOut(uint32_t seqno) {
    PingResp *resp = new PingResp();
    pkt_lost_count_++;
    resp->set_resp("Timed Out");
    resp->set_seq_no(seqno);
    resp->set_context(context_);
    resp->set_more(true);
    resp->Response();
}

void time_duration_to_string(time_duration &td, std::string &str) {
    std::ostringstream td_str;

    if (td.minutes()) {
        td_str << td.minutes() << "m " << td.seconds() << "s";
    } else if (td.total_milliseconds()) {
        td_str << td.total_milliseconds() << "ms";
    } else if (td.total_microseconds()) {
        td_str << td.total_microseconds() << "us";
    } else {
        td_str << td.total_nanoseconds() << "ns";
    }

    str = td_str.str();
}

void Ping::HandleReply(DiagPktHandler *handler) {
    //Send reply
    PingResp *resp = new PingResp();
    AgentDiagPktData *ad = (AgentDiagPktData *)handler->GetData();

    resp->set_seq_no(ntohl(ad->seq_no_));

    //Calculate rtt
    time_duration rtt = microsec_clock::universal_time() - ad->rtt_;
    avg_rtt_ += rtt;
    std::string rtt_str;
    time_duration_to_string(rtt, rtt_str);
    resp->set_rtt(rtt_str);

    resp->set_resp("Success");
    resp->set_context(context_);
    resp->set_more(true);
    resp->Response();
}

void Ping::SendSummary() {
    PingSummaryResp *resp = new PingSummaryResp();

    if (pkt_lost_count_ != GetMaxAttempts()) {
        //If we had some succesful replies, send in
        //average rtt for succesful ping requests
        avg_rtt_ = (avg_rtt_ / (seq_no_ - pkt_lost_count_));
        std::string avg_rtt_string;
        time_duration_to_string(avg_rtt_, avg_rtt_string);
        resp->set_average_rtt(avg_rtt_string);
    }

    resp->set_request_sent(seq_no_);
    resp->set_response_received(seq_no_ - pkt_lost_count_);
    uint32_t pkt_loss_percent = (pkt_lost_count_ * 100/seq_no_);
    resp->set_pkt_loss(pkt_loss_percent);
    resp->set_context(context_);
    resp->Response();
}

void PingReq::HandleRequest() const {
    std::string err_str;
    boost::system::error_code ec;
    Ping *ping = NULL;

    {
    IpAddress sip(IpAddress::from_string(get_source_ip(), ec));
    if (ec != 0) {
        err_str = "Invalid source IP";
        goto error;
    }

    IpAddress dip(IpAddress::from_string(get_dest_ip(), ec));
    if (ec != 0) {
        err_str = "Invalid destination IP";
        goto error;
    }

    uint8_t proto = get_protocol();
    if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
        err_str = "Invalid protocol. Valid Protocols are TCP and UDP";
        goto error;
    }

    if (Agent::GetInstance()->vrf_table()->FindVrfFromName(get_vrf_name()) == NULL) {
        err_str = "Invalid VRF";
        goto error;
    }

    const NextHop *nh = NULL;
    Agent *agent = Agent::GetInstance();
    InetUnicastAgentRouteTable *table;
    if (sip.is_v4()) {
        table = agent->vrf_table()->GetInet4UnicastRouteTable(get_vrf_name());
    } else {
        table = agent->vrf_table()->GetInet6UnicastRouteTable(get_vrf_name());
    }
    AgentRoute *rt = table->FindRoute(sip);
    if (rt) {
        nh = rt->GetActiveNextHop();
    }
    if (!nh || nh->GetType() != NextHop::INTERFACE) {
        err_str = "VM not present on this server";
        goto error;
    }
    }
    ping = new Ping(this, Agent::GetInstance()->diag_table());
    ping->Init();
    return;

error:
    PingErrResp *resp = new PingErrResp;
    resp->set_error_response(err_str);
    resp->set_context(context());
    resp->Response();
    return;
}
