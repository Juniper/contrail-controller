/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "oper/vn.h"
#include "oper/route_common.h"
#include "oper/multicast.h"
#include "services/igmp_proto.h"

IgmpProto::IgmpProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::IGMP, io),
    task_name_("Agent::Services"), io_(io) {

    itf_attach_count_ = 0;
    IgmpProtoInit();
}

IgmpProto::~IgmpProto() {
}

void IgmpProto::IgmpProtoInit(void) {

    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent_->params()->services_queue_limit());
    work_queue_.SetBounded(true);

    gmp_proto_ = GmpProtoManager::CreateGmpProto(GmpType::IGMP, agent_,
                                    task_name_, PktHandler::IGMP, io_);
    if (gmp_proto_) {
        gmp_proto_->Register(
                        boost::bind(&IgmpProto::SendIgmpPacket, this, _1, _2));
        gmp_proto_->Start();
    }

    vn_listener_id_ = agent_->vn_table()->Register(
            boost::bind(&IgmpProto::VnNotify, this, _1, _2));
    itf_listener_id_ = agent_->interface_table()->Register(
            boost::bind(&IgmpProto::ItfNotify, this, _1, _2));

    ClearStats();
}

void IgmpProto::Shutdown() {

    agent_->vn_table()->Unregister(vn_listener_id_);
    agent_->interface_table()->Unregister(itf_listener_id_);

    if (gmp_proto_) {
        gmp_proto_->Stop();
        GmpProtoManager::DeleteGmpProto(gmp_proto_);
    }
}

ProtoHandler *IgmpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new IgmpHandler(agent(), info, io);
}

void IgmpProto::VnNotify(DBTablePartBase *part, DBEntryBase *entry) {

    // Registering/Unregisterint every IPAM gateway (or) dns_server
    // present in the VN with the IGMP module.
    // Changes to VN, or VN IPAM info, or gateway or dns server is
    // handled below.

    VnEntry *vn = static_cast<VnEntry *>(entry);
    IgmpInfo::IgmpSubnetState *igmp_intf = NULL;

    IgmpInfo::VnIgmpDBState *state =
                        static_cast<IgmpInfo::VnIgmpDBState *>
                        (entry->GetState(part->parent(), vn_listener_id_));

    if (vn->IsDeleted() || !vn->GetVrf()) {
        if (!state) {
            return;
        }
        IgmpInfo::VnIgmpDBState::IgmpSubnetStateMap::iterator it =
                            state->igmp_state_map_.begin();
        for (;it != state->igmp_state_map_.end(); ++it) {
            igmp_intf = it->second;
            // Cleanup the GMP database and timers
            igmp_intf->gmp_intf_->set_vrf_name(string());
            igmp_intf->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
            gmp_proto_->DeleteIntf(igmp_intf->gmp_intf_);
            itf_attach_count_--;
            delete igmp_intf;
        }
        state->igmp_state_map_.clear();

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
        state = new IgmpInfo::VnIgmpDBState();

        entry->SetState(part->parent(), vn_listener_id_, state);
    }

    IgmpInfo::VnIgmpDBState::IgmpSubnetStateMap::iterator it =
                            state->igmp_state_map_.begin();
    while (it != state->igmp_state_map_.end()) {
        const VnIpam *ipam = vn->GetIpam(it->first);
        if ((ipam != NULL) && ((ipam->default_gw == it->first) ||
                (ipam->dns_server == it->first))) {
            it++;
            continue;
        }
        igmp_intf = it->second;
        // Cleanup the GMP database and timers
        igmp_intf->gmp_intf_->set_vrf_name(string());
        igmp_intf->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
        gmp_proto_->DeleteIntf(igmp_intf->gmp_intf_);
        itf_attach_count_--;
        delete igmp_intf;
        state->igmp_state_map_.erase(it++);
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

        IpAddress igmp_address = IpAddress(Ip4Address());
        IgmpInfo::VnIgmpDBState::IgmpSubnetStateMap::const_iterator it;

        if (ipam[i].dns_server != IpAddress(Ip4Address())) {
            it = state->igmp_state_map_.find(ipam[i].dns_server);
            igmp_address = ipam[i].dns_server;
        }
        if (ipam[i].default_gw != IpAddress(Ip4Address())) {
            if (it != state->igmp_state_map_.end()) {
                igmp_intf = it->second;
                // Cleanup the GMP database and timers
                igmp_intf->gmp_intf_->set_vrf_name(string());
                igmp_intf->gmp_intf_->set_ip_address(IpAddress(Ip4Address()));
                gmp_proto_->DeleteIntf(igmp_intf->gmp_intf_);
                itf_attach_count_--;
                delete igmp_intf;
                state->igmp_state_map_.erase(it->first);
            }

            igmp_address = ipam[i].default_gw;
        }

        it = state->igmp_state_map_.find(igmp_address);
        if (it == state->igmp_state_map_.end()) {
            igmp_intf = new IgmpInfo::IgmpSubnetState;
            igmp_intf->gmp_intf_ = gmp_proto_->CreateIntf();
            itf_attach_count_++;
            state->igmp_state_map_.insert(
                            std::pair<IpAddress, IgmpInfo::IgmpSubnetState*>
                            (igmp_address, igmp_intf));
        } else {
            igmp_intf = it->second;
        }
        if (igmp_intf && igmp_intf->gmp_intf_) {
            igmp_intf->gmp_intf_->set_ip_address(igmp_address);
            if (vn->GetVrf()) {
                igmp_intf->gmp_intf_->set_vrf_name(vn->GetVrf()->GetName());
                igmp_intf->vrf_name_ = vn->GetVrf()->GetName();
            }
        }
    }
}

