/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
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
#include "gmp_sm.h"

GmpIntf::GmpIntf(const GmpSm *gmp_sm) : gmp_sm_(gmp_sm) {

}

bool GmpIntf::SetIpAddress(const IpAddress &addr) {

    if (ip_addr_ != addr) {
        ip_addr_ = addr;

        uint32_t intf_addr = htonl(ip_addr_.to_v4().to_ulong());
        gmp_update_intf_state(gmp_sm_->gd_, gif_, (const u_int8_t *)&intf_addr);
        return true;
    }

    return false;
}

GmpSm::GmpSm(Agent *agent, const std::string &name,
                            PktHandler::PktModuleName module,
                            boost::asio::io_service &io) :
    agent_(agent), name_(name), module_(module), io_(io) {

    task_map_ = NULL;
    gd_ = NULL;

}

GmpSm::~GmpSm() {
}

bool GmpSm::Start() {

    task_map_ = TaskMapManager::CreateTaskMap(agent_, name_, io_);

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

bool GmpSm::Stop() {

    if (!gd_) {
        return true;
    }

    gmp_deinit(gd_);
    gd_ = 0;
    TaskMapManager::DeleteTaskMap(task_map_);
    task_map_ = NULL;

    return true;
}

GmpIntf *GmpSm::CreateIntf() {

    GmpIntf *gmp_intf = new GmpIntf(this);

    gmp_intf->SetGif(gmp_attach_intf(gd_, gmp_intf));
    if (!gmp_intf->GetGif()) {
        delete gmp_intf;
        return NULL;
    }

    return gmp_intf;
}

bool GmpSm::DeleteIntf(GmpIntf *gif) {

    gmp_detach_intf(gd_, gif->GetGif());
    delete gif;

    return true;
}

bool GmpSm::GmpProcessPkt(GmpIntf *gmp_intf,
                        void *rcv_pkt, u_int32_t packet_len,
                        const u_int8_t *src_addr, const u_int8_t *dst_addr) {

    boolean ret = gmp_process_pkt(gd_, gmp_intf->GetGif(), rcv_pkt, packet_len,
                        src_addr, dst_addr);

    return ret ? true : false;
}

GmpSm *GmpSmManager::CreateGmpSm(Agent *agent, const std::string &name,
            PktHandler::PktModuleName module, boost::asio::io_service &io) {

    GmpSm *sm_inst = new GmpSm(agent, name, module, io);
    if (!sm_inst) {
        return NULL;
    }

    return sm_inst;
}

bool GmpSmManager::DeleteGmpSm(GmpSm *sm_inst) {
    if (!sm_inst) {
        return false;
    }

    delete sm_inst;

    return true;
}

