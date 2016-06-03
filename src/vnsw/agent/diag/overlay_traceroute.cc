/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include <netinet/udp.h>
#include "vr_defs.h"
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include "cmn/agent_cmn.h"
#include "oper/route_common.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "diag/diag_types.h"
#include "diag/diag.h"
#include "diag/overlay_traceroute.h"
#include "diag/overlay_ping.h"
#include "diag/traceroute.h"

using namespace boost::posix_time;
OverlayTraceRoute::OverlayTraceRoute(const OverlayTraceReq *traceroute_req,
                       DiagTable *diag_table) :
    DiagEntry(traceroute_req->get_source_ip(), traceroute_req->get_dest_ip(),
              traceroute_req->get_protocol(), traceroute_req->get_source_port(),
              traceroute_req->get_dest_port(), Agent::GetInstance()->fabric_vrf_name(),
              traceroute_req->get_interval() * 100,traceroute_req->get_max_attempts(), diag_table),
              vn_uuid_(StringToUuid(traceroute_req->get_vn_uuid())),
              remote_vm_mac_(traceroute_req->get_vm_remote_mac()), ttl_(2),
              max_ttl_(traceroute_req->get_max_hops()),
              context_(traceroute_req->context()) {
}

OverlayTraceRoute::~OverlayTraceRoute() {
}


void OverlayTraceRoute::SendRequest() 
{
    Agent *agent = diag_table_->agent();
    Ip4Address tunneldst;
    Ip4Address tunnelsrc;
    seq_no_++;
    string vrf_name;
    boost::system::error_code ec;
    uint8_t data_len = 50;
    int vxlan_id = agent->vn_table()->Find(vn_uuid_)->GetVxLanId();
    VxLanId *vxlan = agent->vxlan_table()->Find(vxlan_id);
    if (!vxlan)
        return;

    BridgeRouteEntry *rt = OverlayPing::L2RouteGet(vxlan, 
                                                   remote_vm_mac_.ToString(), agent);
    if (!rt)
        return;
    const AgentPath *path = rt->GetActivePath();
    const TunnelNH *nh = static_cast<const TunnelNH *>(path->nexthop());

    tunneldst = *nh->GetDip();
    tunnelsrc = *nh->GetSip();
    len_ = OverlayPing::kOverlayUdpHdrLength + data_len;
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(agent, len_,
                                                        PktHandler::DIAG, 0));
    uint8_t *buf = pkt_info->packet_buffer()->data();
    memset(buf, 0, len_);
    OverlayOamPktData *pktdata = NULL;
    pktdata = (OverlayOamPktData *)(buf + OverlayPing::kOverlayUdpHdrLength);

    FillOamPktHeader(pktdata, vxlan_id);
    DiagPktHandler *pkt_handler = new DiagPktHandler(diag_table_->agent(), pkt_info,
                                   *(diag_table_->agent()->event_manager())->io_service());
    // FIll outer header
    pkt_info->eth = (struct ether_header *)(buf);
    pkt_handler->EthHdr(agent->vhost_interface()->mac(), *nh->GetDmac(),
                        ETHERTYPE_IP);
    pkt_info->ip = (struct ip *)(pkt_info->eth +1);
    pkt_info->transp.udp = (struct udphdr *)(pkt_info->ip + 1);
    uint8_t  len;
    len = data_len+2 * sizeof(udphdr)+sizeof(VxlanHdr)+
                            sizeof(struct ip) + sizeof(struct ether_header);
    pkt_handler->UdpHdr(len, ntohl(tunnelsrc.to_ulong()), HashValUdpSourcePort(),
                     ntohl(tunneldst.to_ulong()), VXLAN_UDP_DEST_PORT);

    pkt_handler->IpHdr(len + sizeof(struct ip), ntohl(tunnelsrc.to_ulong()),
                       ntohl(tunneldst.to_ulong()), IPPROTO_UDP,
                       DEFAULT_IP_ID, ttl_);
   // Fill VxLan Header  
    VxlanHdr *vxlanhdr = (VxlanHdr *)(buf + sizeof(udphdr)+ sizeof(struct ip)
                                   + sizeof(struct ether_header));
    vxlanhdr->vxlan_id =  ntohl(vxlan_id << 8);
    vxlanhdr->reserved = ntohl(OverlayPing::kVxlanRABit | OverlayPing::kVxlanIBit);

    //Fill  inner packet details.
    pkt_info->eth = (struct ether_header *)(vxlanhdr + 1);

    pkt_handler->EthHdr(OverlayPing::in_source_mac_, OverlayPing::in_dst_mac_,
                        ETHERTYPE_IP);

    pkt_info->ip = (struct ip *)(pkt_info->eth +1);
    Ip4Address dip = Ip4Address::from_string("127.0.0.1", ec);
    pkt_info->transp.udp = (struct udphdr *)(pkt_info->ip + 1);
    len = data_len+sizeof(struct udphdr);
    pkt_handler->UdpHdr(len, sip_.to_ulong(), sport_, dip.to_ulong(), 
                        VXLAN_UDP_DEST_PORT);
    pkt_handler->IpHdr(len + sizeof(struct ip), ntohl(sip_.to_ulong()),
                       ntohl(dip.to_ulong()), proto_, 
                       DEFAULT_IP_ID, DEFAULT_IP_TTL);
    //pkt_handler->SetDiagChkSum();
    pkt_handler->pkt_info()->set_len(len_);
    PhysicalInterfaceKey key1(agent->fabric_interface_name());
    Interface *intf = static_cast<Interface *>
                (agent->interface_table()->Find(&key1, true));
    pkt_handler->Send(intf->id(), agent->fabric_vrf()->vrf_id(),
                      AgentHdr::TX_SWITCH, CMD_PARAM_PACKET_CTRL, 
                      CMD_PARAM_1_DIAG, PktHandler::DIAG);

    delete pkt_handler;
    return;
}

