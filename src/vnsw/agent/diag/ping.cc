/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vr_defs.h"
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include "cmn/agent_cmn.h"
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/mirror_table.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "diag/diag_types.h"
#include "diag/diag.h"
#include "diag/ping.h"

using namespace boost::posix_time;

Ping::Ping(const PingReq *ping_req,DiagTable *diag_table):
    DiagEntry(ping_req->get_interval() * 100, ping_req->get_count(),diag_table),
    sip_(Ip4Address::from_string(ping_req->get_source_ip(), ec_)),
    dip_(Ip4Address::from_string(ping_req->get_dest_ip(), ec_)),
    proto_(ping_req->get_protocol()), sport_(ping_req->get_source_port()),
    dport_(ping_req->get_dest_port()), data_len_(ping_req->get_packet_size()),
    vrf_name_(ping_req->get_vrf_name()), context_(ping_req->context()), 
    pkt_lost_count_(0) {

}

Ping::~Ping() {
}

void
Ping::FillAgentHeader(AgentDiagPktData *ad) {
    ad->op_ = htonl(AgentDiagPktData::DIAG_REQUEST);
    ad->key_ = htonl(key_);
    ad->seq_no_ = htonl(seq_no_);
    ad->rtt_ = microsec_clock::universal_time();
}

DiagPktHandler*
Ping::CreateTcpPkt(Agent *agent) {
    //Allocate buffer to hold packet
    len_ = KPingTcpHdr + data_len_;
    uint8_t *msg = new uint8_t[len_];
    memset(msg, 0, len_);

    AgentDiagPktData *ad = (AgentDiagPktData *)(msg + KPingTcpHdr);
    FillAgentHeader(ad);

    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(msg, len_, len_));
    DiagPktHandler *pkt_handler = new DiagPktHandler(diag_table_->agent(), pkt_info,
                                   *(diag_table_->agent()->event_manager())->io_service());

    //Update pointers to ethernet header, ip header and l4 header
    pkt_info->UpdateHeaderPtr();
    pkt_handler->TcpHdr(htonl(sip_.to_ulong()), sport_,  htonl(dip_.to_ulong()),
                        dport_, false, rand(), data_len_ + sizeof(tcphdr));
    pkt_handler->IpHdr(data_len_ + sizeof(tcphdr) + sizeof(ip),
                       ntohl(sip_.to_ulong()), ntohl(dip_.to_ulong()),
                       IPPROTO_TCP);
    pkt_handler->EthHdr(agent->vhost_interface()->mac(),
                        agent->vrrp_mac(), IP_PROTOCOL);

    return pkt_handler;
}

DiagPktHandler*
Ping::CreateUdpPkt(Agent *agent) {
    //Allocate buffer to hold packet
    len_ = KPingUdpHdr + data_len_;
    uint8_t *msg = new uint8_t[len_];
    memset(msg, 0, len_);

    AgentDiagPktData *ad = (AgentDiagPktData *)(msg + KPingUdpHdr);
    FillAgentHeader(ad);

    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(msg, len_, len_));
    DiagPktHandler *pkt_handler = new DiagPktHandler(diag_table_->agent(), pkt_info,
                                    *(diag_table_->agent()->event_manager())->io_service());

    //Update pointers to ethernet header, ip header and l4 header
    pkt_info->UpdateHeaderPtr();
    pkt_handler->UdpHdr(data_len_+ sizeof(udphdr), sip_.to_ulong(), sport_,
                        dip_.to_ulong(), dport_);
    pkt_handler->IpHdr(data_len_ + sizeof(udphdr) + sizeof(ip),
                       ntohl(sip_.to_ulong()), ntohl(dip_.to_ulong()),
                       IPPROTO_UDP);
    pkt_handler->EthHdr(agent->vhost_interface()->mac(),
                        agent->vrrp_mac(), IP_PROTOCOL);

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

    Inet4UnicastAgentRouteTable *table = NULL;
    table = static_cast<Inet4UnicastAgentRouteTable *>
        (agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name_));
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
    pkt_handler->Send(len_ - TapInterface::kAgentHdrLen,
                      intf_id, vrf_id, AgentHdr::TX_ROUTE, PktHandler::DIAG);
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

static void time_duration_to_string(time_duration &td, std::string &str) {
    ostringstream td_str;

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
    AgentDiagPktData *ad = handler->GetData();

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

    if (pkt_lost_count_ != count_) {
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
    Ip4Address sip(Ip4Address::from_string(get_source_ip(), ec));
    if (ec != 0) {
        err_str = "Invalid source IP";
        goto error;
    }

    Ip4Address dip(Ip4Address::from_string(get_source_ip(), ec));
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
    Inet4UnicastAgentRouteTable *table = NULL;
    table = static_cast<Inet4UnicastAgentRouteTable *>
        (agent->vrf_table()->GetInet4UnicastRouteTable(get_vrf_name()));
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

void Ping::PingInit() {
}
