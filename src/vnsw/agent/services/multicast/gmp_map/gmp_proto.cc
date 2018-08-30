/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include <init/agent_param.h>
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

GmpIntf::GmpIntf(const GmpProto *gmp_proto) : gmp_proto_(gmp_proto),
                                    vrf_name_(), ip_addr_() {

    gif_ = NULL;
    querying_ = false;
}

bool GmpIntf::set_ip_address(const IpAddress &addr) {

    if (ip_addr_ != addr) {
        ip_addr_ = addr;

        uint32_t intf_addr = htonl(ip_addr_.to_v4().to_ulong());
        gmp_addr_string gmp_addr;
        memcpy(&gmp_addr, &intf_addr, IPV4_ADDR_LEN);
        gmp_update_intf_state(gmp_proto_->gd_, gif_,
                                    addr != IpAddress() ?
                                    (const gmp_addr_string *)&intf_addr : NULL);
        return true;
    }

    return false;
}

bool GmpIntf::set_vrf_name(const string &vrf_name) {
    vrf_name_ = vrf_name;
    return true;
}

bool GmpIntf::set_gmp_querying(bool querying) {

    if (querying_ != querying) {
        querying_ = querying;
        boolean ret = gmp_update_intf_querying(gmp_proto_->gd_, gif_,
                                querying ? TRUE : FALSE);
        return ret ? true : false;
    }

    return true;
}

GmpProto::GmpProto(GmpType::Type type, Agent *agent,
                            const std::string &task_name, int instance,
                            boost::asio::io_service &io) :
    type_(type), agent_(agent), name_(task_name), instance_(instance), io_(io) {

    task_map_ = NULL;
    gmp_trigger_timer_ = NULL;
    gmp_notif_trigger_ = NULL;
    gd_ = NULL;
    cb_ = NULL;

    stats_.gmp_g_add_count_ = 0;
    stats_.gmp_g_del_count_ = 0;

    stats_.gmp_sg_add_count_ = 0;
    stats_.gmp_sg_del_count_ = 0;
}

GmpProto::~GmpProto() {
}

bool GmpProto::Start() {

    if (type_ != GmpType::IGMP) {
        return false;
    }

    task_map_ = TaskMapManager::CreateTaskMap(agent_, name_, instance_, io_);
    if (!task_map_) {
        return false;
    }

    gmp_notif_trigger_ = new TaskTrigger(
                            boost::bind(&GmpProto::GmpNotificationHandler,this),
                            TaskScheduler::GetInstance()->GetTaskId(name_),
                            instance_);

    gd_ = gmp_init(MCAST_AF_IPV4, (task *)task_map_->task_, this);
    if (!gd_) {
        if (task_map_) TaskMapManager::DeleteTaskMap(task_map_);
        task_map_ = NULL;

        if (gmp_notif_trigger_) {
            gmp_notif_trigger_->Reset();
            delete gmp_notif_trigger_;
        }
        gmp_notif_trigger_ = NULL;

        return false;
    }

    vn_listener_id_ = agent_->vn_table()->Register(
            boost::bind(&GmpProto::GmpVnNotify, this, _1, _2));
    itf_listener_id_ = agent_->interface_table()->Register(
            boost::bind(&GmpProto::GmpItfNotify, this, _1, _2));
    return true;
}

bool GmpProto::Stop() {

    if (!gd_) {
        return true;
    }

    gmp_deinit(MCAST_AF_IPV4);
    gd_ = NULL;

    if (gmp_trigger_timer_) {
        gmp_trigger_timer_->Cancel();
        TimerManager::DeleteTimer(gmp_trigger_timer_);
        gmp_trigger_timer_ = NULL;
    }

    gmp_notif_trigger_->Reset();
    delete gmp_notif_trigger_;
    gmp_notif_trigger_ = NULL;

    TaskMapManager::DeleteTaskMap(task_map_);
    task_map_ = NULL;

    return true;
}

