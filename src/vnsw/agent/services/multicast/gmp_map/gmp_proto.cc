/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "oper/route_common.h"
#include "oper/multicast.h"
#include "oper/nexthop.h"

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

GmpIntf::GmpIntf(const GmpProto *gmp_proto) : gmp_proto_(gmp_proto), vrf_name_(),
                                    ip_addr_() {

    gif_ = NULL;
}

bool GmpIntf::SetIpAddress(const IpAddress &addr) {

    if (ip_addr_ != addr) {
        ip_addr_ = addr;

        uint32_t intf_addr = htonl(ip_addr_.to_v4().to_ulong());
        gmp_addr_string gmp_addr;
        memcpy(&gmp_addr, &intf_addr, IPV4_ADDR_LEN);
        gmp_update_intf_state(gmp_proto_->gd_, gif_,
                                    (const gmp_addr_string *)&intf_addr);
        return true;
    }

    return false;
}

bool GmpIntf::SetVrf(const string &vrf_name) {
    vrf_name_ = vrf_name;
    return true;
}

GmpProto::GmpProto(GmpType::Type type, Agent *agent,
                            const std::string &task_name, int instance,
                            boost::asio::io_service &io) :
    type_(type), agent_(agent), name_(task_name), instance_(instance), io_(io) {

    task_map_ = NULL;
    gd_ = NULL;

    stats_.igmp_sgh_add_count_ = 0;
    stats_.igmp_sgh_del_count_ = 0;
}

GmpProto::~GmpProto() {
}

bool GmpProto::Start() {

    if (type_ != GmpType::IGMP) {
        return false;
    }

    task_map_ = TaskMapManager::CreateTaskMap(agent_, name_, instance_, io_);

    gd_ = gmp_init(MCAST_AF_IPV4, (task *)task_map_->task_, this);
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
    gd_ = NULL;
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
                        void *rcv_pkt, uint32_t packet_len,
                        IpAddress ip_saddr, IpAddress ip_daddr) {

    uint32_t addr;
    gmp_addr_string src_addr, dst_addr;

    addr = htonl(ip_saddr.to_v4().to_ulong());
    memcpy(&src_addr, &addr, IPV4_ADDR_LEN);
    addr = htonl(ip_daddr.to_v4().to_ulong());
    memcpy(&dst_addr, &addr, IPV4_ADDR_LEN);

    boolean ret = gmp_process_pkt(gd_, gmp_intf->GetGif(), rcv_pkt,
                        packet_len, &src_addr, &dst_addr);

    return ret ? true : false;
}

void GmpProto::GroupNotify(GmpIntf *gif, IpAddress source, IpAddress group,
                            bool add) {
}

void GmpProto::ResyncNotify(GmpIntf *gif, IpAddress source, IpAddress group) {
}

void GmpProto::UpdateHostInSourceGroup(GmpIntf *gif, bool join, IpAddress host,
                                    IpAddress source, IpAddress group) {

    uint32_t src_addr;
    src_addr = htonl(source.to_v4().to_ulong());

    MulticastHandler *mch = MulticastHandler::GetInstance();
    if (!mch) {
        return;
    }

    InetUnicastAgentRouteTable *table =
                agent_->vrf_table()->GetInet4UnicastRouteTable(gif->GetVrf());
    InetUnicastRouteEntry *uc_route = table->FindLPM(host);
    if (!uc_route) {
        return;
    }

    const NextHop *nh = uc_route->GetActiveNextHop();
    if (!nh) {
        return;
    }

    const InterfaceNH *inh = dynamic_cast<const InterfaceNH *>(nh);
    const Interface *intf = inh->GetInterface();
    if (!intf) {
        return;
    }

    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(intf);
    if (!vm_intf) {
        return;
    }

    if (src_addr) {
        if (join) {
            stats_.igmp_sgh_add_count_++;
            mch->AddVmInterfaceToSourceGroup(agent_->fabric_policy_vrf_name(),
                    agent_->fabric_vn_name(), vm_intf, source.to_v4(), group.to_v4());
        } else {
            stats_.igmp_sgh_del_count_++;
            mch->DeleteVmInterfaceFromSourceGroup(agent_->fabric_policy_vrf_name(),
                    vm_intf, source.to_v4(), group.to_v4());
        }
    }
}

GmpProto *GmpProtoManager::CreateGmpProto(GmpType::Type type, Agent *agent,
                            const std::string &task_name, int instance,
                            boost::asio::io_service &io) {

    GmpProto *proto_inst = new GmpProto(type, agent, task_name, instance, io);
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

void gmp_group_notify(mgm_global_data *gd, gmp_intf *intf,
                            int group_action, gmp_addr_string source,
                            gmp_addr_string group)
{
    if (!gd || !intf) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    GmpIntf *gif = (GmpIntf *)intf->vm_interface;

    if (!gmp_proto || !gif) {
        return;
    }

    uint32_t addr;
    memcpy(&addr, &source, IPV4_ADDR_LEN);
    IpAddress source_addr = IpAddress(Ip4Address(addr));
    memcpy(&addr, &group, IPV4_ADDR_LEN);
    IpAddress group_addr = IpAddress(Ip4Address(addr));

    bool add;
    if (group_action == MGM_GROUP_ADDED) {
        add = true;
    } else {
        add = false;
    }
    gmp_proto->GroupNotify(gif, source_addr, group_addr, add);
}

void gmp_cache_resync_notify(mgm_global_data *gd, gmp_intf *intf,
                            gmp_addr_string source, gmp_addr_string group)
{
    if (!gd || !intf) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    GmpIntf *gif = (GmpIntf *)intf->vm_interface;

    if (!gmp_proto || !gif) {
        return;
    }

    uint32_t addr;
    memcpy(&addr, &source, IPV4_ADDR_LEN);
    IpAddress source_addr = IpAddress(Ip4Address(addr));
    memcpy(&addr, &group, IPV4_ADDR_LEN);
    IpAddress group_addr = IpAddress(Ip4Address(addr));

    gmp_proto->ResyncNotify(gif, source_addr, group_addr);
}

void gmp_host_update(mgm_global_data *gd, gmp_intf *intf, boolean join,
                            gmp_addr_string host, gmp_addr_string source,
                            gmp_addr_string group)
{
    if (!gd || !intf) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    GmpIntf *gif = (GmpIntf *)intf->vm_interface;

    if (!gmp_proto || !gif) {
        return;
    }

    uint32_t addr;
    memcpy(&addr, &host, IPV4_ADDR_LEN);
    IpAddress host_addr = Ip4Address(ntohl(addr));
    memcpy(&addr, &source, IPV4_ADDR_LEN);
    IpAddress source_addr = Ip4Address(ntohl(addr));
    memcpy(&addr, &group, IPV4_ADDR_LEN);
    IpAddress group_addr = Ip4Address(ntohl(addr));

    gmp_proto->UpdateHostInSourceGroup(gif, join ? true : false, host_addr,
                                    source_addr, group_addr);
}