void IgmpProto::ItfNotify(DBTablePartBase *part, DBEntryBase *entry) {

    Interface *itf = static_cast<Interface *>(entry);
    if (itf->type() != Interface::VM_INTERFACE) {
        return;
    }

    VmInterface *vm_itf = static_cast<VmInterface *>(itf);
    if (vm_itf->vmi_type() == VmInterface::VHOST) {
        return;
    }

    IgmpInfo::VmiIgmpDBState *vmi_state =
                static_cast<IgmpInfo::VmiIgmpDBState *>
                        (entry->GetState(part->parent(), itf_listener_id_));

    if (itf->IsDeleted() || !vm_itf->igmp_enabled()) {
        if (!vmi_state) {
            return;
        }
        if (agent_->oper_db()->multicast()) {
            agent_->oper_db()->multicast()->DeleteVmInterfaceFromSourceGroup(
                                    agent_->fabric_policy_vrf_name(),
                                    vmi_state->vrf_name_, vm_itf);
        }
        if (itf->IsDeleted()) {
            entry->ClearState(part->parent(), itf_listener_id_);
            delete vmi_state;
        }
        return;
    }

    if (vmi_state == NULL) {
        vmi_state = new IgmpInfo::VmiIgmpDBState();
        entry->SetState(part->parent(), itf_listener_id_, vmi_state);
    }

    if (vm_itf->vrf()) {
        vmi_state->vrf_name_ = vm_itf->vrf()->GetName();
    }

    return;
}

DBTableBase::ListenerId IgmpProto::vn_listener_id () {
    return vn_listener_id_;
}

DBTableBase::ListenerId IgmpProto::itf_listener_id () {
    return itf_listener_id_;
}

