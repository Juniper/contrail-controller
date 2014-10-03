/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "oper/interface_common.h"
#include "interface_kstate.h"
#include "route_kstate.h"
#include "nh_kstate.h"
#include "mpls_kstate.h"
#include "flow_kstate.h"
#include "mirror_kstate.h"
#include "vrf_assign_kstate.h"
#include "vrf_stats_kstate.h"
#include "drop_stats_kstate.h"
#include "vxlan_kstate.h"
#include "kstate_io_context.h"
#include "vr_nexthop.h"
#include "vr_message.h"
#include <net/if.h>

using namespace std;

bool KState::MoreData() const {
    if ((vr_response_code_ & VR_MESSAGE_DUMP_INCOMPLETE))
        return true;
    return false;
}

int KState::VrResponseMsgHandler(vr_response *r) {
    int code = r->get_resp_code();

    KState *st = static_cast<KState *>(this);
    st->set_vr_response_code(code);
    if (code == -ENOENT) {
        ErrResp *resp = new ErrResp();
        resp->set_context(st->response_context());
        resp->Response();

        st->Release();
        return 0;
    }
    
    if (code < 0) {
        InternalErrResp *resp = new InternalErrResp();
        resp->set_context(st->response_context());
        resp->Response();

        st->Release();
        LOG(ERROR, "Error: " << strerror(-code) << " :code: " << -code);
        return -code;
    }

    return 0;
}

void KState::EncodeAndSend(Sandesh &encoder) {

    int encode_len, error;
    uint8_t *buf = (uint8_t *)malloc(KSYNC_DEFAULT_MSG_SIZE);
    KSyncSock   *sock = KSyncSock::Get(0);

    encode_len = encoder.WriteBinary(buf, KSYNC_DEFAULT_MSG_SIZE, &error);

    AgentSandeshContext *sctx = static_cast<AgentSandeshContext *>(this);
    KStateIoContext *ioc = new KStateIoContext(encode_len, (char *)buf,
                                               sock->AllocSeqNo(false), sctx);
    sock->GenericSend(ioc);
}

void KState::UpdateContext(void *ctx) {
    more_context_ = ctx;
}

void KState::IfMsgHandler(vr_interface_req *r) {

    KInterfaceInfo data;
    const Interface *intf;

    InterfaceKState *ist = static_cast<InterfaceKState *>(this);
    KInterfaceResp *resp = static_cast<KInterfaceResp *>(ist->response_object());

    vector<KInterfaceInfo> &list =
                        const_cast<std::vector<KInterfaceInfo>&>(resp->get_if_list());
    data.set_type(ist->TypeToString(r->get_vifr_type()));
    data.set_flags(ist->FlagsToString(r->get_vifr_flags()));
    data.set_mirror_id(r->get_vifr_mir_id());
    data.set_vrf(r->get_vifr_vrf());
    data.set_idx(r->get_vifr_idx());
    data.set_rid(r->get_vifr_rid());
    data.set_os_idx(r->get_vifr_os_idx());
    data.set_mtu(r->get_vifr_mtu());

    intf = InterfaceTable::GetInstance()->FindInterface(r->get_vifr_idx());
    if (!intf) {
        data.set_name(string("NULL"));
    } else {
        data.set_name(string(intf->name()));
    }

    Ip4Address ipaddr(r->get_vifr_ip());
    data.set_ip(ipaddr.to_string());

    data.set_ibytes(r->get_vifr_ibytes());
    data.set_ipackets(r->get_vifr_ipackets());
    data.set_ierrors(r->get_vifr_ierrors());
    data.set_obytes(r->get_vifr_obytes());
    data.set_opackets(r->get_vifr_opackets());
    data.set_oerrors(r->get_vifr_oerrors());

    data.set_ref_cnt(r->get_vifr_ref_cnt());
    data.set_speed(r->get_vifr_speed());
    data.set_duplexity(r->get_vifr_duplex());

    data.set_mac(ist->MacToString(r->get_vifr_mac()));
    list.push_back(data);

    UpdateContext(reinterpret_cast<void *>(r->get_vifr_idx()));
}

