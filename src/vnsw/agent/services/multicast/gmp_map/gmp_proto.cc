/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include <init/agent_param.h>
#include "oper/route_common.h"
#include "oper/multicast.h"
#include "oper/multicast_policy.h"
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

extern SandeshTraceBufferPtr MulticastTraceBuf;

GmpIntf::GmpIntf(const GmpProto *gmp_proto) : gmp_proto_(gmp_proto),
                                    vrf_name_(), ip_addr_() {

    gif_ = NULL;
    querying_ = false;
}

// GmpIntf is local to Agent IGMP implementation and is not known
// by core IGMP module.
// IP address change to be reflected in GMP interface also.
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

// Update the new VRF name for GmpIntf.
bool GmpIntf::set_vrf_name(const string &vrf_name) {
    vrf_name_ = vrf_name;
    return true;
}

// Change of querying mode per-GmpIntf.
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

// Start the GmpProto that represents the agent's IGMP functionality.
bool GmpProto::Start() {

    if (type_ != GmpType::IGMP) {
        return false;
    }

    task_map_ = TaskMapManager::CreateTaskMap(agent_, name_, instance_, io_);
    if (!task_map_) {
        return false;
    }

    // TaskTrigger instance for handling GMP notifications like join, leave
    // per-<S,G> and per-host as required.
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

// Stop the GmpProto that represents the agent's IGMP functionality.
bool GmpProto::Stop() {

    if (!gd_) {
        return true;
    }

    agent_->interface_table()->Unregister(itf_listener_id_);
    agent_->vn_table()->Unregister(vn_listener_id_);

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

// Cleans up <S,G> per-GmpIntf. Also cleans up VMIs per-<S,G> from NH.
void GmpProto::GmpIntfSGClear(VnGmpDBState *state,
                            VnGmpDBState::VnGmpIntfState *gmp_intf_state) {

    MulticastHandler *m_handler = agent_->oper_db()->multicast();

    GmpSourceGroup *gmp_sg = NULL;
    std::set<GmpSourceGroup *> sg_to_delete;
    VnGmpDBState::VnGmpSGIntfListIter gif_sg_it =
                            gmp_intf_state->gmp_intf_sg_list_.begin();
    for (; gif_sg_it != gmp_intf_state->gmp_intf_sg_list_.end(); ++gif_sg_it) {

        gmp_sg = *gif_sg_it;

        MCTRACE(LogSG, "Delete Mcast VRF SG for VN ",
                            gmp_intf_state->gmp_intf_->get_vrf_name(),
                            gmp_sg->source_.to_v4().to_string(),
                            gmp_sg->group_.to_v4().to_string(), 0);

        gmp_sg->refcount_--;
        sg_to_delete.insert(gmp_sg);
        if (gmp_sg->refcount_ == 0) {
            state->gmp_sg_list_.erase(gmp_sg);
            // Delete VMIs from multicast list for the <S,G>
            m_handler->DeleteMulticastVrfSourceGroup(
                            gmp_intf_state->gmp_intf_->get_vrf_name(),
                            gmp_sg->source_.to_v4(), gmp_sg->group_.to_v4());
        }
    }

    for(std::set<GmpSourceGroup *>::iterator sg_it = sg_to_delete.begin();
        sg_it != sg_to_delete.end(); sg_it++) {
        gmp_sg = *sg_it;
        gmp_intf_state->gmp_intf_sg_list_.erase(gmp_sg);
        if (gmp_sg->refcount_ == 0) {
            delete gmp_sg;
        }
    }

    return;
}

// Handle VN change notification, specifically IPAM changes
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
            if (gmp_intf_state->gmp_intf_) {
                gmp_intf_state->gmp_intf_->set_vrf_name(string());
                gmp_intf_state->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
                DeleteIntf(gmp_intf_state->gmp_intf_);
                delete gmp_intf_state;
            }
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
        GmpIntfSGClear(state, gmp_intf_state);
        // Cleanup the GMP database and timers
        if (gmp_intf_state->gmp_intf_) {
            gmp_intf_state->gmp_intf_->set_vrf_name(string());
            gmp_intf_state->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
            DeleteIntf(gmp_intf_state->gmp_intf_);
        }
        delete gmp_intf_state;
        itf_attach_count_--;
        state->gmp_intf_map_.erase(it++);
    }

    const std::vector<VnIpam> &ipam = vn->GetVnIpam();
    bool create_intf = false;
    for (unsigned int i = 0; i < ipam.size(); ++i) {
        create_intf = false;
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
                if (gmp_intf_state->gmp_intf_) {
                    gmp_intf_state->gmp_intf_->set_vrf_name(string());
                    gmp_intf_state->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
                    DeleteIntf(gmp_intf_state->gmp_intf_);
                    gmp_intf_state->gmp_intf_ = NULL;
                    create_intf = true;
                }
                if (!create_intf) {
                    delete gmp_intf_state;
                    state->gmp_intf_map_.erase(it->first);
                }
                itf_attach_count_--;
            }

            gmp_address = ipam[i].default_gw;
        }

        it = state->gmp_intf_map_.find(gmp_address);
        if (it == state->gmp_intf_map_.end()) {
            if (!create_intf) {
                gmp_intf_state = new VnGmpDBState::VnGmpIntfState();
                state->gmp_intf_map_.insert(
                            std::pair<IpAddress,VnGmpDBState::VnGmpIntfState*>
                            (gmp_address, gmp_intf_state));
            } else {
                gmp_intf_state->gmp_intf_ = CreateIntf();
            }
            itf_attach_count_++;
        } else {
            gmp_intf_state = it->second;
        }
        if (gmp_intf_state && gmp_intf_state->gmp_intf_) {
            gmp_intf_state->gmp_intf_->set_ip_address(gmp_address);
            if (vn->GetVrf()) {
                gmp_intf_state->gmp_intf_->set_vrf_name(vn->GetVrf()->GetName());
            }
        }
    }
}

