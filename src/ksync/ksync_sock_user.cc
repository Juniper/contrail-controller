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

//store ops data
void vrouter_ops_test::Process(SandeshContext *context) {
}

//process sandesh messages that are being sent from the agent
//this is used to store a local copy of what is being send to kernel
void KSyncSockTypeMap::ProcessSandesh(const uint8_t *parse_buf, size_t buf_len, 
                                      KSyncUserSockContext *ctx) {
    int decode_len;
    uint8_t *decode_buf;

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
        vr_response encoder;
        encoder.set_h_op(sandesh_op::RESPONSE);
        encoder.set_resp_code(flow_error);
        encode_len = encoder.WriteBinary(buf, buf_len, &error);
        if (error != 0) {
            SimulateResponse(seq_num, -ENOENT, 0); 
            nl_free(&cl);
            return;
        }
        buf += encode_len;
        buf_len -= encode_len;
    }
    req->set_fr_op(flow_op::FLOW_SET);
    encode_len += req->WriteBinary(buf, buf_len, &error);
    if (error != 0) {
        SimulateResponse(seq_num, -ENOENT, 0); 
        nl_free(&cl);
        return;
    }

    nl_update_header(&cl, encode_len);
    sock->sock_.send_to(buffer(cl.cl_buf, cl.cl_msg_len), sock->local_ep_);
    nl_free(&cl);
}

void KSyncSockTypeMap::SendNetlinkDoneMsg(int seq_num) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    struct nlmsghdr nlh;
    nlh.nlmsg_seq = seq_num;
    nlh.nlmsg_type = NLMSG_DONE;
    nlh.nlmsg_len = NLMSG_HDRLEN;
    nlh.nlmsg_flags = 0;
    sock->sock_.send_to(buffer(&nlh, NLMSG_HDRLEN), sock->local_ep_);
}

void KSyncSockTypeMap::SimulateResponse(uint32_t seq_num, int code, int flags) {
    struct nl_client cl;
    vr_response encoder;
    int encode_len, error, ret;
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
    encoder.set_h_op(sandesh_op::RESPONSE);
    encoder.set_resp_code(code);
    encode_len = encoder.WriteBinary(buf, buf_len, &error);
    nl_update_header(&cl, encode_len);
    LOG(DEBUG, "SimulateResponse " << " seq " << seq_num << " code " << std::hex << code);

    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    sock->sock_.send_to(buffer(cl.cl_buf, cl.cl_msg_len), sock->local_ep_);
    nl_free(&cl);
}

void KSyncSockTypeMap::SetDropStats(const vr_drop_stats_req &req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    sock->drop_stats = req;
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
    vrf_stats.set_vsr_type(RT_UCAST);
    vrf_stats.set_vsr_rid(0);
    vrf_stats.set_vsr_discards(0);
    vrf_stats.set_vsr_resolves(0);
    vrf_stats.set_vsr_receives(0);
    vrf_stats.set_vsr_udp_tunnels(0);
    vrf_stats.set_vsr_udp_mpls_tunnels(0);
    vrf_stats.set_vsr_gre_mpls_tunnels(0);
    vrf_stats.set_vsr_ecmp_composites(0);
    vrf_stats.set_vsr_fabric_composites(0);
    vrf_stats.set_vsr_l3_mcast_composites(0);
    vrf_stats.set_vsr_l2_mcast_composites(0);
    vrf_stats.set_vsr_multi_proto_composites(0);
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

void KSyncSockTypeMap::VrfStatsUpdate(int vrf_id, uint64_t discards, uint64_t resolves, 
                    uint64_t receives, uint64_t udp_tunnels, 
                    uint64_t udp_mpls_tunnels, 
                    uint64_t gre_mpls_tunnels, 
                    int64_t ecmp_composites, 
                    int64_t fabric_composites,
                    int64_t l2_mcast_composites,
                    int64_t l3_mcast_composites,
                    int64_t multi_proto_composites,
                    uint64_t encaps, uint64_t l2_encaps) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    vr_vrf_stats_req &vrf_stats = sock->vrf_stats_map[vrf_id];
    vrf_stats.set_vsr_discards(discards);
    vrf_stats.set_vsr_resolves(resolves);
    vrf_stats.set_vsr_receives(receives);
    vrf_stats.set_vsr_udp_tunnels(udp_tunnels);
    vrf_stats.set_vsr_udp_mpls_tunnels(udp_mpls_tunnels);
    vrf_stats.set_vsr_gre_mpls_tunnels(gre_mpls_tunnels);
    vrf_stats.set_vsr_ecmp_composites(ecmp_composites);
    vrf_stats.set_vsr_fabric_composites(fabric_composites);
    vrf_stats.set_vsr_l3_mcast_composites(l3_mcast_composites);
    vrf_stats.set_vsr_l2_mcast_composites(l2_mcast_composites);
    vrf_stats.set_vsr_multi_proto_composites(multi_proto_composites);
    vrf_stats.set_vsr_encaps(encaps);
    vrf_stats.set_vsr_l2_encaps(l2_encaps);
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

    return ((nlh->nlmsg_flags & NLM_F_MULTI) && (nlh->nlmsg_type != NLMSG_DONE));
}