void KState::NHMsgHandler(vr_nexthop_req *r) {

    KNHInfo data;
    NHKState *nhst;

    nhst = static_cast<NHKState *>(this);
    KNHResp *resp = static_cast<KNHResp *>(nhst->response_object());

    vector<KNHInfo> &list =
                        const_cast<std::vector<KNHInfo>&>(resp->get_nh_list());
    data.set_type(nhst->TypeToString(r->get_nhr_type()));
    data.set_family(nhst->FamilyToString(r->get_nhr_family()));
    data.set_id(r->get_nhr_id());
    data.set_rid(r->get_nhr_rid());
    if (r->get_nhr_type() != NH_COMPOSITE) {
        data.set_encap_oif_id(r->get_nhr_encap_oif_id());
        data.set_encap_family(nhst->EncapFamilyToString(
                              r->get_nhr_encap_family()));
    }
    data.set_vrf(r->get_nhr_vrf());
    if (r->get_nhr_type() == NH_TUNNEL) {
        Ip4Address sip((uint32_t)r->get_nhr_tun_sip());
        Ip4Address dip((uint32_t)r->get_nhr_tun_dip());
        data.set_tun_sip(sip.to_string());
        data.set_tun_dip(dip.to_string());
        if (r->get_nhr_flags() & NH_FLAG_TUNNEL_UDP) {
            data.set_tun_sport(ntohs(r->get_nhr_tun_sport()));
            data.set_tun_dport(ntohs(r->get_nhr_tun_dport()));
        }
    }

    data.set_ref_cnt(r->get_nhr_ref_cnt());
    data.set_flags(nhst->FlagsToString(r->get_nhr_flags()));
    const vector<signed char> &encap = r->get_nhr_encap();
    /* Kernel does not return encap len via r->get_nhr_encap_len() 
     * We need to fill it via the encap vector's size. */
    if (encap.size()) {
        data.set_encap_len(encap.size());
        data.set_encap(nhst->EncapToString(encap));
    }

    nhst->SetComponentNH(r, data);
    list.push_back(data);
    UpdateContext(reinterpret_cast<void *>(r->get_nhr_id()));
}

const std::string KState::PrefixToString(const std::vector<int8_t> &prefix) {

    int size = prefix.size();
    string str = "unknown";
    if (size <= 4) {
        boost::array<unsigned char, 4> bytes = { {0, 0, 0, 0} };
        for (int i = 0; i < size; i++) {
            bytes[i] = prefix.at(i);
        }
        Ip4Address addr4(bytes);
        str = addr4.to_string();
    } else {
        boost::array<unsigned char, 16> bytes =
        { {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} };
        for (int i = 0; i < size; i++) {
            bytes[i] = prefix.at(i);
        }
        Ip6Address addr6(bytes);
        str = addr6.to_string();
    }
    return str;
}

void KState::RouteMsgHandler(vr_route_req *r) {

    KRouteInfo data;
    RouteKState *rst;

    rst = static_cast<RouteKState *>(this);
    KRouteResp *resp = static_cast<KRouteResp *>(rst->response_object());

    vector<KRouteInfo> &list =
                        const_cast<std::vector<KRouteInfo>&>(resp->get_rt_list());
    
    data.set_vrf_id(r->get_rtr_vrf_id());
    data.set_family(rst->FamilyToString(r->get_rtr_family()));
    data.set_prefix(PrefixToString(r->get_rtr_prefix()));
    data.set_prefix_len(r->get_rtr_prefix_len());
    data.set_rid(r->get_rtr_rid());
    data.set_label_flags(rst->LabelFlagsToString(
                                  r->get_rtr_label_flags()));
    data.set_label(r->get_rtr_label());
    data.set_nh_id(r->get_rtr_nh_id());
    
    list.push_back(data);

    RouteContext *rctx = static_cast<RouteContext *>(rst->more_context());
    if (!rctx) {
        rctx = new RouteContext;
    }
    rctx->vrf_id = r->get_rtr_vrf_id();
    rctx->marker = r->get_rtr_prefix();
    rctx->marker_plen = r->get_rtr_prefix_len();
    UpdateContext(rctx);
}