// Handle Interface notification, specifically VMI delete notification
// Clean up VMI from NH for the <S,G>s on VMI delete
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
            MCTRACE(IgmpIntf, "Itf Notify, no VMI state ",
                            "no-vrf", vm_itf->primary_ip_addr().to_string(),
                            vm_itf->igmp_enabled());
            return;
        }

        MCTRACE(IgmpIntf, "Itf Notify, VMI delete or IGMP disable ",
                            vmi_state->vrf_name_,
                            vm_itf->primary_ip_addr().to_string(),
                            vm_itf->igmp_enabled());
        if (agent_->oper_db()->multicast()) {
            agent_->oper_db()->multicast()->DeleteVmInterfaceFromVrfSourceGroup(
                                    vmi_state->vrf_name_, vm_itf);
        }

        if (vmi_state->igmp_enabled_ != vm_itf->igmp_enabled()) {
            TryCreateDeleteIntf(vmi_state, vm_itf);
        }

        if (itf->IsDeleted()) {
            vm_ip_to_vmi_.erase(vmi_state->vmi_v4_addr_);
            entry->ClearState(part->parent(), itf_listener_id_);
            delete vmi_state;
        }
        return;
    }

    MCTRACE(IgmpIntf, "Itf Notify, VMI create or IGMP enable",
                            vm_itf->vrf() ? vm_itf->vrf()->GetName() : "",
                            vm_itf->primary_ip_addr().to_string(),
                            vm_itf->igmp_enabled());
    if (vmi_state == NULL) {
        vmi_state = new VmiGmpDBState();
        entry->SetState(part->parent(), itf_listener_id_, vmi_state);
    }

    std::string vrf_name = vm_itf->vrf() ? vm_itf->vrf()->GetName() : "";
    if (vmi_state->vrf_name_ != vrf_name) {
        if (agent_->oper_db()->multicast()) {
            agent_->oper_db()->multicast()->
                            DeleteVmInterfaceFromVrfSourceGroup(
                                    vmi_state->vrf_name_, vm_itf);
        }

        vmi_state->vrf_name_ = vm_itf->vrf()->GetName();
    }

    if (vmi_state->vmi_v4_addr_ != vm_itf->primary_ip_addr()) {

        if (vm_ip_to_vmi_.find(vmi_state->vmi_v4_addr_) !=
                                    vm_ip_to_vmi_.end()) {
            if (agent_->oper_db()->multicast()) {
                agent_->oper_db()->multicast()->
                            DeleteVmInterfaceFromVrfSourceGroup(
                                    vmi_state->vrf_name_, vm_itf);
            }

            vm_ip_to_vmi_.erase(vmi_state->vmi_v4_addr_);
        }

        vmi_state->vmi_v4_addr_ = vm_itf->primary_ip_addr();
        vm_ip_to_vmi_.insert(std::pair<IpAddress,boost::uuids::uuid>
                                        (vmi_state->vmi_v4_addr_,
                                         vm_itf->GetUuid()));
    }

    if (vmi_state->igmp_enabled_ != vm_itf->igmp_enabled()) {
        TryCreateDeleteIntf(vmi_state, vm_itf);
    }

    return;
}