void KSyncSockTypeMap::Decoder(char *data, SandeshContext *ctxt) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    //LOG(DEBUG, "Kernel Data: msg_type " << nlh->nlmsg_type << " seq no " 
    //                << nlh->nlmsg_seq << " len " << nlh->nlmsg_len);
    if (nlh->nlmsg_type == GetNetlinkFamilyId()) {
        struct genlmsghdr *genlh = (struct genlmsghdr *)
                                   (data + NLMSG_HDRLEN);
        int total_len = nlh->nlmsg_len;
        int decode_len;
        uint8_t *decode_buf;
        if (genlh->cmd == SANDESH_REQUEST) {
            struct nlattr * attr = (struct nlattr *)(data + NLMSG_HDRLEN
                                                     + GENL_HDRLEN);
            int decode_buf_len = total_len - (NLMSG_HDRLEN + GENL_HDRLEN + 
                                              NLA_HDRLEN);
            int err = 0;
            if (attr->nla_type == NL_ATTR_VR_MESSAGE_PROTOCOL) {
                decode_buf = (uint8_t *)(data + NLMSG_HDRLEN + 
                                         GENL_HDRLEN + NLA_HDRLEN);
                while(decode_buf_len > (NLA_ALIGNTO - 1)) {
                    decode_len = Sandesh::ReceiveBinaryMsgOne(decode_buf, decode_buf_len, &err,
                                                              ctxt);
                    if (decode_len < 0) {
                        LOG(DEBUG, "Incorrect decode len " << decode_len);
                        break;
                    }
                    decode_buf += decode_len;
                    decode_buf_len -= decode_len;
                }
            } else {
                LOG(ERROR, "Unknown generic netlink TLV type : " << attr->nla_type);
                assert(0);
            }
        } else {
            LOG(ERROR, "Unknown generic netlink cmd : " << genlh->cmd);
            assert(0);
        }
    } else if (nlh->nlmsg_type != NLMSG_DONE) {
        LOG(ERROR, "Netlink unknown message type : " << nlh->nlmsg_type);
        assert(0);
    }
    
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

//send or store in map
void KSyncSockTypeMap::AsyncSendTo(IoContext *ioc, mutable_buffers_1 buf,
                                   HandlerCb cb) {
    KSyncUserSockContext ctx(true, ioc->GetSeqno());
    //parse and store info in map [done in Process() callbacks]
    ProcessSandesh(buffer_cast<const uint8_t *>(buf), buffer_size(buf), &ctx);

    if (ctx.IsResponseReqd()) {
        //simulate ok response with the same seq
        SimulateResponse(ioc->GetSeqno(), 0, 0); 
    }
}

//send or store in map
size_t KSyncSockTypeMap::SendTo(const_buffers_1 buf, uint32_t seq_no) {
    KSyncUserSockContext ctx(true, seq_no);
    //parse and store info in map [done in Process() callbacks]
    ProcessSandesh(buffer_cast<const uint8_t *>(buf), buffer_size(buf), &ctx);

    if (ctx.IsResponseReqd()) {
        //simulate ok response with the same seq
        SimulateResponse(seq_no, 0, 0); 
    }
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
        return;
    }

    f->fe_flags |= VR_FLOW_FLAG_ACTIVE;
    f->fe_stats.flow_bytes = 30;
    f->fe_stats.flow_packets = 1;
    f->fe_key.key_src_ip = req->get_fr_flow_sip();
    f->fe_key.key_dest_ip = req->get_fr_flow_dip();
    f->fe_key.key_src_port = req->get_fr_flow_sport();
    f->fe_key.key_dst_port = req->get_fr_flow_dport();
    f->fe_key.key_nh_id = req->get_fr_flow_nh_id();
    f->fe_key.key_proto = req->get_fr_flow_proto();
}

void KSyncSockTypeMap::IncrFlowStats(int idx, int pkts, int bytes) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_stats.flow_bytes += bytes;
        f->fe_stats.flow_packets += pkts;
    }
}

