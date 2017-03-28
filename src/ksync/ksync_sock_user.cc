/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/sockios.h>
#elif defined(__FreeBSD__)
#include "vr_os.h"
#endif

#include <boost/bind.hpp>

#include <base/logging.h>
#include <net/address_util.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "ksync_index.h"
#include "ksync_entry.h"
#include "ksync_object.h"
#include "ksync_sock.h"
#include "ksync_sock_user.h"

#include "nl_util.h"
#include "vr_genetlink.h"
#include "vr_message.h"
#include "vr_types.h"
#include "vr_defs.h"
#include "vr_interface.h"
#include <vector>

KSyncSockTypeMap *KSyncSockTypeMap::singleton_;
vr_flow_entry *KSyncSockTypeMap::flow_table_;
int KSyncSockTypeMap::error_code_;
using namespace boost::asio;

int EncodeVrResponse(uint8_t *buf, int buf_len, uint32_t seq_num, int code) {
    vr_response encoder;
    int error = 0;

    encoder.set_h_op(sandesh_op::RESPONSE);
    encoder.set_resp_code(code);
    return encoder.WriteBinary(buf, buf_len, &error);
}

void KSyncSockTypeMap::AddNetlinkTxBuff(struct nl_client *cl) {
    tx_buff_list_.push_back(*cl);
}

//process sandesh messages that are being sent from the agent
//this is used to store a local copy of what is being send to kernel
//Also handles bulk request messages.
void KSyncSockTypeMap::ProcessSandesh(const uint8_t *parse_buf, size_t buf_len, 
                                      KSyncUserSockContext *ctx) {
    int decode_len;
    uint8_t *decode_buf;

    // Ensure that tx_buff_list is empty
    assert(tx_buff_list_.size() == 0);

    //parse sandesh
    int err = 0;
    int decode_buf_len = buf_len;
    decode_buf = (uint8_t *)(parse_buf);
    while(decode_buf_len > 0) {
        decode_len = Sandesh::ReceiveBinaryMsgOne(decode_buf, decode_buf_len,
                                                  &err, ctx);
        if (decode_len < 0) {
            LOG(DEBUG, "Incorrect decode len " << decode_len);
            break;
        }
        decode_buf += decode_len;
        decode_buf_len -= decode_len;
    }

    PurgeTxBuffer();
}

void KSyncSockTypeMap::PurgeTxBuffer() {
    // All responses are stored in tx_buff_list_
    // Make io-vector of all responses and transmit them
    // If there are more than one responses, they are sent as NETLINK MULTI
    // messages
    uint32_t count = 0;
    KSyncBufferList iovec;
    struct nlmsghdr *last_nlh = NULL;
    std::vector<struct nl_client>::iterator it = tx_buff_list_.begin();
    // Add all messages to to io-vector.
    while (it != tx_buff_list_.end()) {
        struct nl_client *cl = &(*it);
        struct nlmsghdr *nlh = (struct nlmsghdr *)(cl->cl_buf);
        // Set MULTI flag by default. It will be reset for last buffer later
        nlh->nlmsg_flags |= NLM_F_MULTI;
        last_nlh = nlh;
        iovec.push_back(buffer(cl->cl_buf, cl->cl_msg_len));
        it++;
        count++;
    }

    // If there are more than one NETLINK messages, we need to add 
    struct nlmsghdr nlh;
    if (count > 1) {
        //Send Netlink-Done message NLMSG_DONE at end
        InitNetlinkDoneMsg(&nlh, last_nlh->nlmsg_seq);
        iovec.push_back(buffer((uint8_t *)&nlh, NLMSG_HDRLEN));
    } else {
        // Single buffer. Reset the MULTI flag
        if (last_nlh)
            last_nlh->nlmsg_flags &= (~NLM_F_MULTI);
    }

    // Send a message for each entry in io-vector
    KSyncBufferList::iterator iovec_it = iovec.begin();
    while (iovec_it != iovec.end()) {
         sock_.send_to(*iovec_it, local_ep_);
         iovec_it++;
    }

    // Free the buffers
    it = tx_buff_list_.begin();
    while (it != tx_buff_list_.end()) {
        struct nl_client *cl = &(*it);
        nl_free(cl);
        *it++;
    }
    tx_buff_list_.clear();
}

void KSyncSockTypeMap::FlowNatResponse(uint32_t seq_num, vr_flow_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    int flow_error = sock->GetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE);
    struct nl_client cl;
    int error = 0, ret;
    uint8_t *buf = NULL;
    uint32_t buf_len = 0, encode_len = 0;
    struct nlmsghdr *nlh;

    nl_init_generic_client_req(&cl, KSyncSock::GetNetlinkFamilyId());
    if ((ret = nl_build_header(&cl, &buf, &buf_len)) < 0) {
        LOG(DEBUG, "Error creating interface DUMP message : " << ret);
        nl_free(&cl);
        return;
    }

    nlh = (struct nlmsghdr *)cl.cl_buf;
    nlh->nlmsg_seq = seq_num;

    uint32_t fwd_flow_idx = req->get_fr_index();
    bool add_error = false;
    if (fwd_flow_idx == 0xFFFFFFFF) {
        add_error = true;
    } else {
        if (flow_error != -ENOSPC && flow_error != 0) {
            add_error = true;
        }
    }
    if (add_error) {
        encode_len = EncodeVrResponse(buf, buf_len, seq_num, flow_error);
    } else {
        encode_len = EncodeVrResponse(buf, buf_len, seq_num, 0);
    }

    buf += encode_len;
    buf_len -= encode_len;

    vr_flow_response resp;
    resp.set_fresp_op(flow_op::FLOW_SET);
    resp.set_fresp_flags(req->get_fr_flags());
    resp.set_fresp_index(req->get_fr_index());
    resp.set_fresp_gen_id(req->get_fr_gen_id());
    resp.set_fresp_bytes(0);
    resp.set_fresp_packets(0);
    resp.set_fresp_stats_oflow(0);

    encode_len += resp.WriteBinary(buf, buf_len, &error);
    if (error != 0) {
        SimulateResponse(seq_num, -ENOENT, 0);
        nl_free(&cl);
        return;
    }

    nl_update_header(&cl, encode_len);
    sock->AddNetlinkTxBuff(&cl);
}

void KSyncSockTypeMap::InitNetlinkDoneMsg(struct nlmsghdr *nlh,
                                          uint32_t seq_num) {
    nlh->nlmsg_seq = seq_num;
    nlh->nlmsg_type = NLMSG_DONE;
    nlh->nlmsg_len = NLMSG_HDRLEN;
    nlh->nlmsg_flags = 0;
}