void GmpProto::TryCreateDeleteIntf(VmiGmpDBState *vmi_state, VmInterface *vm_itf) {

    const VnEntry *vn = vm_itf->vn();
    const VnIpam *ipam;

    if (!vn || vn->IsDeleted()) {
        return;
    }

    ipam = vn->GetIpam(vmi_state->vmi_v4_addr_);
    if (!ipam) {
        return;
    }

    VnGmpDBState *vn_state = static_cast<VnGmpDBState *>
                        (vn->GetState(vn->get_table(), vn_listener_id_));
    if (!vn_state) {
        return;
    }

    if ((ipam->default_gw == IpAddress(Ip4Address())) &&
        (ipam->dns_server == IpAddress(Ip4Address()))) {
        return;
    }

    VnGmpDBState::VnGmpIntfState *gmp_intf_state = NULL;
    IpAddress gmp_address = IpAddress(Ip4Address());
    VnGmpDBState::VnGmpIntfMap::const_iterator it;

    if (ipam->dns_server != IpAddress(Ip4Address())) {
        it = vn_state->gmp_intf_map_.find(ipam->dns_server);
        gmp_address = ipam->dns_server;
    }
    if (ipam->default_gw != IpAddress(Ip4Address())) {
        if ((it != vn_state->gmp_intf_map_.end()) &&
            (ipam->default_gw != ipam->dns_server)) {
            gmp_intf_state = it->second;
            GmpIntfSGClear(vn_state, gmp_intf_state);
            // Cleanup the GMP database and timers
            if (gmp_intf_state->gmp_intf_) {
                gmp_intf_state->gmp_intf_->set_vrf_name(string());
                gmp_intf_state->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
                DeleteIntf(gmp_intf_state->gmp_intf_);
            }
            itf_attach_count_--;
            delete gmp_intf_state;
            vn_state->gmp_intf_map_.erase(it->first);
        }

        gmp_address = ipam->default_gw;
    }

    it = vn_state->gmp_intf_map_.find(gmp_address);
    if (it == vn_state->gmp_intf_map_.end()) {
        gmp_intf_state = new VnGmpDBState::VnGmpIntfState();
        gmp_intf_state->gmp_intf_ = CreateIntf();
        vn_state->gmp_intf_map_.insert(
                        std::pair<IpAddress,VnGmpDBState::VnGmpIntfState*>
                        (gmp_address, gmp_intf_state));
    } else {
        gmp_intf_state = it->second;
    }

    if (!gmp_intf_state) {
        return;
    }

    if (vm_itf->igmp_enabled()) {
        gmp_intf_state->igmp_enabled_vmi_count_++;
    } else {
        gmp_intf_state->igmp_enabled_vmi_count_--;
    }

    if (gmp_intf_state->igmp_enabled_vmi_count_ && !gmp_intf_state->gmp_intf_) {
        gmp_intf_state->gmp_intf_ = CreateIntf();
    } else if (!gmp_intf_state->igmp_enabled_vmi_count_) {
        if (gmp_intf_state->gmp_intf_) {
            DeleteIntf(gmp_intf_state->gmp_intf_);
        }
        gmp_intf_state->gmp_intf_ = NULL;
    }

    if (gmp_intf_state->gmp_intf_) {
        gmp_intf_state->gmp_intf_->set_ip_address(gmp_address);
        if (vn->GetVrf()) {
            gmp_intf_state->gmp_intf_->set_vrf_name(vn->GetVrf()->GetName());
        }
    }

    vmi_state->igmp_enabled_ = vm_itf->igmp_enabled();
}