void OverlayTraceReq::HandleRequest() const {
    std::string err_str;
    boost::system::error_code ec;
    OverlayTraceRoute *overlaytraceroute = NULL;

    {
        Agent *agent = Agent::GetInstance();
        uuid vn_uuid = StringToUuid(get_vn_uuid());
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
        if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
            err_str = "Invalid protocol - Supported protocols are TCP & UDP";
            goto error;
        }
        VnEntry *vn  = agent->vn_table()->Find(vn_uuid);

        if (!vn) {
            err_str = "Invalid VN segment";
            goto error;
        }

        int vxlan_id = vn->GetVxLanId();
        VxLanId* vxlan = agent->vxlan_table()->Find(vxlan_id);
    
        if (!vxlan) {
            err_str = "Invalid vxlan segment";
            goto error;
        }

        BridgeRouteEntry *rt = OverlayPing::L2RouteGet(vxlan, 
                                                       get_vm_remote_mac(), agent);
        if (!rt) {
            err_str = "Invalid remote mac";
            goto error;
        }
    }
    overlaytraceroute = new OverlayTraceRoute(this, 
                                              Agent::GetInstance()->diag_table());
    overlaytraceroute->Init();
    overlaytraceroute->ReplyLocalHop();
    return;

error:
    TraceRouteErrResp *resp = new TraceRouteErrResp;
    resp->set_error_response(err_str);
    resp->set_context(context());
    resp->Response();
    return;
}
// if timed out max times for a TTL, reply and increment ttl
void OverlayTraceRoute::RequestTimedOut(uint32_t seqno) {
    if (seq_no_ >= GetMaxAttempts()) {
        std::string address;
        for (uint32_t i = 0; i < GetMaxAttempts(); i++)
            address += "* ";

        done_ = ((ttl_ >= max_ttl_) ? true : false);
        TraceRoute::SendSandeshReply(address, context_, !done_);
        IncrementTtl();
    }
}

// Ready to send a response and increment ttl
void OverlayTraceRoute::HandleReply(DiagPktHandler *handler) {
    if (ttl_ >= max_ttl_) {
        handler->set_done(true);
        done_ = true;
    }
    struct ip *ip = handler->pkt_info()->tunnel.ip;
    IpAddress saddr = IpAddress(Ip4Address(ntohl(ip->ip_src.s_addr)));
    TraceRoute::SendSandeshReply(saddr.to_v4().to_string(), context_, 
                                 !handler->IsDone());
    IncrementTtl();
}

// Reply with local node as the first hop
void OverlayTraceRoute::ReplyLocalHop() {
    TraceRoute::SendSandeshReply(diag_table_->agent()->router_id().to_string(),
                     context_, true);
}

void OverlayTraceRoute::IncrementTtl() {
    ttl_++;
    seq_no_ = 0;
}