void KSyncSockTypeMap::SimulateResponse(uint32_t seq_num, int code, int flags) {
    struct nl_client cl;
    int encode_len, ret;
    uint8_t *buf;
    uint32_t buf_len;

    nl_init_generic_client_req(&cl, KSyncSock::GetNetlinkFamilyId());
    if ((ret = nl_build_header(&cl, &buf, &buf_len)) < 0) {
        LOG(DEBUG, "Error creating mpls message. Error : " << ret);
        nl_free(&cl);
        return;
    }

    struct nlmsghdr *nlh = (struct nlmsghdr *)cl.cl_buf;
    nlh->nlmsg_seq = seq_num;
    nlh->nlmsg_flags |= flags;
    encode_len = EncodeVrResponse(buf, buf_len, seq_num, code);
    nl_update_header(&cl, encode_len);
    LOG(DEBUG, "SimulateResponse " << " seq " << seq_num << " code " << std::hex << code);

    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    sock->AddNetlinkTxBuff(&cl);
}

void KSyncSockTypeMap::DisableReceiveQueue(bool disable) {
    for(int i = 0; i < IoContext::MAX_WORK_QUEUES; i++) {
        ksync_rx_queue[i]->set_disable(disable);
    }
}

void KSyncSockTypeMap::SetDropStats(const vr_drop_stats_req &req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    sock->drop_stats = req;
}

void KSyncSockTypeMap::SetVRouterOps(const vrouter_ops &req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    sock->ksync_vrouter_ops = req;
}

void KSyncSockTypeMap::InterfaceAdd(int id, int flags, int mac_size) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_if::const_iterator it;
    static int os_index = 10;
    char name[50];
    sprintf(name, "intf%d", id);
    vr_interface_req req;
    req.set_vifr_idx(id);
    req.set_vifr_type(VIF_TYPE_VIRTUAL);
    req.set_vifr_rid(0);
    req.set_vifr_os_idx(os_index);
    req.set_vifr_name(name);
    const std::vector<signed char> list(mac_size);
    req.set_vifr_mac(list);
    req.set_vifr_flags(flags);

    it = sock->if_map.find(id);
    if (it == sock->if_map.end()) {
        sock->if_map[id] = req;
        ++os_index;
    }
}

void KSyncSockTypeMap::InterfaceDelete(int id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_if::iterator it;
    it = sock->if_map.find(id);
    if (it != sock->if_map.end()) {
        sock->if_map.erase(it);
    }
}

void KSyncSockTypeMap::NHAdd(int id, int flags) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_nh::const_iterator it;
    vr_nexthop_req req;
    req.set_nhr_id(id);
    req.set_nhr_flags(flags);
    it = sock->nh_map.find(id);
    if (it == sock->nh_map.end()) {
        sock->nh_map[id] = req;
    }
}

void KSyncSockTypeMap::NHDelete(int id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_nh::iterator it;
    it = sock->nh_map.find(id);
    if (it != sock->nh_map.end()) {
        sock->nh_map.erase(it);
    }
}

void KSyncSockTypeMap::MplsAdd(int id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mpls::const_iterator it;
    vr_mpls_req req;
    req.set_mr_label(id);
    it = sock->mpls_map.find(id);
    if (it == sock->mpls_map.end()) {
        sock->mpls_map[id] = req;
    }
}

void KSyncSockTypeMap::MplsDelete(int id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mpls::iterator it;
    it = sock->mpls_map.find(id);
    if (it != sock->mpls_map.end()) {
        sock->mpls_map.erase(it);
    }
}

void KSyncSockTypeMap::MirrorAdd(int id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mirror::const_iterator it;
    vr_mirror_req req;
    req.set_mirr_index(id);
    it = sock->mirror_map.find(id);
    if (it == sock->mirror_map.end()) {
        sock->mirror_map[id] = req;
    }
}

void KSyncSockTypeMap::MirrorDelete(int id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mirror::iterator it;
    it = sock->mirror_map.find(id);
    if (it != sock->mirror_map.end()) {
        sock->mirror_map.erase(it);
    }
}

void KSyncSockTypeMap::RouteAdd(vr_route_req &req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_rt_tree::const_iterator it;
    it = sock->rt_tree.find(req);
    if (it == sock->rt_tree.end()) {
        sock->rt_tree.insert(req);
    }
}

void KSyncSockTypeMap::RouteDelete(vr_route_req &req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_rt_tree::iterator it;
    it = sock->rt_tree.find(req);
    if (it != sock->rt_tree.end()) {
        sock->rt_tree.erase(it);
    }
}

void KSyncSockTypeMap::VrfAssignAdd(vr_vrf_assign_req &req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_vrf_assign_tree::const_iterator it;
    it = sock->vrf_assign_tree.find(req);
    if (it == sock->vrf_assign_tree.end()) {
        sock->vrf_assign_tree.insert(req);
    }
}

void KSyncSockTypeMap::VrfAssignDelete(vr_vrf_assign_req &req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_vrf_assign_tree::iterator it;
    it = sock->vrf_assign_tree.find(req);
    if (it != sock->vrf_assign_tree.end()) {
        sock->vrf_assign_tree.erase(it);
    }
}

void KSyncSockTypeMap::VrfStatsAdd(int vrf_id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vrf_stats::const_iterator it;
    vr_vrf_stats_req vrf_stats;
    vrf_stats.set_vsr_vrf(vrf_id);
    vrf_stats.set_vsr_family(AF_INET);
    vrf_stats.set_vsr_rid(0);
    vrf_stats.set_vsr_discards(0);
    vrf_stats.set_vsr_resolves(0);
    vrf_stats.set_vsr_receives(0);
    vrf_stats.set_vsr_udp_tunnels(0);
    vrf_stats.set_vsr_udp_mpls_tunnels(0);
    vrf_stats.set_vsr_gre_mpls_tunnels(0);
    vrf_stats.set_vsr_ecmp_composites(0);
    vrf_stats.set_vsr_fabric_composites(0);
    vrf_stats.set_vsr_l2_mcast_composites(0);
    vrf_stats.set_vsr_encaps(0);
    vrf_stats.set_vsr_l2_encaps(0);

    it = sock->vrf_stats_map.find(vrf_id);
    if (it == sock->vrf_stats_map.end()) {
        sock->vrf_stats_map[vrf_id] = vrf_stats;
    }
}