void GmpProto::GmpIntfSGClear(VnGmpDBState *state,
                            VnGmpDBState::VnGmpIntfState *gmp_intf_state) {

    MulticastHandler *m_handler = agent_->oper_db()->multicast();

    GmpSourceGroup *gmp_sg = NULL;
    VnGmpDBState::VnGmpSGIntfListIter gif_sg_it =
                            gmp_intf_state->gmp_intf_sg_list_.begin();
    for (; gif_sg_it != gmp_intf_state->gmp_intf_sg_list_.end(); ++gif_sg_it) {
        gmp_sg = *gif_sg_it;
        gmp_sg->refcount_--;
        gmp_intf_state->gmp_intf_sg_list_.erase(gmp_sg);
        if (gmp_sg->refcount_ == 0) {
            state->gmp_sg_list_.erase(gmp_sg);
            m_handler->DeleteMulticastVrfSourceGroup(
                            gmp_intf_state->gmp_intf_->get_vrf_name(),
                            gmp_sg->source_.to_v4(), gmp_sg->group_.to_v4());
            delete gmp_sg;
        }
    }

    return;
}

void GmpProto::GmpVnNotify(DBTablePartBase *part, DBEntryBase *entry) {

    // Registering/Unregistering every IPAM gateway (or) dns_server
    // present in the VN with the IGMP module.
    // Changes to VN, or VN IPAM info, or gateway or dns server is
    // handled below.

    VnEntry *vn = static_cast<VnEntry *>(entry);

    VnGmpDBState *state = static_cast<VnGmpDBState *>
                        (entry->GetState(part->parent(), vn_listener_id_));
    VnGmpDBState::VnGmpIntfState *gmp_intf_state;

    if (vn->IsDeleted() || !vn->GetVrf()) {
        if (!state) {
            return;
        }
        VnGmpDBState::VnGmpIntfMap::iterator it = state->gmp_intf_map_.begin();
        for (;it != state->gmp_intf_map_.end(); ++it) {
            gmp_intf_state = it->second;
            GmpIntfSGClear(state, gmp_intf_state);
            // Cleanup the GMP database and timers
            gmp_intf_state->gmp_intf_->set_vrf_name(string());
            gmp_intf_state->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
            DeleteIntf(gmp_intf_state->gmp_intf_);
            delete gmp_intf_state;
            itf_attach_count_--;
        }
        state->gmp_intf_map_.clear();

        if (vn->IsDeleted()) {
            entry->ClearState(part->parent(), vn_listener_id_);
            delete state;
        }
        return;
    }

    if (!vn->GetVrf()) {
        return;
    }

    if ((vn->GetVrf()->GetName() == agent_->fabric_policy_vrf_name()) ||
        (vn->GetVrf()->GetName() == agent_->fabric_vrf_name())) {
        return;
    }

    if (state == NULL) {
        state = new VnGmpDBState();

        entry->SetState(part->parent(), vn_listener_id_, state);
    }

    VnGmpDBState::VnGmpIntfMap::iterator it = state->gmp_intf_map_.begin();
    while (it != state->gmp_intf_map_.end()) {
        const VnIpam *ipam = vn->GetIpam(it->first);
        if ((ipam != NULL) && ((ipam->default_gw == it->first) ||
                (ipam->dns_server == it->first))) {
            it++;
            continue;
        }
        gmp_intf_state = it->second;
        // Cleanup the GMP database and timers
        gmp_intf_state->gmp_intf_->set_vrf_name(string());
        gmp_intf_state->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
        DeleteIntf(gmp_intf_state->gmp_intf_);
        delete gmp_intf_state;
        itf_attach_count_--;
        state->gmp_intf_map_.erase(it++);
    }

    const std::vector<VnIpam> &ipam = vn->GetVnIpam();
    for (unsigned int i = 0; i < ipam.size(); ++i) {
        if (!ipam[i].IsV4()) {
            continue;
        }
        if ((ipam[i].default_gw == IpAddress(Ip4Address())) &&
            (ipam[i].dns_server == IpAddress(Ip4Address()))) {
            continue;
        }

        IpAddress gmp_address = IpAddress(Ip4Address());
        VnGmpDBState::VnGmpIntfMap::const_iterator it;

        if (ipam[i].dns_server != IpAddress(Ip4Address())) {
            it = state->gmp_intf_map_.find(ipam[i].dns_server);
            gmp_address = ipam[i].dns_server;
        }
        if (ipam[i].default_gw != IpAddress(Ip4Address())) {
            if ((it != state->gmp_intf_map_.end()) &&
                (ipam[i].default_gw != ipam[i].dns_server)) {
                gmp_intf_state = it->second;
                GmpIntfSGClear(state, gmp_intf_state);
                // Cleanup the GMP database and timers
                gmp_intf_state->gmp_intf_->set_vrf_name(string());
                gmp_intf_state->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
                DeleteIntf(gmp_intf_state->gmp_intf_);
                itf_attach_count_--;
                delete gmp_intf_state;
                state->gmp_intf_map_.erase(it->first);
            }

            gmp_address = ipam[i].default_gw;
        }

        it = state->gmp_intf_map_.find(gmp_address);
        if (it == state->gmp_intf_map_.end()) {
            gmp_intf_state = new VnGmpDBState::VnGmpIntfState();
            gmp_intf_state->gmp_intf_ = CreateIntf();
            itf_attach_count_++;
            state->gmp_intf_map_.insert(
                            std::pair<IpAddress,VnGmpDBState::VnGmpIntfState*>
                            (gmp_address, gmp_intf_state));
        } else {
            gmp_intf_state = it->second;
        }
        if (gmp_intf_state) {
            gmp_intf_state->gmp_intf_->set_ip_address(gmp_address);
            if (vn->GetVrf()) {
                gmp_intf_state->gmp_intf_->set_vrf_name(vn->GetVrf()->GetName());
            }
        }
    }
}