void KSyncSockTypeMap::SetOFlowStats(int idx, uint8_t pkts, uint16_t bytes) {
    vr_flow_entry *f = &flow_table_[idx];
    if (f->fe_flags & VR_FLOW_FLAG_ACTIVE) {
        f->fe_stats.flow_bytes_oflow = bytes;
        f->fe_stats.flow_packets_oflow = pkts;
    }
}

//init ksync map
void KSyncSockTypeMap::Init(boost::asio::io_service &ios, int count) {
    KSyncSock::Init(count);
    assert(singleton_ == NULL);
    singleton_ = new KSyncSockTypeMap(ios);

    singleton_->local_ep_.address(ip::address::from_string("127.0.0.1"));
    singleton_->local_ep_.port(0);
    singleton_->sock_.open(ip::udp::v4());
    singleton_->sock_.bind(singleton_->local_ep_);
    singleton_->local_ep_ = singleton_->sock_.local_endpoint();

    //update map for Sandesh callback of Process()
    SandeshBaseFactory::map_type update_map;
    update_map["vrouter_ops"] = &createT<vrouter_ops_test>;
    SandeshBaseFactory::Update(update_map);
    for (int i = 0; i < count; i++) {
        KSyncSock::SetSockTableEntry(i, singleton_);
    }
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
    SetResponseReqd(false);
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
        /* Send reverse-flow index as one more than fwd-flow index */
        uint32_t fwd_flow_idx = req_->get_fr_index();
        if (fwd_flow_idx == 0xFFFFFFFF) {
            if (flow_error == 0) {
                /* Allocate entry only of no error case */
                fwd_flow_idx = rand() % 50000;
                req_->set_fr_index(fwd_flow_idx);
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
        SetResponseReqd(false);
        KSyncSockTypeMap::FlowNatResponse(GetSeqNum(), req_);
        return;
    }
    SetResponseReqd(false);
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
    SetResponseReqd(false);
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
    SetResponseReqd(false);
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
    SetResponseReqd(false);
}

void KSyncUserSockRouteContext::Process() {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from the route tree, if the command is delete
    if (req_->get_h_op() == sandesh_op::DELETE) {
        sock->rt_tree.erase(*req_);
    } else if (req_->get_h_op() == sandesh_op::DUMP) {
        RouteDumpHandler dump;
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
    SetResponseReqd(false);
}

void KSyncUserSockContext::MirrorMsgHandler(vr_mirror_req *req) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();

    //delete from map if command is delete
    if (req->get_h_op() == sandesh_op::DELETE) {
        sock->mirror_map.erase(req->get_mirr_index());
        return;
    }

    if (req->get_h_op() == sandesh_op::DUMP) {
        SetResponseReqd(false);
        MirrorDumpHandler dump;
        dump.SendDumpResponse(GetSeqNum(), req);
        return;
    }

    if (req->get_h_op() == sandesh_op::GET) {
        SetResponseReqd(false);
        MirrorDumpHandler dump;
        dump.SendGetResponse(GetSeqNum(), req->get_mirr_index());
        return;
    }

    //store in the map
    vr_mirror_req mirror_info(*req);
    sock->mirror_map[req->get_mirr_index()] = mirror_info;

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
    SetResponseReqd(false);
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
    SetResponseReqd(false);
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
    SetResponseReqd(false);
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
    SetResponseReqd(false);
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
        sock->sock_.send_to(buffer(cl.cl_buf, cl.cl_msg_len), sock->local_ep_);
        nl_free(&cl);
        //Send Netlink-Done message
        KSyncSockTypeMap::SendNetlinkDoneMsg(seq_num);
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
        KSyncSockTypeMap::SimulateResponse(seq_num, -KSyncSockTypeMap::error_code(), 0); 
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

    encode_len = req->WriteBinary(buf, buf_len, &error);
    if (error) {
        KSyncSockTypeMap::SimulateResponse(seq_num, -ENOENT, 0); 
        nl_free(&cl);
        return;
    }
    buf += encode_len;
    buf_len -= encode_len;

    nl_update_header(&cl, encode_len);
    sock->sock_.send_to(buffer(cl.cl_buf, cl.cl_msg_len), sock->local_ep_);
    nl_free(&cl);
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
        return &req;
    }
    return NULL;
}

Sandesh* IfDumpHandler::GetNext(Sandesh *input) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    KSyncSockTypeMap::ksync_map_if::const_iterator it;
    static vr_interface_req req, *r;

    r = static_cast<vr_interface_req *>(input);
    it = sock->if_map.upper_bound(r->get_vifr_idx());

    if (it != sock->if_map.end()) {
        req = it->second;
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