void KSyncSockTypeMap::VrfStatsDelete(int vrf_id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vrf_stats::iterator it;
    it = sock->vrf_stats_map.find(vrf_id);
    if (it != sock->vrf_stats_map.end()) {
        sock->vrf_stats_map.erase(it);
    }
}

void KSyncSockTypeMap::VrfStatsUpdate(int vrf_id, const vr_vrf_stats_req &req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    vr_vrf_stats_req &r = sock->vrf_stats_map[vrf_id];
    r.set_vsr_discards(req.get_vsr_discards());
    r.set_vsr_resolves(req.get_vsr_resolves());
    r.set_vsr_receives(req.get_vsr_receives());
    r.set_vsr_udp_tunnels(req.get_vsr_udp_tunnels());
    r.set_vsr_udp_mpls_tunnels(req.get_vsr_udp_mpls_tunnels());
    r.set_vsr_gre_mpls_tunnels(req.get_vsr_gre_mpls_tunnels());
    r.set_vsr_ecmp_composites(req.get_vsr_ecmp_composites());
    r.set_vsr_l2_mcast_composites(req.get_vsr_l2_mcast_composites());
    r.set_vsr_fabric_composites(req.get_vsr_fabric_composites());
    r.set_vsr_encaps(req.get_vsr_encaps());
    r.set_vsr_l2_encaps(req.get_vsr_l2_encaps());
    r.set_vsr_gros(req.get_vsr_gros());
    r.set_vsr_diags(req.get_vsr_diags());
    r.set_vsr_encap_composites(req.get_vsr_encap_composites());
    r.set_vsr_evpn_composites(req.get_vsr_evpn_composites());
    r.set_vsr_vrf_translates(req.get_vsr_vrf_translates());
    r.set_vsr_vxlan_tunnels(req.get_vsr_vxlan_tunnels());
    r.set_vsr_arp_virtual_proxy(req.get_vsr_arp_virtual_proxy());
    r.set_vsr_arp_virtual_stitch(req.get_vsr_arp_virtual_stitch());
    r.set_vsr_arp_virtual_flood(req.get_vsr_arp_virtual_flood());
    r.set_vsr_arp_physical_stitch(req.get_vsr_arp_physical_stitch());
    r.set_vsr_arp_tor_proxy(req.get_vsr_arp_tor_proxy());
    r.set_vsr_arp_physical_flood(req.get_vsr_arp_physical_flood());
    r.set_vsr_l2_receives(req.get_vsr_l2_receives());
    r.set_vsr_uuc_floods(req.get_vsr_uuc_floods());
}

void KSyncSockTypeMap::VxlanAdd(int id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vxlan::const_iterator it;

    it = sock->vxlan_map.find(id);
    if (it == sock->vxlan_map.end()) {
        vr_vxlan_req vxlan;
        vxlan.set_vxlanr_vnid(id);
        vxlan.set_vxlanr_rid(0);
        sock->vxlan_map[id] = vxlan;
    }
}

void KSyncSockTypeMap::VxlanDelete(int id) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vxlan::iterator it;

    it = sock->vxlan_map.find(id);
    if (it != sock->vxlan_map.end()) {
        sock->vxlan_map.erase(it);
    }
}

void KSyncSockTypeMap::IfStatsUpdate(int idx, int ibytes, int ipkts, int ierrors, 
                                     int obytes, int opkts, int oerrors) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    vr_interface_req req = sock->if_map[idx];
    req.set_vifr_ibytes(ibytes+req.get_vifr_ibytes());
    req.set_vifr_ipackets(ipkts+req.get_vifr_ipackets());
    req.set_vifr_ierrors(ierrors+req.get_vifr_ierrors());
    req.set_vifr_obytes(obytes+req.get_vifr_obytes());
    req.set_vifr_opackets(opkts+req.get_vifr_opackets());
    req.set_vifr_oerrors(oerrors+req.get_vifr_oerrors());
    sock->if_map[idx] = req;
}

void KSyncSockTypeMap::IfStatsSet(int idx, int ibytes, int ipkts, int ierrors, 
                                  int obytes, int opkts, int oerrors) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    vr_interface_req req = sock->if_map[idx];
    req.set_vifr_ibytes(ibytes);
    req.set_vifr_ipackets(ipkts);
    req.set_vifr_ierrors(ierrors);
    req.set_vifr_obytes(obytes);
    req.set_vifr_opackets(opkts);
    req.set_vifr_oerrors(oerrors);
    sock->if_map[idx] = req;
}

int KSyncSockTypeMap::IfCount() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    return sock->if_map.size();
}

int KSyncSockTypeMap::NHCount() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    return sock->nh_map.size();
}

int KSyncSockTypeMap::MplsCount() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    return sock->mpls_map.size();
}

int KSyncSockTypeMap::RouteCount() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    return sock->rt_tree.size();
}

int KSyncSockTypeMap::VxLanCount() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    return sock->vxlan_map.size();
}
 
uint32_t KSyncSockTypeMap::GetSeqno(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    return nlh->nlmsg_seq;
}

bool KSyncSockTypeMap::IsMoreData(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;

    return (nlh->nlmsg_flags & NLM_F_MULTI);
}

bool KSyncSockTypeMap::BulkDecoder(char *data,
                                   KSyncBulkSandeshContext *bulk_context) {
    KSyncSockNetlink::NetlinkBulkDecoder(data, bulk_context, IsMoreData(data));
    return true;
}

bool KSyncSockTypeMap::Decoder(char *data, AgentSandeshContext *context) {
    KSyncSockNetlink::NetlinkDecoder(data, context);
    return true;
}

bool KSyncSockTypeMap::Validate(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    if (nlh->nlmsg_type == NLMSG_ERROR) {
        LOG(ERROR, "Ignoring Netlink error for seqno " << nlh->nlmsg_seq 
                        << " len " << nlh->nlmsg_len);
        assert(0);
        return true;
    }

    if (nlh->nlmsg_len > kBufLen) {
        LOG(ERROR, "Length of " << nlh->nlmsg_len << " is more than expected "
            "length of " << kBufLen);
        assert(0);
        return true;
    }
    return true;
}
static int IoVectorToData(char *data, uint32_t len, KSyncBufferList *iovec) {
    KSyncBufferList::iterator it = iovec->begin();
    int offset = 0;
    while (it != iovec->end()) {
        unsigned char *buf = boost::asio::buffer_cast<unsigned char *>(*it);
        assert((offset + boost::asio::buffer_size(*it)) < len);
        memcpy(data + offset, buf, boost::asio::buffer_size(*it));
        offset +=  boost::asio::buffer_size(*it);
        it++;
    }
    return offset;
}

