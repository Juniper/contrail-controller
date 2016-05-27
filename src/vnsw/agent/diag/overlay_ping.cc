#include <stdint.h>
#include "base/os.h"
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "diag/diag_types.h"
#include "diag/diag.h"
#include "diag/overlay_ping.h"
#include <oper/route_common.h>
#include <oper/vrf_assign.h>  
#include <oper/tunnel_nh.h>

using namespace boost::posix_time;
extern void time_duration_to_string(time_duration &td, std::string &str);
const MacAddress OverlayPing::in_dst_mac_(0x00, 0x00, 0x5E, 0x90, 0x00, 0x01);
const MacAddress OverlayPing::in_source_mac_(0x00, 0x00, 0x5E, 0x90, 0x00, 0x02);

OverlayPing::OverlayPing(const OverlayPingReq *ping_req, DiagTable *diag_table):
   DiagEntry(ping_req->get_source_ip(), ping_req->get_dest_ip(),
            ping_req->get_protocol(), ping_req->get_source_port(),
            ping_req->get_dest_port(), Agent::GetInstance()->fabric_vrf_name(),
            ping_req->get_interval() * 100, ping_req->get_count(), diag_table),
            vn_uuid_(StringToUuid(ping_req->get_vn_uuid())), 
            remote_vm_mac_(ping_req->get_vm_remote_mac()),
            data_len_(ping_req->get_packet_size()), 
            context_(ping_req->context()),
            pkt_lost_count_(0) {

}
/*
 * Get the L2 Route entry to find the Tunnel NH
 */
BridgeRouteEntry * 
OverlayPing::L2RouteGet(VxLanId* vxlan, string remotemac, Agent *agent)
{
    string vrf_name;
    const VrfNH *vrf_nh = dynamic_cast<const VrfNH *>(vxlan->nexthop());
    VrfEntry *vrf = NULL;
    if (vrf_nh) {
        vrf = const_cast<VrfEntry*> (vrf_nh->GetVrf());
        if (!vrf || vrf->IsDeleted()) {
            return NULL;
        } else {  
            vrf_name = vrf->GetName();
        }
    } else {
        return NULL;
    }
    MacAddress mac = MacAddress::FromString(remotemac);
    BridgeRouteKey key(agent->local_vm_peer(), vrf_name, mac);
    BridgeRouteEntry *rt =  static_cast<BridgeRouteEntry *>
            (static_cast<BridgeAgentRouteTable *>
             (vrf->GetBridgeRouteTable())->FindActiveEntry(&key));
    return rt;
}

void OverlayPingReq::HandleRequest() const {
    std::string err_str;
    boost::system::error_code ec;
    OverlayPing *overlayping = NULL;
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
        err_str = "Invalid protocol. Valid Protocols are TCP & UDP";
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

    BridgeRouteEntry *rt = OverlayPing::L2RouteGet(vxlan, get_vm_remote_mac(), 
                                                   agent);
    if (!rt) {
        err_str = "Invalid remote mac";
        goto error;
    }
    }
    overlayping = new OverlayPing(this, Agent::GetInstance()->diag_table());
    overlayping->Init();
    return;
error:
    PingErrResp *resp = new PingErrResp;
    resp->set_error_response(err_str);
    resp->set_context(context());
    resp->Response();
    return;
}

OverlayPing::~OverlayPing() {

}

/*
 * Creat Overlay VXlan packet.
 * Set the Route alert bit to indicate Overlay OAM packet
 */
void OverlayPing::SendRequest() {
    // Create VxLan packet 
    Agent *agent = diag_table_->agent();
    Ip4Address tunneldst;
    Ip4Address tunnelsrc;
    seq_no_++;
    string vrf_name;
    boost::system::error_code ec;

    int vxlan_id = agent->vn_table()->Find(vn_uuid_)->GetVxLanId();
    VxLanId *vxlan = agent->vxlan_table()->Find(vxlan_id);
    if (!vxlan)
        return;

    BridgeRouteEntry *rt = L2RouteGet(vxlan, remote_vm_mac_.ToString(), agent);
    if (!rt)
        return;
    const AgentPath *path = rt->GetActivePath();
    const TunnelNH *nh = static_cast<const TunnelNH *>(path->nexthop());

    tunneldst = *nh->GetDip();
    tunnelsrc = *nh->GetSip();
    len_ = kOverlayUdpHdrLength + data_len_;
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(agent, len_,
                                                        PktHandler::DIAG, 0));
    uint8_t *buf = pkt_info->packet_buffer()->data();
    memset(buf, 0, len_);
    OverlayOamPktData *pktdata = NULL;
    pktdata = (OverlayOamPktData *)(buf + kOverlayUdpHdrLength);
    memset(pktdata, 0, sizeof(OverlayOamPktData));

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
    len = data_len_+2 * sizeof(udphdr)+sizeof(VxlanHdr)+
                            sizeof(struct ip) + sizeof(struct ether_header);
    pkt_handler->UdpHdr(len, ntohl(tunnelsrc.to_ulong()), HashValUdpSourcePort(), 
                        ntohl(tunneldst.to_ulong()), VXLAN_UDP_DEST_PORT);

    pkt_handler->IpHdr(len + sizeof(struct ip), ntohl(tunnelsrc.to_ulong()),
                       ntohl(tunneldst.to_ulong()), IPPROTO_UDP,
                       DEFAULT_IP_ID, DEFAULT_IP_TTL );
   // Fill VxLan Header  
    VxlanHdr *vxlanhdr = (VxlanHdr *)(buf + sizeof(udphdr)+ sizeof(struct ip)
                                   + sizeof(struct ether_header));
    vxlanhdr->vxlan_id =  ntohl(vxlan_id << 8);
    vxlanhdr->reserved = ntohl(kVxlanRABit | kVxlanIBit);

    //Fill  inner packet details.
    pkt_info->eth = (struct ether_header *)(vxlanhdr + 1);

    pkt_handler->EthHdr(in_source_mac_, in_dst_mac_,
                        ETHERTYPE_IP);

    pkt_info->ip = (struct ip *)(pkt_info->eth +1);
    Ip4Address dip = Ip4Address::from_string("127.0.0.1", ec);
    pkt_info->transp.udp = (struct udphdr *)(pkt_info->ip + 1);
    len = data_len_+sizeof(struct udphdr);
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

void OverlayPing::HandleReply(DiagPktHandler *handler) {
    PingResp *resp = new PingResp();
    OverlayOamPktData *pktdata = (OverlayOamPktData*) handler->GetData();
    resp->set_seq_no(ntohl(pktdata->seq_no_));
    boost::posix_time::ptime time = microsec_clock::universal_time(); 
    boost::posix_time::time_duration td = time.time_of_day();
    uint64_t senttime = seconds(pktdata->timesent_sec_).total_microseconds() +
                 pktdata->timesent_misec_;
    time_duration  rtt= microseconds(td.total_microseconds() -  senttime);
    avg_rtt_ += rtt;
    std::string rtt_str;
    time_duration_to_string(rtt, rtt_str);
    resp->set_rtt(rtt_str); 
    resp->set_resp("Success");
    resp->set_context(context_);
    resp->set_more(true);
    resp->Response();
}

void OverlayPing::RequestTimedOut(uint32_t seq_no) {
    PingResp *resp = new PingResp();
    pkt_lost_count_++;
    resp->set_resp("Timed Out");
    resp->set_seq_no(seq_no_);
    resp->set_context(context_);
    resp->set_more(true);
    resp->Response();
}

void OverlayPing::SendSummary(){
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

