/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "interface_kstate.h"
#include <iomanip>
#include <sstream>
#include "vr_interface.h"

using namespace std;

InterfaceKState::InterfaceKState(KInterfaceResp *obj, const std::string &ctx,
                                 vr_interface_req &req, int id) : 
                                 KState(ctx, obj) {
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
        req.set_vifr_idx(id);    
    } else {
        InitDumpRequest(req);
        req.set_vifr_marker(-1);
    }
}

void InterfaceKState::InitDumpRequest(vr_interface_req &req) const {
    req.set_h_op(sandesh_op::DUMP);
    req.set_vifr_idx(0);
}

void InterfaceKState::Handler() {
    KInterfaceResp *resp = static_cast<KInterfaceResp *>(response_object_);
    if (resp) {
        if (MoreData()) {
            /* There are more interfaces in Kernel. We need to query them from 
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

void InterfaceKState::SendNextRequest() {
    vr_interface_req req;
    InitDumpRequest(req);
    int idx = reinterpret_cast<long>(more_context_);
    req.set_vifr_marker(idx);
    EncodeAndSend(req);
}

void InterfaceKState::SendResponse() {

    KInterfaceResp *resp = static_cast<KInterfaceResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();

    response_object_ = new KInterfaceResp();
}

const string InterfaceKState::TypeToString(int if_type) const {
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

const string InterfaceKState::FlagsToString(int flags) const {
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

const string InterfaceKState::MacToString(const vector<signed char> &mac) 
    const {
    ostringstream strm;
    strm << hex << setfill('0') << setw(2) << (int)((uint8_t) mac.at(0)) << ":" 
         << setw(2) << (int)((uint8_t) mac.at(1)) << ":" << setw(2) 
         << (int)((uint8_t) mac.at(2)) << ":" << setw(2)
         << (int)((uint8_t) mac.at(3)) << ":" << setw(2) 
         << (int)((uint8_t) mac.at(4)) << ":" << setw(2)
         << (int)((uint8_t) mac.at(5));
    return strm.str();
}