//send or store in map
void KSyncSockTypeMap::AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                                   HandlerCb cb) {
    char data[4096];
    int data_len = IoVectorToData(data, 4096, iovec);

    KSyncUserSockContext ctx(seq_no);
    //parse and store info in map [done in Process() callbacks]
    ProcessSandesh((const uint8_t *)(data), data_len, &ctx);
}

//send or store in map
std::size_t KSyncSockTypeMap::SendTo(KSyncBufferList *iovec, uint32_t seq_no) {
    char data[4096];
    int data_len = IoVectorToData(data, 4096, iovec);
    KSyncUserSockContext ctx(seq_no);
    //parse and store info in map [done in Process() callbacks]
    ProcessSandesh((const uint8_t *)(data), data_len, &ctx);
    return 0;
}

//receive msgs from datapath
void KSyncSockTypeMap::AsyncReceive(mutable_buffers_1 buf, HandlerCb cb) {
    sock_.async_receive_from(buf, local_ep_, cb);
}

//receive msgs from datapath
void KSyncSockTypeMap::Receive(mutable_buffers_1 buf) {
    sock_.receive(buf);
}

vr_flow_entry *KSyncSockTypeMap::FlowMmapAlloc(int size) {
    flow_table_ = (vr_flow_entry *)malloc(size);
    return flow_table_;
}

void KSyncSockTypeMap::FlowMmapFree() {
    if (flow_table_) {
        free(flow_table_);
        flow_table_ = NULL;
    }
}

vr_flow_entry *KSyncSockTypeMap::GetFlowEntry(int idx) {
    return &flow_table_[idx];
}

void KSyncSockTypeMap::SetFlowEntry(vr_flow_req *req, bool set) {
    uint32_t idx = req->get_fr_index();

    vr_flow_entry *f = &flow_table_[idx];
    if (!set) {
        f->fe_flags &= ~VR_FLOW_FLAG_ACTIVE;
        f->fe_stats.flow_bytes = 0;
        f->fe_stats.flow_packets = 0;
        f->fe_gen_id = 0;
        return;
    }

    int family = (req->get_fr_family() == AF_INET)? Address::INET :
        Address::INET6;
    IpAddress sip;
    IpAddress dip;
    U64ToIp(req->get_fr_flow_sip_u(), req->get_fr_flow_sip_l(),
            req->get_fr_flow_dip_u(), req->get_fr_flow_dip_l(),
            family, &sip, &dip);
    f->fe_flags = VR_FLOW_FLAG_ACTIVE;
    f->fe_stats.flow_bytes = 30;
    f->fe_stats.flow_packets = 1;
    f->fe_gen_id = req->get_fr_gen_id();
    if (sip.is_v4()) {
        f->fe_key.flow4_sip = htonl(sip.to_v4().to_ulong());
        f->fe_key.flow4_dip = htonl(dip.to_v4().to_ulong());
    }
    if (family == Address::INET) {
        f->fe_key.flow4_nh_id = req->get_fr_flow_nh_id();
    } else {
        f->fe_key.flow6_nh_id = req->get_fr_flow_nh_id();
    }
    f->fe_key.flow_family = req->get_fr_family();
    f->fe_key.flow_sport = req->get_fr_flow_sport();
    f->fe_key.flow_dport = req->get_fr_flow_dport();
    f->fe_key.flow_nh_id = req->get_fr_flow_nh_id();
    f->fe_key.flow_proto = req->get_fr_flow_proto();
}

void KSyncSockTypeMap::SetEvictedFlag(int idx) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_flags |= VR_FLOW_FLAG_EVICTED;
    }
}

void KSyncSockTypeMap::ResetEvictedFlag(int idx) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_flags &= ~VR_FLOW_FLAG_EVICTED;
    }
}


void KSyncSockTypeMap::IncrFlowStats(int idx, int pkts, int bytes) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_stats.flow_bytes += bytes;
        f->fe_stats.flow_packets += pkts;
    }
}

void KSyncSockTypeMap::SetTcpFlag(int idx, uint32_t flags) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_tcp_flags = flags;
    }
}

void KSyncSockTypeMap::SetFlowTcpFlags(int idx, uint16_t flags) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_tcp_flags = flags;
    }
}

void KSyncSockTypeMap::SetUnderlaySourcePort(int idx, int port) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_udp_src_port = port;
    }
}

void KSyncSockTypeMap::SetOFlowStats(int idx, uint8_t pkts, uint16_t bytes) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_stats.flow_bytes_oflow = bytes;
        f->fe_stats.flow_packets_oflow = pkts;
    }
}

vr_bridge_entry *KSyncSockTypeMap::BridgeMmapAlloc(int size) {
    bridge_table_ = (vr_bridge_entry *)malloc(size);
    bzero(bridge_table_, size);
    return bridge_table_;
}

void KSyncSockTypeMap::BridgeMmapFree() {
    if (bridge_table_) {
        free(bridge_table_);
        bridge_table_ = NULL;
    }
}

vr_bridge_entry *KSyncSockTypeMap::GetBridgeEntry(int idx) {
    return &bridge_table_[idx];
}

void KSyncSockTypeMap::SetBridgeEntry(vr_route_req *req, bool set) {
    uint32_t idx = 0;
    vr_bridge_entry *be = &bridge_table_[idx];
    if (!set) {
        be->be_packets = 0;
        return;
    }

    if (be->be_packets == 0) {
        be->be_packets = 1;
    }
}

//init ksync map
void KSyncSockTypeMap::Init(boost::asio::io_service &ios) {
    assert(singleton_ == NULL);

    singleton_ = new KSyncSockTypeMap(ios);
    KSyncSock::SetSockTableEntry(singleton_);
    KSyncSock::Init(true, "disabled");

    singleton_->local_ep_.address
        (boost::asio::ip::address::from_string("127.0.0.1"));
    singleton_->local_ep_.port(0);
    singleton_->sock_.open(boost::asio::ip::udp::v4());
    singleton_->sock_.bind(singleton_->local_ep_);
    singleton_->local_ep_ = singleton_->sock_.local_endpoint();
}

void KSyncSockTypeMap::Shutdown() {
    delete singleton_;
    singleton_ = NULL;
}

void KSyncSockTypeMap::PurgeBlockedMsg() {
    while (!ctx_queue_.empty()) {
        ctx_queue_.front()->Process();
        delete ctx_queue_.front();
        ctx_queue_.pop();
    }
    PurgeTxBuffer();
}

