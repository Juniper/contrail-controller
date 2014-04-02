/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "nh_kstate.h"
#include "vr_nexthop.h"
#if defined(__linux__)
#include <linux/if_ether.h>
#elif defined(__FreeBSD__)
#include <net/ethernet.h>
#endif
#include <iomanip>
#include <sstream>

using namespace std;

NHKState::NHKState(KNHResp *obj, const std::string &resp_ctx, 
                   vr_nexthop_req &req, int id) 
    : KState(resp_ctx, obj) {

    req.set_nhr_id(id);
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
    } else {
        req.set_h_op(sandesh_op::DUMP);
        req.set_nhr_marker(-1);
    }
}

void NHKState::SendNextRequest() {
    vr_nexthop_req req;
    req.set_nhr_id(0);
    req.set_h_op(sandesh_op::DUMP);
    int idx = reinterpret_cast<long>(more_context_);
    req.set_nhr_marker(idx);
    EncodeAndSend(req);
}

void NHKState::Handler() {
    KNHResp *resp = static_cast<KNHResp *>(response_object_);
    if (resp) {
        if (MoreData()) {
            /* There are more nexthops in Kernel. We need to query them from 
             * Kernel and send it to Sandesh.
             */
            SendResponse();
            SendNextRequest();
        } else {
            resp->set_context(response_context_);
            resp->Response();
            more_context_ = NULL;
        }
    }
}

void NHKState::SendResponse() {

    KNHResp *resp = static_cast<KNHResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();

    response_object_ = new KNHResp();
}


const string NHKState::TypeToString(int nh_type) const {
    unsigned short type = nh_type;
    switch(type) {
        case NH_ENCAP:
            return "ENCAP";
        case NH_TUNNEL:
            return "TUNNEL";
        case NH_DISCARD:
            return "DISCARD";
        case NH_RCV:
            return "RECEIVE";
        case NH_RESOLVE:
            return "RESOLVE";
        case NH_COMPOSITE:
            return "COMPOSITE";
        case NH_DEAD:
            return "DEAD";
        default:
            return "INVALID";
    }
}

const string NHKState::FamilyToString(int nh_family) const {
    unsigned family = nh_family;
    switch(family) {
        case AF_INET:
            return "AF_INET";
        default:
            return "INVALID";
    }
}

const string NHKState::EncapFamilyToString(int nh_family) const {
    unsigned family = nh_family;
    switch(family) {
        case ETHERTYPE_ARP:
            return "ETH_P_ARP";
        case 0:
            return "NO_ENCAP";
        default:
            return "INVALID";
    }
}

const string NHKState::EncapToString(const vector<signed char> &encap) const {
    ostringstream strm;
    uint8_t ubyte;
    vector<signed char>::const_iterator it = encap.begin();
    strm << hex << setfill('0'); 
    while(it != encap.end()) {
        ubyte = (uint8_t) *it;
        strm << setw(2) << (int)ubyte;
        ++it;
   }
   return strm.str();
}

const string NHKState::FlagsToString(short nh_flags) const {
    unsigned short flags = nh_flags;
    string flag_str, policy_str("POLICY "), gre_str("TUNNEL_GRE ");
    string fabric_multicast("FABRIC_MULTICAST");
    string l2_multicast("L2_MULTICAST");
    string l3_multicast("L3_MULTICAST");
    string multi_proto_multicast("MULTI_PROTO_MULTICAST");
    string ecmp("ECMP");
    string multicast_encap("MULTICAST_ENCAP");
    string mpls_udp_str("TUNNEL_MPLS_UDP ");
    string udp_str("TUNNEL_UDP ");
    bool assigned = false;

    if (flags & NH_FLAG_VALID) {
        flag_str.assign("VALID ");
        assigned = true;
    }
    if (flags & NH_FLAG_POLICY_ENABLED) {
        if (assigned) {
            flag_str.append("| " + policy_str);
        } else {
            flag_str.assign(policy_str);
            assigned = true;
        }
    }
    if (flags & NH_FLAG_TUNNEL_GRE) {
        if (assigned) {
            flag_str.append("| " + gre_str);
        } else {
            flag_str.assign(gre_str);
            assigned = true;
        }
    }
    if (flags & NH_FLAG_TUNNEL_UDP) {
        if (assigned) {
            flag_str.append("| " + udp_str);
        } else {
            flag_str.assign(udp_str);
            assigned = true;
        }
    }
    if (flags & NH_FLAG_TUNNEL_UDP_MPLS) {
        if (assigned) {
            flag_str.append("| " + mpls_udp_str);
        } else {
            flag_str.assign(mpls_udp_str);
            assigned = true;
        }
    }

    if (flags & NH_FLAG_COMPOSITE_ECMP) {
        if (assigned) {
            flag_str.append("| " + ecmp);
        } else {
            flag_str.assign(ecmp);
            assigned = true;
        }
    }

    if (flags & NH_FLAG_COMPOSITE_FABRIC) {
        if (assigned) {
            flag_str.append("| " + fabric_multicast);
        } else {
            flag_str.assign(fabric_multicast);
            assigned = true;
        }
    }

    if (flags & NH_FLAG_COMPOSITE_MULTI_PROTO) {
        if (assigned) {
            flag_str.append("| " + multi_proto_multicast);
        } else {
            flag_str.assign(multi_proto_multicast);
            assigned = true;
        }
    }

    if (flags & NH_FLAG_COMPOSITE_L2) {
        if (assigned) {
            flag_str.append("| " + l2_multicast);
        } else {
            flag_str.assign(l2_multicast);
            assigned = true;
        }
    }

    if (flags & NH_FLAG_COMPOSITE_L3) {
        if (assigned) {
            flag_str.append("| " + l3_multicast);
        } else {
            flag_str.assign(l3_multicast);
            assigned = true;
        }
    }

    if (!assigned) {
        return "NIL";
    }
    return flag_str;
}

void NHKState::SetComponentNH(vr_nexthop_req *req, KNHInfo &info) {
    std::vector<KComponentNH> comp_nh_list;
    KComponentNH comp_nh;

    if (req->get_nhr_type() != NH_COMPOSITE) {
        return;
    }

    const std::vector<int32_t> nh_list = req->get_nhr_nh_list();
    const std::vector<int32_t> label_list = req->get_nhr_label_list();

    for (uint32_t i = 0; i < nh_list.size(); i++) {
        comp_nh.set_nh_id(nh_list[i]);
        comp_nh.set_label(label_list[i]);
        comp_nh_list.push_back(comp_nh);
    }
    info.set_component_nh(comp_nh_list);
}
