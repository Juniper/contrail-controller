/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include <netinet/udp.h>
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
#include "diag/traceroute.h"

static void SendSandeshReply(const std::string &address,
                             const std::string &context,
                             bool more) {
    TraceRouteResp *resp = new TraceRouteResp();
    resp->set_hop(address);

    resp->set_context(context);
    resp->set_more(more);
    resp->Response();
}

TraceRoute::TraceRoute(const TraceRouteReq *trace_route_req,
                       DiagTable *diag_table) :
    DiagEntry(trace_route_req->get_source_ip(), trace_route_req->get_dest_ip(),
              trace_route_req->get_protocol(), trace_route_req->get_source_port(),
              trace_route_req->get_dest_port(), trace_route_req->get_vrf_name(),
              trace_route_req->get_interval() * 100,
              trace_route_req->get_max_attempts(), diag_table),
    done_(false), ttl_(2),
    max_ttl_(trace_route_req->get_max_hops()),
    context_(trace_route_req->context()) {
}

TraceRoute::~TraceRoute() {
}

void TraceRoute::FillHeader(AgentDiagPktData *data) {
    data->op_ = htonl(AgentDiagPktData::DIAG_REQUEST);
    data->key_ = htons(key_);
    data->seq_no_ = htonl(seq_no_);
    // data->rtt_ = microsec_clock::universal_time();
}

void TraceRoute::SendRequest() {
    Agent *agent = diag_table_->agent();

    InetUnicastAgentRouteTable *table =
        agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name_);
    AgentRoute *rt = table->FindRoute(sip_);
    if (!rt) return;

    const NextHop *nh = rt->GetActiveNextHop();
    if (!nh || nh->GetType() != NextHop::INTERFACE) return;

    const InterfaceNH *intf_nh;
    intf_nh = static_cast<const InterfaceNH *>(nh);

    uint32_t intf_id = intf_nh->GetInterface()->id();
    uint32_t vrf_id = agent->vrf_table()->FindVrfFromName(vrf_name_)->vrf_id();

    //Allocate buffer to hold packet
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(agent, kBufferSize,
                                                    PktHandler::DIAG, 0));
    uint8_t *msg = pkt_info->packet_buffer()->data();
    memset(msg, 0, kBufferSize);

    DiagPktHandler *pkt_handler =
        new DiagPktHandler(agent, pkt_info,
                           *(agent->event_manager())->io_service());

    //Update pointers to ethernet header, ip header and l4 header
    pkt_info->UpdateHeaderPtr();
    uint16_t len = sizeof(AgentDiagPktData);
    uint8_t *data = NULL;
    switch (proto_) {
        case IPPROTO_TCP:
            len += sizeof(tcphdr);
            data = (uint8_t *)pkt_handler->pkt_info()->transp.tcp + sizeof(tcphdr);
            pkt_handler->TcpHdr(htonl(sip_.to_ulong()), sport_,
                                htonl(dip_.to_ulong()), dport_,
                                false, rand(), len);
            pkt_handler->pkt_info()->transp.tcp->check = 0xffff;
            break;

        case IPPROTO_UDP:
            len += sizeof(udphdr);
            data = (uint8_t *)pkt_handler->pkt_info()->transp.udp + sizeof(udphdr);
            pkt_handler->UdpHdr(len, sip_.to_ulong(), sport_,
                                dip_.to_ulong(), dport_);
            pkt_handler->pkt_info()->transp.udp->check = 0xffff;
            break;

        case IPPROTO_ICMP:
            len += 8;
            data = (uint8_t *)pkt_handler->pkt_info()->transp.icmp + 8;
            pkt_handler->pkt_info()->transp.icmp->icmp_type = ICMP_ECHO;
            pkt_handler->pkt_info()->transp.icmp->icmp_code = 0;
            pkt_handler->pkt_info()->transp.icmp->icmp_cksum = 0xffff;
            break;
    }
    FillHeader((AgentDiagPktData *)data);
    len += sizeof(struct ip);
    pkt_handler->IpHdr(len, ntohl(sip_.to_ulong()), ntohl(dip_.to_ulong()),
                       proto_, key_, ttl_);
    len += sizeof(ether_header);
    pkt_handler->EthHdr(agent->vhost_interface()->mac(),
                        agent->vrrp_mac(), ETHERTYPE_IP);

    //Increment the attempt count
    seq_no_++;

    //Send request out
    pkt_handler->pkt_info()->set_len(len);
    pkt_handler->Send(intf_id, vrf_id, AgentHdr::TX_ROUTE,
                      CMD_PARAM_PACKET_CTRL, CMD_PARAM_1_DIAG, PktHandler::DIAG);
    delete pkt_handler;
    return;
}