void KSyncSockTypeMap::SetBlockMsgProcessing(bool enable) {
    tbb::mutex::scoped_lock lock(ctx_queue_lock_);
    if (block_msg_processing_ != enable) {
        block_msg_processing_ = enable;
        if (!block_msg_processing_) {
            PurgeBlockedMsg();
        }
    }
}

void KSyncUserSockIfContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from map if command is delete
    if (req_->get_h_op() == sandesh_op::DELETE) {
        sock->if_map.erase(req_->get_vifr_idx());
    } else if (req_->get_h_op() == sandesh_op::DUMP) {
        IfDumpHandler dump;
        dump.SendDumpResponse(GetSeqNum(), req_);
        return;
    } else if (req_->get_h_op() == sandesh_op::GET) {
        IfDumpHandler dump;
        dump.SendGetResponse(GetSeqNum(), req_->get_vifr_idx());
        return;
    } else {
        //store in the map
        vr_interface_req if_info(*req_);
        sock->if_map[req_->get_vifr_idx()] = if_info;
    }
    KSyncSockTypeMap::SimulateResponse(GetSeqNum(), 0, 0); 
}

void KSyncUserSockContext::IfMsgHandler(vr_interface_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockIfContext *ifctx = new KSyncUserSockIfContext(GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(ifctx);
    } else {
        ifctx->Process();
        delete ifctx;
    }
}

void KSyncUserSockFlowContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    uint16_t flags = 0;
    int flow_error = sock->GetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE);

    flags = req_->get_fr_flags();
    //delete from map if command is delete
    if (!flags) {
        sock->flow_map.erase(req_->get_fr_index());
        //Deactivate the flow-entry in flow mmap
        KSyncSockTypeMap::SetFlowEntry(req_, false);
    } else {
        uint32_t fwd_flow_idx = req_->get_fr_index();
        if (fwd_flow_idx == 0xFFFFFFFF) {
            if (flow_error == 0) {
                /* Allocate entry only of no error case */
                if (sock->is_incremental_index()) {
                    /* Send reverse-flow index as one more than fwd-flow index */
                    fwd_flow_idx = req_->get_fr_rindex() + 1;
                } else {
                    fwd_flow_idx = rand() % 20000;
                    /* Reserve first 20000 indexes for forwarding flow
                     * Reverse flow indexes will start from 20000 always
                     */
                    fwd_flow_idx += 20000;
                }
                /* If the randomly allocated index is used already then
                 * find out the next randon index which is free
                 */
                while (sock->flow_map.find(fwd_flow_idx) != sock->flow_map.end()) {
                    fwd_flow_idx = rand() % 20000;
                    /* Reserve first 20000 indexes for forwarding flow
                     * Reverse flow indexes will start from 20000 always
                     */
                    fwd_flow_idx += 20000;
                }
                req_->set_fr_index(fwd_flow_idx);
                req_->set_fr_gen_id((fwd_flow_idx % 255));
            }
        }          

        if (fwd_flow_idx != 0xFFFFFFFF) {
            //store info from binary sandesh message
            vr_flow_req flow_info(*req_);

            sock->flow_map[req_->get_fr_index()] = flow_info;

            //Activate the flow-entry in flow mmap
            KSyncSockTypeMap::SetFlowEntry(req_, true);
        }

        // For NAT flow, don't send vr_response, instead send
        // vr_flow_req with index of reverse_flow
        KSyncSockTypeMap::FlowNatResponse(GetSeqNum(), req_);
        return;
    }
    KSyncSockTypeMap::FlowNatResponse(GetSeqNum(), req_);
}

void KSyncUserSockContext::FlowMsgHandler(vr_flow_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockFlowContext *flowctx = new KSyncUserSockFlowContext(GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(flowctx);
    } else {
        flowctx->Process();
        delete flowctx;
    }
}

void KSyncUserSockNHContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from map if command is delete
    if (req_->get_h_op() == sandesh_op::DELETE) {
        sock->nh_map.erase(req_->get_nhr_id());
    } else if (req_->get_h_op() == sandesh_op::DUMP) {
        NHDumpHandler dump;
        dump.SendDumpResponse(GetSeqNum(), req_);
        return;
    } else if (req_->get_h_op() == sandesh_op::GET) {
        NHDumpHandler dump;
        dump.SendGetResponse(GetSeqNum(), req_->get_nhr_id());
        return;
    } else {
        //store in the map
        vr_nexthop_req nh_info(*req_);
        sock->nh_map[req_->get_nhr_id()] = nh_info;
    }
    KSyncSockTypeMap::SimulateResponse(GetSeqNum(), 0, 0);
}

void KSyncUserSockContext::NHMsgHandler(vr_nexthop_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockNHContext *nhctx = new KSyncUserSockNHContext(GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(nhctx);
    } else {
        nhctx->Process();
        delete nhctx;
    }
}

void KSyncUserSockMplsContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from map mpls command is delete
    if (req_->get_h_op() == sandesh_op::DELETE) {
        sock->mpls_map.erase(req_->get_mr_label());
    } else if (req_->get_h_op() == sandesh_op::DUMP) {
        MplsDumpHandler dump;
        dump.SendDumpResponse(GetSeqNum(), req_);
        return;
    } else if (req_->get_h_op() == sandesh_op::GET) {
        MplsDumpHandler dump;
        dump.SendGetResponse(GetSeqNum(), req_->get_mr_label());
        return;
    } else {
        //store in the map
        vr_mpls_req mpls_info(*req_);
        sock->mpls_map[req_->get_mr_label()] = mpls_info;
    }
    KSyncSockTypeMap::SimulateResponse(GetSeqNum(), 0, 0); 
}

void KSyncUserSockContext::MplsMsgHandler(vr_mpls_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockMplsContext *mplsctx = new KSyncUserSockMplsContext(GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(mplsctx);
    } else {
        mplsctx->Process();
        delete mplsctx;
    }
}

void KSyncUserSockRouteContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from the route tree, if the command is delete
    if (req_->get_h_op() == sandesh_op::DELETE) {
        sock->rt_tree.erase(*req_);
    } else if (req_->get_h_op() == sandesh_op::DUMP) {
        RouteDumpHandler dump;
        sock->SetBridgeEntry(req_, false);
        dump.SendDumpResponse(GetSeqNum(), req_);
        return;
    } else {
        //store in the route tree
        std::pair<std::set<vr_route_req>::iterator, bool> ret;
        ret = sock->rt_tree.insert(*req_);

        /* If insertion fails, remove the existing entry and add the new one */
        if (ret.second == false) {
            int del_count = sock->rt_tree.erase(*req_);
            assert(del_count);
            ret = sock->rt_tree.insert(*req_);
            assert(ret.second == true);
            if (req_->get_rtr_family() == AF_BRIDGE) {
                sock->SetBridgeEntry(req_, true);
            }
        }
    }
    KSyncSockTypeMap::SimulateResponse(GetSeqNum(), 0, 0); 
}