void KState::MplsMsgHandler(vr_mpls_req *r) {

    KMplsInfo data;
    MplsKState *mst;

    mst = static_cast<MplsKState *>(this);
    KMplsResp *resp = static_cast<KMplsResp *>(mst->response_object());

    vector<KMplsInfo> &list =
                        const_cast<std::vector<KMplsInfo>&>(resp->get_mpls_list());
    data.set_label(r->get_mr_label());
    data.set_rid(r->get_mr_rid());
    data.set_nhid(r->get_mr_nhid());

    list.push_back(data);

    int label = r->get_mr_label();
    UpdateContext(reinterpret_cast<void *>(label));
}

void KState::MirrorMsgHandler(vr_mirror_req *r) {
    KMirrorInfo data;
    MirrorKState *mst;

    mst = static_cast<MirrorKState *>(this);
    KMirrorResp *resp = static_cast<KMirrorResp *>(mst->response_object());

    vector<KMirrorInfo> &list =
        const_cast<std::vector<KMirrorInfo>&>(resp->get_mirror_list());

    data.set_mirr_index(r->get_mirr_index());
    data.set_mirr_rid(r->get_mirr_rid());
    data.set_mirr_nhid(r->get_mirr_nhid());
    data.set_mirr_flags(mst->FlagsToString(r->get_mirr_flags()));
    data.set_mirr_users(r->get_mirr_users());

    list.push_back(data);
    
    int mirror_id = r->get_mirr_index();
    UpdateContext(reinterpret_cast<void *>(mirror_id));
}

void KState::VrfAssignMsgHandler(vr_vrf_assign_req *r) {
    KVrfAssignInfo data;
    VrfAssignKState *state;

    state = static_cast<VrfAssignKState *>(this);
    KVrfAssignResp *resp = 
        static_cast<KVrfAssignResp *>(state->response_object());

    vector<KVrfAssignInfo> &list =
        const_cast<std::vector<KVrfAssignInfo>&>(resp->get_vrf_assign_list());
    data.set_vif_index(r->get_var_vif_index());
    data.set_vlan_id(r->get_var_vlan_id());
    data.set_vif_vrf(r->get_var_vif_vrf());
    list.push_back(data);

    // Update the last interface and tag seen. 
    // Will be used to send next request to kernel
    VrfAssignContext *ctx = 
        static_cast<VrfAssignContext *>(state->more_context());
    if (!ctx) {
        ctx = new VrfAssignContext;
    }
    ctx->vif_index_ = r->get_var_vif_index();
    ctx->marker_ = r->get_var_vlan_id();
    UpdateContext(ctx);
}

void KState::VrfStatsMsgHandler(vr_vrf_stats_req *r) {
    KVrfStatsInfo data;
    VrfStatsKState *state;

    state = static_cast<VrfStatsKState *>(this);
    KVrfStatsResp *resp = 
        static_cast<KVrfStatsResp *>(state->response_object());

    vector<KVrfStatsInfo> &list =
        const_cast<std::vector<KVrfStatsInfo>&>(resp->get_vrf_stats_list());
    data.set_vrf_id(r->get_vsr_vrf());
    data.set_vrf_family(state->FamilyToString(r->get_vsr_family()));
    data.set_vrf_type(state->TypeToString(r->get_vsr_type()));
    data.set_vrf_rid(r->get_vsr_rid());
    data.set_vrf_discards(r->get_vsr_discards());
    data.set_vrf_resolves(r->get_vsr_resolves());
    data.set_vrf_receives(r->get_vsr_receives());
    data.set_vrf_udp_tunnels(r->get_vsr_udp_tunnels());
    data.set_vrf_udp_mpls_tunnels(r->get_vsr_udp_mpls_tunnels());
    data.set_vrf_gre_mpls_tunnels(r->get_vsr_gre_mpls_tunnels());
    data.set_vrf_l3_mcast_composites(r->get_vsr_l3_mcast_composites());
    data.set_vrf_l2_mcast_composites(r->get_vsr_l2_mcast_composites());
    data.set_vrf_multi_proto_composites(r->get_vsr_multi_proto_composites());
    data.set_vrf_fabric_composites(r->get_vsr_fabric_composites());
    data.set_vrf_ecmp_composites(r->get_vsr_ecmp_composites());
    data.set_vrf_encaps(r->get_vsr_encaps());
    list.push_back(data);

    UpdateContext(reinterpret_cast<void *>(r->get_vsr_vrf()));
}