// if timed out max times for a TTL, reply and increment ttl
void TraceRoute::RequestTimedOut(uint32_t seqno) {
    if (seq_no_ >= GetMaxAttempts()) {
        std::string address;
        for (uint32_t i = 0; i < GetMaxAttempts(); i++)
            address += "* ";

        done_ = ((ttl_ >= max_ttl_) ? true : false);
        SendSandeshReply(address, context_, !done_);
        IncrementTtl();
    }
}

// Ready to send a response and increment ttl
void TraceRoute::HandleReply(DiagPktHandler *handler) {
    if (ttl_ >= max_ttl_) {
        handler->set_done(true);
        done_ = true;
    }
    SendSandeshReply(handler->GetAddress(), context_, !handler->IsDone());
    IncrementTtl();
}

// Reply with local node as the first hop
void TraceRoute::ReplyLocalHop() {
    SendSandeshReply(diag_table_->agent()->router_id().to_string(),
                     context_, true);
}

void TraceRoute::SendSummary() {
}

bool TraceRoute::IsDone() {
    return done_;
}

void TraceRoute::IncrementTtl() {
    ttl_++;
    seq_no_ = 0;
}

void TraceRouteReq::HandleRequest() const {
    std::string err_str;
    boost::system::error_code ec;
    TraceRoute *trace_route = NULL;

    {
        Ip4Address sip(Ip4Address::from_string(get_source_ip(), ec));
        if (ec != 0) {
            err_str = "Invalid source IP";
            goto error;
        }

        Ip4Address dip(Ip4Address::from_string(get_dest_ip(), ec));
        if (ec != 0) {
            err_str = "Invalid destination IP";
            goto error;
        }

        uint8_t proto = get_protocol();
        if (proto != IPPROTO_TCP && proto != IPPROTO_UDP && proto != IPPROTO_ICMP) {
            err_str = "Invalid protocol - Supported protocols are TCP, UDP and ICMP";
            goto error;
        }

        if (Agent::GetInstance()->vrf_table()->FindVrfFromName(get_vrf_name()) == NULL) {
            err_str = "Invalid VRF";
            goto error;
        }

        const NextHop *nh = NULL;
        Agent *agent = Agent::GetInstance();
        InetUnicastAgentRouteTable *table =
            agent->vrf_table()->GetInet4UnicastRouteTable(get_vrf_name());
        AgentRoute *rt = table->FindRoute(sip);
        if (rt) {
            nh = rt->GetActiveNextHop();
        }
        if (!nh || nh->GetType() != NextHop::INTERFACE) {
            err_str = "Source VM is not present in this server";
            goto error;
        }

        rt = table->FindRoute(dip);
        if (rt) {
            nh = rt->GetActiveNextHop();
            if (nh && nh->GetType() == NextHop::INTERFACE) {
                // Dest VM is also local
                SendSandeshReply(agent->router_id().to_string(), context(), false);
                return;
            }
        }
    }

    trace_route = new TraceRoute(this, Agent::GetInstance()->diag_table());
    trace_route->ReplyLocalHop();
    trace_route->Init();
    return;

error:
    TraceRouteErrResp *resp = new TraceRouteErrResp;
    resp->set_error_response(err_str);
    resp->set_context(context());
    resp->Response();
    return;
}