void KSyncUserSockContext::RouteMsgHandler(vr_route_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockRouteContext *rtctx = new KSyncUserSockRouteContext(GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(rtctx);
    } else {
        rtctx->Process();
        delete rtctx;
    }
}

void KSyncUserSockContext::QosConfigMsgHandler(vr_qos_map_req *req) {
    assert(0);
}

void KSyncUserSockContext::ForwardingClassMsgHandler(vr_fc_map_req *req) {
    assert(0);
}

void KSyncUserSockContext::MirrorMsgHandler(vr_mirror_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from map if command is delete
    if (req->get_h_op() == sandesh_op::DELETE) {
        sock->mirror_map.erase(req->get_mirr_index());
        KSyncSockTypeMap::SimulateResponse(GetSeqNum(), 0, 0);
        return;
    }

    if (req->get_h_op() == sandesh_op::DUMP) {
        MirrorDumpHandler dump;
        dump.SendDumpResponse(GetSeqNum(), req);
        return;
    }

    if (req->get_h_op() == sandesh_op::GET) {
        MirrorDumpHandler dump;
        dump.SendGetResponse(GetSeqNum(), req->get_mirr_index());
        return;
    }

    //store in the map
    vr_mirror_req mirror_info(*req);
    sock->mirror_map[req->get_mirr_index()] = mirror_info;
    KSyncSockTypeMap::SimulateResponse(GetSeqNum(), 0, 0);
}

void KSyncUserSockVxLanContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from map vxlan command is delete
    if (req_->get_h_op() == sandesh_op::DELETE) {
        sock->vxlan_map.erase(req_->get_vxlanr_vnid());
    } else if (req_->get_h_op() == sandesh_op::DUMP) {
        VxLanDumpHandler dump;
        dump.SendDumpResponse(GetSeqNum(), req_);
        return;
    } else if (req_->get_h_op() == sandesh_op::GET) {
        VxLanDumpHandler dump;
        dump.SendGetResponse(GetSeqNum(), req_->get_vxlanr_vnid());
        return;
    } else {
        //store in the map
        vr_vxlan_req vxlan_info(*req_);
        sock->vxlan_map[req_->get_vxlanr_vnid()] = vxlan_info;
    }
    KSyncSockTypeMap::SimulateResponse(GetSeqNum(), 0, 0);
}

void KSyncUserSockContext::VxLanMsgHandler(vr_vxlan_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockVxLanContext *vxlanctx = 
        new KSyncUserSockVxLanContext(GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(vxlanctx);
    } else {
        vxlanctx->Process();
        delete vxlanctx;
    }
}


void KSyncUserVrouterOpsContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    if (req_->get_h_op() == sandesh_op::GET) {
        VRouterOpsDumpHandler dump;
        sock->ksync_vrouter_ops.set_vo_mpls_labels(10000);
        dump.SendGetResponse(GetSeqNum(), 0);
        return;
    }
}
void KSyncUserSockContext::VrouterOpsMsgHandler(vrouter_ops *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserVrouterOpsContext *vrouter_ops =
        new KSyncUserVrouterOpsContext(GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(vrouter_ops);
    } else {
        vrouter_ops->Process();
        delete vrouter_ops;
    }
}

void KSyncUserSockVrfAssignContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from the vrf assign tree, if the command is delete
    if (req_->get_h_op() == sandesh_op::DELETE) {
        sock->vrf_assign_tree.erase(*req_);
    } else if (req_->get_h_op() == sandesh_op::DUMP) {
        VrfAssignDumpHandler dump;
        dump.SendDumpResponse(GetSeqNum(), req_);
        return;
    } else {
        //store in the vrf assign tree
        std::pair<std::set<vr_vrf_assign_req>::iterator, bool> ret;
        ret = sock->vrf_assign_tree.insert(*req_);

        /* If insertion fails, remove the existing entry and add the new one */
        if (ret.second == false) {
            int del_count = sock->vrf_assign_tree.erase(*req_);
            assert(del_count);
            ret = sock->vrf_assign_tree.insert(*req_);
            assert(ret.second == true);
        }
    }
    KSyncSockTypeMap::SimulateResponse(GetSeqNum(), 0, 0); 
}

void KSyncUserSockContext::VrfAssignMsgHandler(vr_vrf_assign_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockVrfAssignContext *ctx = 
        new KSyncUserSockVrfAssignContext(GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(ctx);
    } else {
        ctx->Process();
        delete ctx;
    }
}

void KSyncUserSockVrfStatsContext::Process() {
    if (req_->get_h_op() == sandesh_op::DUMP) {
        VrfStatsDumpHandler dump;
        dump.SendDumpResponse(GetSeqNum(), req_);
    } else if (req_->get_h_op() == sandesh_op::GET) {
        VrfStatsDumpHandler dump;
        dump.SendGetResponse(GetSeqNum(), req_->get_vsr_vrf());
    }
}

void KSyncUserSockContext::VrfStatsMsgHandler(vr_vrf_stats_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockVrfStatsContext *vrfctx = new KSyncUserSockVrfStatsContext(
                                                          GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(vrfctx);
    } else {
        vrfctx->Process();
        delete vrfctx;
    }
}

void KSyncUserSockDropStatsContext::Process() {
    if (req_->get_h_op() == sandesh_op::GET) {
        DropStatsDumpHandler dump;
        dump.SendGetResponse(GetSeqNum(), 0);
    }
}

void KSyncUserSockContext::DropStatsMsgHandler(vr_drop_stats_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncUserSockDropStatsContext *dropctx = new KSyncUserSockDropStatsContext(
                                                          GetSeqNum(), req);

    if (sock->IsBlockMsgProcessing()) {
        tbb::mutex::scoped_lock lock(sock->ctx_queue_lock_);
        sock->ctx_queue_.push(dropctx);
    } else {
        dropctx->Process();
        delete dropctx;
    }
}