// Create a GmpIntf. Represents per-IPAM entry.
GmpIntf *GmpProto::CreateIntf() {

    GmpIntf *gmp_intf = new GmpIntf(this);

    gmp_intf->SetGif(gmp_attach_intf(gd_, gmp_intf));
    if (!gmp_intf->GetGif()) {
        delete gmp_intf;
        return NULL;
    }

    return gmp_intf;
}

// Delete a GmpIntf.
bool GmpProto::DeleteIntf(GmpIntf *gif) {

    gmp_detach_intf(gd_, gif->GetGif());
    delete gif;

    return true;
}

// Pass the IP header processed IGMP packet to the GMP
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

// TaskTrigger handler for IGMP notifications like join, leave.
bool GmpProto::GmpNotificationHandler() {

    boolean pending = FALSE;

    pending = gmp_notification_handler(gd_);

    if ((pending == TRUE) && gmp_notif_trigger_) {
        gmp_trigger_timer_ = TimerManager::CreateTimer(io_, "GMP Trigger Timer",
                            TaskScheduler::GetInstance()->GetTaskId(name_),
                            instance_, true);

        gmp_trigger_timer_->Start(kGmpTriggerRestartTimer,
                            boost::bind(&GmpProto::GmpNotificationTimer, this));
    }

    return true;
}

// Timer instance to handle pending notifications not handled in previous run
bool GmpProto::GmpNotificationTimer() {

    if (gmp_notif_trigger_ && !gmp_notif_trigger_->IsSet()) {
        gmp_notif_trigger_->Set();
    }

    gmp_trigger_timer_ = NULL;

    return false;
}

// Handler registered with GMP to take care of per-<S,G> and also
// per-host, per-<S,G> notifications. TaskTriggers instance created
// above is triggered to further handle the notifications
void GmpProto::GmpNotificationReady() {

    if (!gmp_notif_trigger_ || gmp_notif_trigger_->IsSet()) {
        return;
    }

    gmp_notif_trigger_->Set();

    return;
}

// <S,G>, including <*,G> notification handling
void GmpProto::GroupNotify(GmpIntf *gif, IpAddress source, IpAddress group,
                            int group_action) {

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
            MCTRACE(LogSG, "Create Mcast VRF SG ", vrf->GetName(),
                            source.to_string(), group.to_string(), 0);
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
                MCTRACE(LogSG, "Delete Mcast VRF SG ", vrf->GetName(),
                            source.to_string(), group.to_string(), 0);
                m_handler->DeleteMulticastVrfSourceGroup(vrf->GetName(),
                            source.to_v4(), group.to_v4());
                delete gmp_sg;
            }
        }
    }

    return;
}

void GmpProto::ResyncNotify(GmpIntf *gif, IpAddress source, IpAddress group) {

    return;
}

