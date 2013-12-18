/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "interface_kstate.h"
#include <iomanip>
#include <sstream>
#include "vr_interface.h"

using namespace std;

InterfaceKState::InterfaceKState(KInterfaceResp *obj, std::string resp_ctx, 
                                 vr_interface_req &req, int id) : 
                                 KState(resp_ctx, obj) {
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
        req.set_vifr_idx(id);    
    } else {
        InitDumpRequest(req);
        req.set_vifr_marker(-1);
    }
}

void InterfaceKState::InitDumpRequest(vr_interface_req &req) {
    req.set_h_op(sandesh_op::DUMP);
    req.set_vifr_idx(0);
}

void InterfaceKState::Handler() {
    KInterfaceResp *resp = static_cast<KInterfaceResp *>(resp_obj_);
    if (resp) {
        if (MoreData()) {
            /* There are more interfaces in Kernel. We need to query them from 
             * Kernel and send it to Sandesh.
             */
            SendResponse();
            SendNextRequest();
        } else {
            resp->set_context(resp_ctx_);
            resp->Response();
            more_ctx_ = NULL;
        }
    }
}

void InterfaceKState::SendNextRequest() {
    vr_interface_req req;
    InitDumpRequest(req);
    int idx = reinterpret_cast<long>(more_ctx_);
    req.set_vifr_marker(idx);
    EncodeAndSend(req);
}

void InterfaceKState::SendResponse() {

    KInterfaceResp *resp = static_cast<KInterfaceResp *>(resp_obj_);
    resp->set_context(resp_ctx_);
    resp->set_more(true);
    resp->Response();

    resp_obj_ = new KInterfaceResp();
}

string InterfaceKState::TypeToString(int if_type) {
    unsigned short type = if_type;
    switch(type) {
        case VIF_TYPE_HOST:
            return "HOST";
        case VIF_TYPE_AGENT:
            return "AGENT";
        case VIF_TYPE_PHYSICAL:
            return "PHYSICAL";
        case VIF_TYPE_VIRTUAL:
            return "VIRTUAL";
        default:
            return "INVALID";
    }
}

string InterfaceKState::FlagsToString(int flags) {
    string str("");;
    if (flags == 0) {
        return "NIL";
    }
    if (flags & VIF_FLAG_POLICY_ENABLED) {
        str += "POLICY ";
    }
    if (flags & VIF_FLAG_MIRROR_RX) {
        str += "MIRR_RX ";
    }
    if (flags & VIF_FLAG_MIRROR_TX) {
        str += "MIRR_TX ";
    }
    return str;
}

string InterfaceKState::MacToString(const vector<signed char> &mac) {
    ostringstream strm;
    strm << hex << setfill('0') << setw(2) << (int)((uint8_t) mac.at(0)) << ":" 
         << setw(2) << (int)((uint8_t) mac.at(1)) << ":" << setw(2) 
         << (int)((uint8_t) mac.at(2)) << ":" << setw(2)
         << (int)((uint8_t) mac.at(3)) << ":" << setw(2) 
         << (int)((uint8_t) mac.at(4)) << ":" << setw(2)
         << (int)((uint8_t) mac.at(5));
    return strm.str();
}