void MockDumpHandlerBase::SendDumpResponse(uint32_t seq_num, Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    struct nl_client cl;
    int error = 0, ret;
    uint8_t *buf = NULL;
    uint32_t buf_len = 0, encode_len = 0, tot_encode_len = 0;
    struct nlmsghdr *nlh = NULL;
    bool more = false;
    int count = 0;
    unsigned int resp_code = 0;

    if (KSyncSockTypeMap::error_code()) {
        int ret_code = -KSyncSockTypeMap::error_code();
        ret_code &= ~VR_MESSAGE_DUMP_INCOMPLETE;
        KSyncSockTypeMap::SimulateResponse(seq_num, ret_code, 0);
        return;
    } 
    Sandesh *req = GetFirst(from_req);
    if (req != NULL) {
        nl_init_generic_client_req(&cl, KSyncSock::GetNetlinkFamilyId());
        if ((ret = nl_build_header(&cl, &buf, &buf_len)) < 0) {
            LOG(DEBUG, "Error creating interface DUMP message : " << ret);
            nl_free(&cl);
            return;
        }

        nlh = (struct nlmsghdr *)cl.cl_buf;
        nlh->nlmsg_seq = seq_num;
    }

    while(req != NULL) {
        encode_len = req->WriteBinary(buf, buf_len, &error);
        if (error != 0) {
            break;
        }
        buf += encode_len;
        buf_len -= encode_len;
        tot_encode_len += encode_len;
        count++;

        req = GetNext(req);
        //If the remaining buffer length cannot accomodate any more encoded
        //messages, quit from the loop.
        if (req != NULL && buf_len < encode_len) {
            more = true;
            break;
        }
    }

    if (error) {
        KSyncSockTypeMap::SimulateResponse(seq_num, -ENOENT, 0); 
        nl_free(&cl);
        return;
    }

    resp_code = count;
    if (count > 0) {
        resp_code = count;
        if (more) {
            resp_code = resp_code | VR_MESSAGE_DUMP_INCOMPLETE;
        }
        //Send Vr-Response (with multi-flag set)
        KSyncSockTypeMap::SimulateResponse(seq_num, resp_code, NLM_F_MULTI);

        //Send dump-response containing objects (with multi-flag set)
        nlh->nlmsg_flags |= NLM_F_MULTI;
        nl_update_header(&cl, tot_encode_len);
        sock->AddNetlinkTxBuff(&cl);
    } else {
        KSyncSockTypeMap::SimulateResponse(seq_num, resp_code, 0);
    }
}

void MockDumpHandlerBase::SendGetResponse(uint32_t seq_num, int idx) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    struct nl_client cl;
    int error = 0, ret;
    uint8_t *buf = NULL;
    uint32_t buf_len = 0, encode_len = 0;
    struct nlmsghdr *nlh;

    /* To simulate error code return the test code has to call 
     * KSyncSockTypeMap::set_error_code() with required error code and 
     * invoke get request */
    if (KSyncSockTypeMap::error_code()) {
        int ret_code = -KSyncSockTypeMap::error_code();
        ret_code &= ~VR_MESSAGE_DUMP_INCOMPLETE;
        KSyncSockTypeMap::SimulateResponse(seq_num, ret_code, 0);
        return;
    }
    Sandesh *req = Get(idx);
    if (req == NULL) {
        KSyncSockTypeMap::SimulateResponse(seq_num, -ENOENT, 0); 
        return;
    }
    nl_init_generic_client_req(&cl, KSyncSock::GetNetlinkFamilyId());
    if ((ret = nl_build_header(&cl, &buf, &buf_len)) < 0) {
        LOG(DEBUG, "Error creating interface DUMP message : " << ret);
        nl_free(&cl);
        return;
    }

    nlh = (struct nlmsghdr *)cl.cl_buf;
    nlh->nlmsg_seq = seq_num;

    int resp_len = EncodeVrResponse(buf, buf_len, seq_num, 0);
    buf += resp_len;
    buf_len -= resp_len;

    encode_len = req->WriteBinary(buf, buf_len, &error);
    if (error) {
        KSyncSockTypeMap::SimulateResponse(seq_num, -ENOENT, 0); 
        nl_free(&cl);
        return;
    }
    buf += encode_len;
    buf_len -= encode_len;

    nl_update_header(&cl, encode_len + resp_len);
    sock->AddNetlinkTxBuff(&cl);
}

