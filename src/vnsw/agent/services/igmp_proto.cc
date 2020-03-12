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
                    boost::bind(&IgmpProto::SendIgmpPacket, this, _1, _2, _3));
        gmp_proto_->Start();
    }

    vn_listener_id_ = agent_->vn_table()->Register(
            boost::bind(&IgmpProto::VnNotify, this, _1, _2));

    ClearStats();
}

void IgmpProto::Shutdown() {

    agent_->vn_table()->Unregister(vn_listener_id_);

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

    // Registering/Unregistering every IPAM gateway (or) dns_server
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
                delete igmp_intf;
                state->igmp_state_map_.erase(it->first);
            }

            igmp_address = ipam[i].default_gw;
        }

        it = state->igmp_state_map_.find(igmp_address);
        if (it == state->igmp_state_map_.end()) {
            igmp_intf = new IgmpInfo::IgmpSubnetState;
            state->igmp_state_map_.insert(
                            std::pair<IpAddress, IgmpInfo::IgmpSubnetState*>
                            (igmp_address, igmp_intf));
        }
    }
}

DBTableBase::ListenerId IgmpProto::vn_listener_id () {
    return vn_listener_id_;
}

// Send IGMP packets to the VMs part of the IPAM VN
bool IgmpProto::SendIgmpPacket(const VrfEntry *vrf, IpAddress gmp_addr,
                            GmpPacket *packet) {

    if (!vrf || !packet) {
        return false;
    }

    if (!gmp_addr.is_v4()) {
        return false;
    }

    VnEntry *vn = vrf->vn();
    if (!vn) {
        return false;
    }

    const VnIpam *ipam = vn->GetIpam(gmp_addr);
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
        if (!nh || nh->IsDeleted()) {
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
        if (!vm_itf->igmp_enabled()) {
            IncrSendStats(vm_itf, false);
            continue;
        }
        if (vrf->GetName() != vm_itf->vrf()->GetName()) {
            continue;
        }
        if (!ipam->IsSubnetMember(IpAddress(vm_itf->primary_ip_addr()))) {
            break;
        }

        igmp_handler.SendPacket(vm_itf, vrf, gmp_addr, packet);

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
    if (!ipam) {
        return false;
    }

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