bool IgmpProto::SendIgmpPacket(GmpIntf *gmp_intf, GmpPacket *packet) {

    if (!gmp_intf || !packet) {
        return false;
    }

    VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(
                                    gmp_intf->get_vrf_name());
    VnEntry *vn;
    if (vrf) {
        vn = vrf->vn();
    }

    if (!vrf || !vn) {
        return false;
    }

    const VnIpam *ipam = vn->GetIpam(gmp_intf->get_ip_address());
    if (!ipam) {
        return false;
    }

    Ip4Address subnet = ipam->GetSubnetAddress();
    InetUnicastAgentRouteTable *inet_table =
                                    vrf->GetInet4UnicastRouteTable();
    const InetUnicastRouteEntry *rt = inet_table->FindRoute(subnet);
    if (!rt) {
        return false;
    }

    boost::shared_ptr<PktInfo> pkt(new PktInfo(agent_, 1024, PktHandler::IGMP,
                                    0));
    IgmpHandler igmp_handler(agent_, pkt,
                                    *(agent_->event_manager()->io_service()));

    do {
        const NextHop *nh = rt->GetActiveNextHop();
        if (!nh) {
            continue;
        }
        const InterfaceNH *inh = dynamic_cast<const InterfaceNH *>(nh);
        if (!inh) {
            continue;
        }
        const Interface *itf = inh->GetInterface();
        if (!itf) {
            continue;
        }
        const VmInterface *vm_itf = dynamic_cast<const VmInterface *>(itf);
        if (!vm_itf || vm_itf->IsDeleted()) {
            continue;
        }
        if (vm_itf->vmi_type() == VmInterface::VHOST) {
            continue;
        }
        if (vrf->GetName() != vm_itf->vrf()->GetName()) {
            continue;
        }
        if (!ipam->IsSubnetMember(IpAddress(vm_itf->primary_ip_addr()))) {
            break;
        }
        if (!vm_itf->igmp_enabled()) {
            IncrSendStats(vm_itf, false);
            continue;
        }

        igmp_handler.SendPacket(vm_itf, vrf, gmp_intf, packet);

    } while ((rt = inet_table->GetNext(rt)) != NULL);

    return true;
}

void IgmpProto::IncrSendStats(const VmInterface *vm_itf, bool tx_done) {

    const VnEntry *vn = vm_itf->vn();
    IgmpInfo::VnIgmpDBState *state = NULL;
    state = static_cast<IgmpInfo::VnIgmpDBState *>(vn->GetState(
                                vn->get_table_partition()->parent(),
                                vn_listener_id()));
    const VnIpam *ipam = vn->GetIpam(vm_itf->primary_ip_addr());
    IgmpInfo::VnIgmpDBState::IgmpSubnetStateMap::const_iterator it =
                            state->igmp_state_map_.find(ipam->default_gw);
    if (it == state->igmp_state_map_.end()) {
        return;
    }

    IgmpInfo::IgmpSubnetState *igmp_intf = NULL;
    igmp_intf = it->second;

    if (tx_done) {
        igmp_intf->IncrTxPkt();
    } else {
        igmp_intf->IncrTxDropPkt();
    }

    return;
}

const bool IgmpProto::GetItfStats(const VnEntry *vn, IpAddress gateway,
                            IgmpInfo::IgmpItfStats &stats) {

    const VnIpam *ipam = vn->GetIpam(gateway);
    IgmpInfo::VnIgmpDBState *state = NULL;
    state = static_cast<IgmpInfo::VnIgmpDBState *>(vn->GetState(
                            vn->get_table_partition()->parent(), vn_listener_id_));

    IgmpInfo::VnIgmpDBState::IgmpSubnetStateMap::const_iterator it =
                            state->igmp_state_map_.find(ipam->default_gw);
    if (it == state->igmp_state_map_.end()) {
        return false;
    }

    IgmpInfo::IgmpSubnetState *igmp_intf = it->second;

    stats = igmp_intf->GetItfStats();

    return true;
}

void IgmpProto::ClearItfStats(const VnEntry *vn, IpAddress gateway) {

    const VnIpam *ipam = vn->GetIpam(gateway);
    if (!vn) {
        return;
    }

    IgmpInfo::VnIgmpDBState *state = NULL;
    state = static_cast<IgmpInfo::VnIgmpDBState *>(vn->GetState(
                        vn->get_table_partition()->parent(), vn_listener_id_));
    if (!state) {
        return;
    }

    IgmpInfo::VnIgmpDBState::IgmpSubnetStateMap::const_iterator it =
                            state->igmp_state_map_.find(ipam->default_gw);
    if (it == state->igmp_state_map_.end()) {
        return;
    }

    IgmpInfo::IgmpSubnetState *igmp_intf = it->second;
    igmp_intf->ClearItfStats();

    return;
}