Sandesh* IfDumpHandler::Get(int idx) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_if::const_iterator it;
    static vr_interface_req req;

    it = sock->if_map.find(idx);
    if (it != sock->if_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* IfDumpHandler::GetFirst(Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_if::const_iterator it;
    static vr_interface_req req;
    int idx;
    vr_interface_req *orig_req;
    orig_req = static_cast<vr_interface_req *>(from_req);

    idx = orig_req->get_vifr_marker();
    it = sock->if_map.upper_bound(idx);

    if (it != sock->if_map.end()) {
        req = it->second;
        req.set_vifr_flags(orig_req->get_vifr_flags());
        return &req;
    }
    return NULL;
}

Sandesh* IfDumpHandler::GetNext(Sandesh *input) {
    static int last_intf_id = 0;
    static int32_t last_if_flags = 0;
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_if::const_iterator it;
    static vr_interface_req req, *r;

    r = dynamic_cast<vr_interface_req *>(input);
    if (r != NULL) {
        /* GetNext on vr_interface_req should return a dummy drop-stats object.
         * We need to store the interface index which will be used during
         * GetNext of IfDumpHandler when invoked with vr_drop_stats_req as
         * argument */
        last_intf_id = r->get_vifr_idx();
        last_if_flags = r->get_vifr_flags();
        if (r->get_vifr_flags() & VIF_FLAG_GET_DROP_STATS) {
            return &drop_stats_req;
        }
    }
    it = sock->if_map.upper_bound(last_intf_id);

    if (it != sock->if_map.end()) {
        req = it->second;
        req.set_vifr_flags(last_if_flags);
        return &req;
    }
    return NULL;
}

Sandesh* NHDumpHandler::Get(int idx) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_nh::const_iterator it;
    static vr_nexthop_req req;

    it = sock->nh_map.find(idx);
    if (it != sock->nh_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* NHDumpHandler::GetFirst(Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_nh::const_iterator it;
    static vr_nexthop_req req;
    vr_nexthop_req *orig_req;
    orig_req = static_cast<vr_nexthop_req *>(from_req);
    int idx;

    idx = orig_req->get_nhr_marker();
    it = sock->nh_map.upper_bound(idx);
    if (it != sock->nh_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* NHDumpHandler::GetNext(Sandesh *input) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_nh::const_iterator it;
    static vr_nexthop_req req, *r;

    r = static_cast<vr_nexthop_req *>(input);
    it = sock->nh_map.upper_bound(r->get_nhr_id());

    if (it != sock->nh_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* MplsDumpHandler::Get(int idx) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mpls::const_iterator it;
    static vr_mpls_req req;

    it = sock->mpls_map.find(idx);
    if (it != sock->mpls_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* MplsDumpHandler::GetFirst(Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mpls::const_iterator it;
    static vr_mpls_req req;
    vr_mpls_req *orig_req;
    orig_req = static_cast<vr_mpls_req *>(from_req);
    int idx;

    idx = orig_req->get_mr_marker();
    it = sock->mpls_map.upper_bound(idx);

    if (it != sock->mpls_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* MplsDumpHandler::GetNext(Sandesh *input) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mpls::const_iterator it;
    static vr_mpls_req req, *r;

    r = static_cast<vr_mpls_req *>(input);
    it = sock->mpls_map.upper_bound(r->get_mr_label());

    if (it != sock->mpls_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* MirrorDumpHandler::Get(int idx) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mirror::const_iterator it;
    static vr_mirror_req req;

    it = sock->mirror_map.find(idx);
    if (it != sock->mirror_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* MirrorDumpHandler::GetFirst(Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mirror::const_iterator it;
    static vr_mirror_req req;
    vr_mirror_req *orig_req;
    orig_req = static_cast<vr_mirror_req *>(from_req);
    int idx;

    idx = orig_req->get_mirr_marker();
    it = sock->mirror_map.upper_bound(idx);

    if (it != sock->mirror_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* MirrorDumpHandler::GetNext(Sandesh *input) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_mirror::const_iterator it;
    static vr_mirror_req req, *r;

    r = static_cast<vr_mirror_req *>(input);
    it = sock->mirror_map.upper_bound(r->get_mirr_index());

    if (it != sock->mirror_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* RouteDumpHandler::GetFirst(Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_rt_tree::const_iterator it;
    static vr_route_req req;
    vr_route_req *orig_req, key;
    orig_req = static_cast<vr_route_req *>(from_req);

    if (orig_req->get_rtr_marker().size()) {
        key.set_rtr_vrf_id(orig_req->get_rtr_vrf_id());
        key.set_rtr_prefix(orig_req->get_rtr_marker());
        key.set_rtr_prefix_len(orig_req->get_rtr_marker_plen());
        it = sock->rt_tree.upper_bound(key);
    } else {
        std::vector<int8_t> rtr_prefix;
        key.set_rtr_vrf_id(orig_req->get_rtr_vrf_id());
        key.set_rtr_prefix(rtr_prefix);
        key.set_rtr_prefix_len(0);
        it = sock->rt_tree.lower_bound(key);
    }


    if (it != sock->rt_tree.end()) {
        req = *it;
        return &req;
    }
    return NULL;
}

Sandesh* RouteDumpHandler::GetNext(Sandesh *input) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_rt_tree::const_iterator it;
    static vr_route_req req, *r, key;

    r = static_cast<vr_route_req *>(input);

    key.set_rtr_vrf_id(r->get_rtr_vrf_id());
    key.set_rtr_prefix(r->get_rtr_prefix());
    key.set_rtr_prefix_len(r->get_rtr_prefix_len());
    it = sock->rt_tree.upper_bound(key);

    if (it != sock->rt_tree.end()) {
        req = *it;
        return &req;
    }
    return NULL;
}

Sandesh* VrfAssignDumpHandler::GetFirst(Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_vrf_assign_tree::const_iterator it;
    static vr_vrf_assign_req req;
    vr_vrf_assign_req *orig_req, key;
    orig_req = static_cast<vr_vrf_assign_req *>(from_req);

    key.set_var_vif_index(orig_req->get_var_vif_index());
    key.set_var_vlan_id(orig_req->get_var_marker());
    it = sock->vrf_assign_tree.upper_bound(key);

    if (it != sock->vrf_assign_tree.end()) {
        req = *it;
        return &req;
    }
    return NULL;
}

Sandesh* VrfAssignDumpHandler::GetNext(Sandesh *input) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_vrf_assign_tree::const_iterator it;
    static vr_vrf_assign_req req, *r, key;

    r = static_cast<vr_vrf_assign_req *>(input);

    key.set_var_vif_index(r->get_var_vif_index());
    key.set_var_vlan_id(r->get_var_vlan_id());
    it = sock->vrf_assign_tree.upper_bound(key);

    if (it != sock->vrf_assign_tree.end()) {
        req = *it;
        return &req;
    }
    return NULL;
}

Sandesh* VrfStatsDumpHandler::Get(int idx) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vrf_stats::const_iterator it;
    static vr_vrf_stats_req req;

    it = sock->vrf_stats_map.find(idx);
    if (it != sock->vrf_stats_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* VrfStatsDumpHandler::GetFirst(Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vrf_stats::const_iterator it;
    static vr_vrf_stats_req req;
    int idx;
    vr_vrf_stats_req *orig_req;
    orig_req = static_cast<vr_vrf_stats_req *>(from_req);

    idx = orig_req->get_vsr_marker();
    it = sock->vrf_stats_map.upper_bound(idx);

    if (it != sock->vrf_stats_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* VrfStatsDumpHandler::GetNext(Sandesh *input) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vrf_stats::const_iterator it;
    static vr_vrf_stats_req req, *r;

    r = static_cast<vr_vrf_stats_req *>(input);
    it = sock->vrf_stats_map.upper_bound(r->get_vsr_vrf());

    if (it != sock->vrf_stats_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* VxLanDumpHandler::Get(int idx) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vxlan::const_iterator it;
    static vr_vxlan_req req;

    it = sock->vxlan_map.find(idx);
    if (it != sock->vxlan_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* VxLanDumpHandler::GetFirst(Sandesh *from_req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vxlan::const_iterator it;
    static vr_vxlan_req req;
    vr_vxlan_req *orig_req;
    orig_req = static_cast<vr_vxlan_req *>(from_req);
    int idx;

    idx = orig_req->get_vxlanr_vnid();
    it = sock->vxlan_map.upper_bound(idx);

    if (it != sock->vxlan_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}

Sandesh* VxLanDumpHandler::GetNext(Sandesh *input) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_vxlan::const_iterator it;
    static vr_vxlan_req req, *r;

    r = static_cast<vr_vxlan_req *>(input);
    it = sock->vxlan_map.upper_bound(r->get_vxlanr_vnid());

    if (it != sock->vxlan_map.end()) {
        req = it->second;
        return &req;
    }
    return NULL;
}