void GmpProto::GmpItfNotify(DBTablePartBase *part, DBEntryBase *entry) {

    Interface *itf = static_cast<Interface *>(entry);
    if (itf->type() != Interface::VM_INTERFACE) {
        return;
    }

    VmInterface *vm_itf = static_cast<VmInterface *>(itf);
    if (vm_itf->vmi_type() == VmInterface::VHOST) {
        return;
    }

    VmiGmpDBState *vmi_state = static_cast<VmiGmpDBState *>
                        (entry->GetState(part->parent(), itf_listener_id_));

    if (itf->IsDeleted() || !vm_itf->igmp_enabled()) {
        if (!vmi_state) {
            return;
        }
        if (agent_->oper_db()->multicast()) {
            agent_->oper_db()->multicast()->DeleteVmInterfaceFromVrfSourceGroup(
                                    vmi_state->vrf_name_, vm_itf);
        }
        if (itf->IsDeleted()) {
            entry->ClearState(part->parent(), itf_listener_id_);
            delete vmi_state;
        }
        return;
    }

    if (vmi_state == NULL) {
        vmi_state = new VmiGmpDBState();
        entry->SetState(part->parent(), itf_listener_id_, vmi_state);
    }

    if (vm_itf->vrf()) {
        vmi_state->vrf_name_ = vm_itf->vrf()->GetName();
    }

    return;
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

bool GmpProto::GmpProcessPkt(const VmInterface *vm_itf,
                        void *rcv_pkt, uint32_t packet_len,
                        IpAddress ip_saddr, IpAddress ip_daddr) {

    uint32_t addr;
    gmp_addr_string src_addr, dst_addr;

    const VnEntry *vn = vm_itf->vn();
    VnGmpDBState *state = NULL;
    state = static_cast<VnGmpDBState *>(vn->GetState(
                                vn->get_table_partition()->parent(),
                                vn_listener_id_));
    if (!state) {
        return false;
    }

    const VnIpam *ipam = vn->GetIpam(ip_saddr);
    VnGmpDBState::VnGmpIntfMap::const_iterator it =
                            state->gmp_intf_map_.find(ipam->default_gw);
    if (it == state->gmp_intf_map_.end()) {
        it = state->gmp_intf_map_.find(ipam->dns_server);
    }
    if (it == state->gmp_intf_map_.end()) {
        return false;
    }

    VnGmpDBState::VnGmpIntfState *gmp_intf_state = it->second;
    GmpIntf *gmp_intf = gmp_intf_state->gmp_intf_;

    addr = htonl(ip_saddr.to_v4().to_ulong());
    memcpy(&src_addr, &addr, IPV4_ADDR_LEN);
    addr = htonl(ip_daddr.to_v4().to_ulong());
    memcpy(&dst_addr, &addr, IPV4_ADDR_LEN);

    boolean ret = gmp_process_pkt(gd_, gmp_intf->GetGif(), rcv_pkt,
                        packet_len, &src_addr, &dst_addr);

    return ret ? true : false;
}

uint8_t *GmpProto::GmpBufferGet() {
    return new uint8_t[GMP_TX_BUFF_LEN];
}

void GmpProto::GmpBufferFree(uint8_t *pkt) {
    delete [] pkt;
}

bool GmpProto::GmpNotificationHandler() {

    boolean pending = FALSE;

    pending = gmp_notification_handler(gd_);
#if 0
    if (!pending) {
        std::cout << "Notifications cleared." << std::endl;
    } else {
        std::cout << "Notifications pending." << std::endl;
    }
#endif

    if ((pending == TRUE) && gmp_notif_trigger_) {
        gmp_trigger_timer_ = TimerManager::CreateTimer(io_, "GMP Trigger Timer",
                            TaskScheduler::GetInstance()->GetTaskId(name_),
                            instance_, true);

        gmp_trigger_timer_->Start(kGmpTriggerRestartTimer,
                            boost::bind(&GmpProto::GmpNotificationTimer, this));
    }

    return true;
}

bool GmpProto::GmpNotificationTimer() {

    if (gmp_notif_trigger_ && !gmp_notif_trigger_->IsSet()) {
        gmp_notif_trigger_->Set();
    }

    gmp_trigger_timer_ = NULL;

    return false;
}

void GmpProto::GmpNotificationReady() {

    if (!gmp_notif_trigger_ || gmp_notif_trigger_->IsSet()) {
        return;
    }

    gmp_notif_trigger_->Set();

#if 0
    if (gmp_trigger_timer_) {
        gmp_trigger_timer_->Cancel();
        TimerManager::DeleteTimer(gmp_trigger_timer_);
        gmp_trigger_timer_ = NULL;
    }
#endif

    return;
}

void GmpProto::GroupNotify(GmpIntf *gif, IpAddress source, IpAddress group,
                            int group_action) {

#if 0
    std::cout << "GmpProto::GroupNotify" << std::endl;

    struct timeval time;
    gettimeofday(&time, NULL);
    std::cout << time.tv_sec << " " << time.tv_usec << " ";
    std::cout << "GT:";

    if (group_action == MGM_GROUP_ADDED) {
        std::cout << "A:";
    } else if (group_action == MGM_GROUP_REMOVED) {
        std::cout << "D:";
    } else if (group_action == MGM_GROUP_SRC_REMOVED) {
        std::cout << "S:";
    }
    std::cout << "G/S/-:" << group << "/" << source;
    std::cout << std::endl;
#endif

    if (agent_->params()->mvpn_ipv4_enable()) {
        return;
    }

    // Support for EVPN <*,G> only.
    if (!gif) {
        return;
    }

    VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(gif->get_vrf_name());
    VnEntry *vn;
    if (vrf) {
        vn = vrf->vn();
    }
    if (!vrf || !vn) {
        return;
    }

    VnGmpDBState *state = NULL;
    state = static_cast<VnGmpDBState *>(vn->GetState(
                                vn->get_table_partition()->parent(),
                                vn_listener_id_));
    if (!state) {
        return;
    }

    VnGmpDBState::VnGmpIntfMap::iterator gif_it = state->gmp_intf_map_.begin();
    VnGmpDBState::VnGmpIntfState *gmp_intf_state = NULL;
    while (gif_it != state->gmp_intf_map_.end()) {
        gmp_intf_state = gif_it->second;
        if (gmp_intf_state->gmp_intf_ == gif) {
            break;
        }
        gif_it++;
    }

    if (gif_it == state->gmp_intf_map_.end()) {
        return;
    }

    GmpSourceGroup *gmp_sg = NULL;
    VnGmpDBState::VnGmpSGListIter sg_it = state->gmp_sg_list_.begin();
    for (;sg_it != state->gmp_sg_list_.end(); ++sg_it) {
        gmp_sg = *sg_it;
        if ((gmp_sg->source_ == source) || (gmp_sg->group_ == group)) {
            break;
        }
    }

    if (sg_it == state->gmp_sg_list_.end()) {
        if (group_action == MGM_GROUP_REMOVED ||
            group_action == MGM_GROUP_SRC_REMOVED) {

            return;
        }
    }

    bool created = false;
    if (!gmp_sg) {
        gmp_sg = new GmpSourceGroup();
        gmp_sg->source_ = source;
        gmp_sg->group_ = group;
        gmp_sg->flags_ = GmpSourceGroup::IGMP_VERSION_V1 |
                            GmpSourceGroup::IGMP_VERSION_V2;
        created = true;
        state->gmp_sg_list_.insert(gmp_sg);
    }

    VnGmpDBState::VnGmpSGIntfListIter gif_sg_it =
                            gmp_intf_state->gmp_intf_sg_list_.begin();

    MulticastHandler *m_handler = agent_->oper_db()->multicast();
    if (group_action == MGM_GROUP_ADDED) {
        gif_sg_it = gmp_intf_state->gmp_intf_sg_list_.find(gmp_sg);
        if (gif_sg_it == gmp_intf_state->gmp_intf_sg_list_.end()) {
            gmp_intf_state->gmp_intf_sg_list_.insert(gmp_sg);
            gmp_sg->refcount_++;
        }
        if (created) {
            m_handler->CreateMulticastVrfSourceGroup(vrf->GetName(),
                            vn->GetName(), source.to_v4(), group.to_v4());
            m_handler->SetEvpnMulticastSGFlags(vrf->GetName(),
                            source.to_v4(), group.to_v4(), gmp_sg->flags_);
        }
    }

    if (group_action == MGM_GROUP_REMOVED ||
        group_action == MGM_GROUP_SRC_REMOVED) {

        gif_sg_it = gmp_intf_state->gmp_intf_sg_list_.find(gmp_sg);
        if (gif_sg_it != gmp_intf_state->gmp_intf_sg_list_.end()) {
            gmp_sg->refcount_--;
            gmp_intf_state->gmp_intf_sg_list_.erase(gmp_sg);
            if (gmp_sg->refcount_ == 0) {
                state->gmp_sg_list_.erase(gmp_sg);
                m_handler->DeleteMulticastVrfSourceGroup(vrf->GetName(),
                            source.to_v4(), group.to_v4());
                delete gmp_sg;
            }
        }
    }

    return;
}

void GmpProto::ResyncNotify(GmpIntf *gif, IpAddress source, IpAddress group) {

#if 0
    struct timeval time;
    gettimeofday(&time, NULL);
    std::cout << time.tv_sec << " " << time.tv_usec << " ";
    std::cout << "RT:R:";
    std::cout << "G/S/-:" << group << "/" << source;
    std::cout << std::endl;
#endif
}

void GmpProto::UpdateHostInSourceGroup(GmpIntf *gif, bool join, IpAddress host,
                                    IpAddress source, IpAddress group) {

#if 0
    std::cout << "GmpProto::UpdateHostInSourceGroup" << std::endl;

    struct timeval time;
    gettimeofday(&time, NULL);
    std::cout << time.tv_sec << " " << time.tv_usec << " ";
    std::cout << "HT:";
    std::cout << (join ? "T" : "F");
    std::cout << ":G/S/H:" << group << "/" << source << "/" << host;
    std::cout << std::endl;
#endif

    if (!agent_->oper_db()->multicast()) {
        return;
    }

    if (!host.is_v4()) {
        return;
    }

    if (source.to_v4() == Ip4Address()) {
        join ? stats_.gmp_g_add_count_++ : stats_.gmp_g_del_count_++;
    } else {
        join ? stats_.gmp_sg_add_count_++ : stats_.gmp_sg_del_count_++;
    }

    InetUnicastAgentRouteTable *table =
            agent_->vrf_table()->GetInet4UnicastRouteTable(gif->get_vrf_name());
    InetUnicastRouteEntry *uc_route = table->FindLPM(host);
    if (!uc_route) {
        return;
    }

    const NextHop *nh = uc_route->GetActiveNextHop();
    if (!nh) {
        return;
    }

    const InterfaceNH *inh = dynamic_cast<const InterfaceNH *>(nh);
    if (!inh) {
        return;
    }

    const Interface *intf = inh->GetInterface();
    if (!intf) {
        return;
    }

    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(intf);
    if (!vm_intf) {
        return;
    }

    if (agent_->params()->mvpn_ipv4_enable()) {
        // Support for MVPN <S,G> only.
        TriggerMvpnNotification(vm_intf, join, source, group);
    } else {
        // Support for EVPN <*,G> only.
        TriggerEvpnNotification(vm_intf, join, source, group);
    }

    return;
}

void GmpProto::TriggerMvpnNotification(const VmInterface *vm_intf, bool join,
                                    IpAddress source, IpAddress group) {

    uint32_t src_addr;
    src_addr = htonl(source.to_v4().to_ulong());

    if (src_addr) {
        if (join) {
            agent_->oper_db()->multicast()->AddVmInterfaceToSourceGroup(
                                    agent_->fabric_policy_vrf_name(),
                                    agent_->fabric_vn_name(), vm_intf,
                                    source.to_v4(), group.to_v4());
        } else {
            agent_->oper_db()->multicast()->DeleteVmInterfaceFromSourceGroup(
                                    agent_->fabric_policy_vrf_name(), vm_intf,
                                    source.to_v4(), group.to_v4());
        }
    } else {
        if (!join) {
            agent_->oper_db()->multicast()->DeleteVmInterfaceFromSourceGroup(
                                    agent_->fabric_policy_vrf_name(), vm_intf,
                                    group.to_v4());
        }
    }

    return;
}

void GmpProto::TriggerEvpnNotification(const VmInterface *vm_intf, bool join,
                                    IpAddress source, IpAddress group) {

    if (join) {
        agent_->oper_db()->multicast()->AddVmInterfaceToVrfSourceGroup(
                vm_intf->vrf()->GetName(),
                agent_->fabric_vn_name(), vm_intf,
                source.to_v4(), group.to_v4());
    } else {
        agent_->oper_db()->multicast()->DeleteVmInterfaceFromVrfSourceGroup(
                vm_intf->vrf()->GetName(), vm_intf,
                source.to_v4(), group.to_v4());
    }

    return;
}

bool GmpProto::SendPacket(GmpIntf *gif, uint8_t *pkt, uint32_t pkt_len,
                            IpAddress dest) {

    GmpPacket packet(pkt, pkt_len, dest);

    if (!cb_) {
        return false;
    }

    const VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(gif->get_vrf_name());
    if (!vrf) {
        return false;
    }

    return cb_(vrf, gif->get_ip_address(), &packet);
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

void gmp_notification_ready(mgm_global_data *gd)
{
   if (!gd) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;

    gmp_proto->GmpNotificationReady();

    return;
}

void gmp_group_notify(mgm_global_data *gd, gmp_intf *intf,
                            int group_action, gmp_addr_string source,
                            gmp_addr_string group)
{
    if (!gd || !intf) {
        return;
    }

    if (gd->mgm_gd_af != MCAST_AF_IPV4) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    GmpIntf *gif = (GmpIntf *)intf->vm_interface;

    if (!gmp_proto || !gif) {
        return;
    }

    uint32_t addr;
    memcpy(&addr, &source, IPV4_ADDR_LEN);
    IpAddress source_addr = Ip4Address(ntohl(addr));
    memcpy(&addr, &group, IPV4_ADDR_LEN);
    IpAddress group_addr = Ip4Address(ntohl(addr));

    gmp_proto->GroupNotify(gif, source_addr, group_addr, group_action);
}

void gmp_cache_resync_notify(mgm_global_data *gd, gmp_intf *intf,
                            gmp_addr_string source, gmp_addr_string group)
{
    if (!gd || !intf) {
        return;
    }

    if (gd->mgm_gd_af != MCAST_AF_IPV4) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    GmpIntf *gif = (GmpIntf *)intf->vm_interface;

    if (!gmp_proto || !gif) {
        return;
    }

    uint32_t addr;
    memcpy(&addr, &source, IPV4_ADDR_LEN);
    IpAddress source_addr = Ip4Address(ntohl(addr));
    memcpy(&addr, &group, IPV4_ADDR_LEN);
    IpAddress group_addr = Ip4Address(ntohl(addr));

    gmp_proto->ResyncNotify(gif, source_addr, group_addr);
}

void gmp_host_update(mgm_global_data *gd, gmp_intf *intf, boolean join,
                            gmp_addr_string host, gmp_addr_string source,
                            gmp_addr_string group)
{
    if (!gd || !intf) {
        return;
    }

    if (gd->mgm_gd_af != MCAST_AF_IPV4) {
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

uint8_t *gmp_get_send_buffer(mgm_global_data *gd, gmp_intf *intf)
{
    if (!gd || !intf) {
        return NULL;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    return gmp_proto->GmpBufferGet();
}

void gmp_free_send_buffer(mgm_global_data *gd, gmp_intf *intf, uint8_t *buffer)
{
    if (!gd || !intf) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    gmp_proto->GmpBufferFree(buffer);

    return;
}

void gmp_send_one_packet(mgm_global_data *gd, gmp_intf *intf, uint8_t *pkt,
                            uint32_t pkt_len, gmp_addr_string dest)
{
    if (!gd || !intf) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    GmpIntf *gif = (GmpIntf *)intf->vm_interface;

    uint32_t addr;
    memcpy(&addr, &dest, IPV4_ADDR_LEN);
    IpAddress dst_addr = IpAddress(Ip4Address(ntohl(addr)));

    gmp_proto->SendPacket(gif, pkt, pkt_len, dst_addr);

    return;
}
