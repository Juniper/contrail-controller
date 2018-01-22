/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "pkt/proto.h"


#ifdef __cplusplus
extern "C" {
#endif
#include "mcast_common.h"
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_intf.h"
#include "gmp_map.h"
#ifdef __cplusplus
}
#endif

#include "task_map.h"
#include "gmp_proto.h"

GmpIntf::GmpIntf(const GmpProto *gmp_proto) : gmp_proto_(gmp_proto) {

}

bool GmpIntf::SetIpAddress(const IpAddress &addr) {

    if (ip_addr_ != addr) {
        ip_addr_ = addr;

        uint32_t intf_addr = htonl(ip_addr_.to_v4().to_ulong());
        gmp_update_intf_state(gmp_proto_->gd_, gif_,
                                    (const u_int8_t *)&intf_addr);
        return true;
    }

    return false;
}

GmpProto::GmpProto(Agent *agent, const std::string &name,
                            PktHandler::PktModuleName module,
                            boost::asio::io_service &io) :
    agent_(agent), name_(name), module_(module), io_(io) {

    task_map_ = NULL;
    gd_ = NULL;
}

GmpProto::~GmpProto() {
}

bool GmpProto::Start() {

    task_map_ = TaskMapManager::CreateTaskMap(agent_, name_, module_, io_);

    if (module_ == PktHandler::IGMP) {
        gd_ = gmp_init(MCAST_AF_IPV4, (task *)task_map_->task_, this);
    }

    if (!gd_) {
        TaskMapManager::DeleteTaskMap(task_map_);
        task_map_ = NULL;

        return false;
    }
    return true;
}

bool GmpProto::Stop() {

    if (!gd_) {
        return true;
    }

    gmp_deinit(MCAST_AF_IPV4);
    gd_ = 0;
    TaskMapManager::DeleteTaskMap(task_map_);
    task_map_ = NULL;

    return true;
}

GmpIntf *GmpProto::CreateIntf() {

    GmpIntf *gmp_intf = new GmpIntf(this);

    gmp_intf->SetGif(gmp_attach_intf(gd_, gmp_intf));
    if (!gmp_intf->GetGif()) {
        delete gmp_intf;
        return NULL;
    }

    return gmp_intf;
}

bool GmpProto::DeleteIntf(GmpIntf *gif) {

    gmp_detach_intf(gd_, gif->GetGif());
    delete gif;

    return true;
}

bool GmpProto::GmpProcessPkt(GmpIntf *gmp_intf,
                        void *rcv_pkt, u_int32_t packet_len,
                        IpAddress ip_saddr, IpAddress ip_daddr) {

    uint32_t addr;
    gmp_addr_string src_addr, dst_addr;

    addr = ip_saddr.to_v4().to_ulong();
    bcopy(&addr, &src_addr, IPV4_ADDR_LEN);
    addr = ip_daddr.to_v4().to_ulong();
    bcopy(&addr, &dst_addr, IPV4_ADDR_LEN);

    boolean ret = gmp_process_pkt(gd_, gmp_intf->GetGif(), rcv_pkt, packet_len,
                        &src_addr, &dst_addr);

    return ret ? true : false;
}

GmpProto *GmpProtoManager::CreateGmpProto(Agent *agent, const std::string &name,
            PktHandler::PktModuleName module, boost::asio::io_service &io) {

    GmpProto *proto_inst = new GmpProto(agent, name, module, io);
    if (!proto_inst) {
        return NULL;
    }

    return proto_inst;
}

bool GmpProtoManager::DeleteGmpProto(GmpProto *proto_inst) {
    if (!proto_inst) {
        return false;
    }

    delete proto_inst;

    return true;
}