void KState::VxLanMsgHandler(vr_vxlan_req *r) {

    KVxLanInfo data;
    VxLanKState *mst;

    mst = static_cast<VxLanKState *>(this);
    KVxLanResp *resp = static_cast<KVxLanResp *>(mst->response_object());

    vector<KVxLanInfo> &list =
                        const_cast<std::vector<KVxLanInfo>&>(resp->get_vxlan_list());
    data.set_vxlanid(r->get_vxlanr_vnid());
    data.set_rid(r->get_vxlanr_rid());
    data.set_nhid(r->get_vxlanr_nhid());

    list.push_back(data);

    int label = r->get_vxlanr_vnid();
    UpdateContext(reinterpret_cast<void *>(label));
}

void KState::DropStatsMsgHandler(vr_drop_stats_req *req) {
    DropStatsKState *state;

    state = static_cast<DropStatsKState *>(this);
    KDropStatsResp *resp = 
        static_cast<KDropStatsResp *>(state->response_object());
    resp->set_ds_rid(req->get_vds_rid());
    resp->set_ds_discard(req->get_vds_discard());
    resp->set_ds_pull(req->get_vds_pull());
    resp->set_ds_invalid_if(req->get_vds_invalid_if());
    resp->set_ds_arp_not_me(req->get_vds_arp_not_me());
    resp->set_ds_garp_from_vm(req->get_vds_garp_from_vm());
    resp->set_ds_invalid_arp(req->get_vds_invalid_arp());
    resp->set_ds_trap_no_if(req->get_vds_trap_no_if());
    resp->set_ds_nowhere_to_go(req->get_vds_nowhere_to_go());
    resp->set_ds_flow_queue_limit_exceeded(req->get_vds_flow_queue_limit_exceeded());
    resp->set_ds_flow_no_memory(req->get_vds_flow_no_memory());
    resp->set_ds_flow_invalid_protocol(req->get_vds_flow_invalid_protocol());
    resp->set_ds_flow_nat_no_rflow(req->get_vds_flow_nat_no_rflow());
    resp->set_ds_flow_action_drop(req->get_vds_flow_action_drop());
    resp->set_ds_flow_action_invalid(req->get_vds_flow_action_invalid());
    resp->set_ds_flow_unusable(req->get_vds_flow_unusable());
    resp->set_ds_flow_table_full(req->get_vds_flow_table_full());
    resp->set_ds_interface_tx_discard(req->get_vds_interface_tx_discard());
    resp->set_ds_interface_drop(req->get_vds_interface_drop());
    resp->set_ds_duplicated(req->get_vds_duplicated());
    resp->set_ds_push(req->get_vds_push());
    resp->set_ds_ttl_exceeded(req->get_vds_ttl_exceeded());
    resp->set_ds_invalid_nh(req->get_vds_invalid_nh());
    resp->set_ds_invalid_label(req->get_vds_invalid_label());
    resp->set_ds_invalid_protocol(req->get_vds_invalid_protocol());
    resp->set_ds_interface_rx_discard(req->get_vds_interface_rx_discard());
    resp->set_ds_invalid_mcast_source(req->get_vds_invalid_mcast_source());
    resp->set_ds_head_alloc_fail(req->get_vds_head_alloc_fail());
    resp->set_ds_head_space_reserve_fail(req->get_vds_head_space_reserve_fail());
    resp->set_ds_pcow_fail(req->get_vds_pcow_fail());
    resp->set_ds_flood(req->get_vds_flood());
    resp->set_ds_mcast_clone_fail(req->get_vds_mcast_clone_fail());
    resp->set_ds_composite_invalid_interface(req->get_vds_composite_invalid_interface());
    resp->set_ds_rewrite_fail(req->get_vds_rewrite_fail());
    resp->set_ds_misc(req->get_vds_misc());
    resp->set_ds_invalid_packet(req->get_vds_invalid_packet());
    resp->set_ds_cksum_err(req->get_vds_cksum_err());
    resp->set_ds_clone_fail(req->get_vds_clone_fail());
    resp->set_ds_no_fmd(req->get_vds_no_fmd());
    resp->set_ds_cloned_original(req->get_vds_cloned_original());
    resp->set_ds_invalid_vnid(req->get_vds_invalid_vnid());
    resp->set_ds_frag_err(req->get_vds_frag_err());
}