// Per-host, per-<S,G> handling. For now, only per-host, per-<*,G> handling
void GmpProto::UpdateHostInSourceGroup(GmpIntf *gif, bool join, IpAddress host,
                                    IpAddress source, IpAddress group) {

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

    if (vm_ip_to_vmi_.find(host) == vm_ip_to_vmi_.end()) {
        MCTRACE(Info, "igmp_trace: No host found", host.to_string());
        return;
    }

    boost::uuids::uuid vmi_uuid = vm_ip_to_vmi_[host];
    InterfaceConstRef intf_ref = agent_->interface_table()->FindVmi(vmi_uuid);
    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf_ref.get());
    if (!vm_intf) {
        MCTRACE(Info, "igmp_trace: No VM Interface for host", host.to_string());
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

// Handling per-host, per-<S,G> notification for MVPN case
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

// Handling per-host, per-<S,G> notification for EVPN(SMET) case
void GmpProto::TriggerEvpnNotification(const VmInterface *vm_intf, bool join,
                                    IpAddress source, IpAddress group) {

    if (join) {
        MCTRACE(LogSG, "Add VM Mcast VRF SG ",
                            vm_intf->primary_ip_addr().to_string(),
                            source.to_string(), group.to_string(), 0);
        agent_->oper_db()->multicast()->AddVmInterfaceToVrfSourceGroup(
                vm_intf->vrf()->GetName(),
                agent_->fabric_vn_name(), vm_intf,
                source.to_v4(), group.to_v4());
    } else {
        MCTRACE(LogSG, "Delete VM Mcast VRF SG ",
                            vm_intf->primary_ip_addr().to_string(),
                            source.to_string(), group.to_string(), 0);
        agent_->oper_db()->multicast()->DeleteVmInterfaceFromVrfSourceGroup(
                vm_intf->vrf()->GetName(), vm_intf,
                source.to_v4(), group.to_v4());
    }

    return;
}

// Accept/reject <S,G> based on the multicast policy configured
// by the user/application.
bool GmpProto::MulticastPolicyCheck(GmpIntf *gif, IpAddress source,
                            IpAddress group) {

    if (!gif) {
        return false;
    }

    VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(gif->get_vrf_name());
    VnEntry *vn = vrf ? vrf->vn() : NULL;
    if (!vn) {
        return false;
    }

    const UuidList &mp_list = vn->mp_list();
    if (!mp_list.size()) {
        return true;
    }

    UuidList::const_iterator it = mp_list.begin();
    MulticastPolicyTable *table = agent_->mp_table();
    while (it != mp_list.end()) {
        MulticastPolicyKey key(*it);
        MulticastPolicyEntry *entry = static_cast<MulticastPolicyEntry *>
                                            (table->FindActiveEntry(&key));
        SourceGroupInfo::Action action = entry->GetAction(source, group);
        if (action == SourceGroupInfo::ACTION_PASS) {
            return true;
        }
        it++;
    }

    return false;
}

// Send IGMP packet generated by the GMP to particular destination
bool GmpProto::SendPacket(GmpIntf *gif, uint8_t *pkt, uint32_t pkt_len,
                            IpAddress dest) {

    GmpPacket packet(pkt, pkt_len, dest);

    if (!cb_) {
        return false;
    }

    const VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(
                            gif->get_vrf_name());
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

// Callback registered with GMP to check <S,G> for multicast policy
boolean gmp_policy_check(mgm_global_data *gd, gmp_intf *intf,
                            gmp_addr_string source, gmp_addr_string group)
{
    if (!gd || !intf) {
        return FALSE;
    }

    if (gd->mgm_gd_af != MCAST_AF_IPV4) {
        return FALSE;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    GmpIntf *gif = (GmpIntf *)intf->vm_interface;

    if (!gmp_proto || !gif) {
        return FALSE;
    }

    uint32_t addr;
    memcpy(&addr, &source, IPV4_ADDR_LEN);
    IpAddress source_addr = Ip4Address(ntohl(addr));
    memcpy(&addr, &group, IPV4_ADDR_LEN);
    IpAddress group_addr = Ip4Address(ntohl(addr));

    bool permit = gmp_proto->MulticastPolicyCheck(gif, source_addr, group_addr);

    return (permit ? TRUE : FALSE);
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

// Function to handle per-<S,G> notifications
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

// C-based function to handle per-<S,G> resync notifications
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

// C-based function to handle per-host, per-<S,G> notifications
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

// Allocate buffer for use in sending IGMP packet
uint8_t *gmp_get_send_buffer(mgm_global_data *gd, gmp_intf *intf)
{
    if (!gd || !intf) {
        return NULL;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    return gmp_proto->GmpBufferGet();
}

// Free the allocated buffer used in sending IGMP packet
void gmp_free_send_buffer(mgm_global_data *gd, gmp_intf *intf, uint8_t *buffer)
{
    if (!gd || !intf) {
        return;
    }

    GmpProto *gmp_proto = (GmpProto *)gd->gmp_sm;
    gmp_proto->GmpBufferFree(buffer);

    return;
}

// Send IGMP packet out to the VMs.
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
