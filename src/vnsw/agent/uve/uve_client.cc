/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <map>
#include <set>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <cmn/agent_cmn.h>
#include <oper/interface.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <uve/uve_client.h>
#include <uve/inter_vn_stats.h>
#include <uve/flow_uve.h>
#include <base/cpuinfo.h>
#include <base/timer.h>
#include <controller/controller_peer.h>
#include <base/misc_utils.h>
#include "cfg/init_config.h"
#include <uve/uve_init.h>

using namespace std;
using namespace boost::asio;

UveClient *UveClient::singleton_;

void UveClient::AddIntfToVm(const VmEntry *vm, const Interface *intf) {
    //Check if the element is already present
    for (VmIntfMap::iterator it = vm_intf_map_.find(vm);
            it != vm_intf_map_.end(); it++) {
        if (vm == it->first && intf == it->second.intf) {
            //Duplicate add
            return;
        }
    }
    vm_intf_map_.insert(make_pair(vm, UveIntfEntry(intf)));
}

void UveClient::DelIntfFromVm(const Interface *intf) {
    for (VmIntfMap::iterator it = vm_intf_map_.begin();
            it != vm_intf_map_.end(); it++) {
        if (intf == it->second.intf) {
            vm_intf_map_.erase(it);
            return;
        }
    }
}

void UveClient::AddIntfToVn(const VnEntry *vn, const Interface *intf) {
    //Check if interface is already present
    for (VnIntfMap::iterator it = vn_intf_map_.find(vn);
            it != vn_intf_map_.end(); it++) {
        if (vn == it->first && intf == it->second) {
            //Duplicate add
            return;
        }
    }
    vn_intf_map_.insert(make_pair(vn, intf)); 
}

void UveClient::DelIntfFromVn(const Interface *intf) {
    for (VnIntfMap::iterator it = vn_intf_map_.begin();
            it != vn_intf_map_.end(); it++) {
        if (intf == it->second) {
            vn_intf_map_.erase(it);
            return;
        }
    }
}

bool UveClient::GetVmIntfGateway(const VmPortInterface *vm_intf, string &gw) {
    const VnEntry *vn = vm_intf->GetVnEntry();
    if (vn == NULL) {
        return false;
    }
    const vector<VnIpam> &list = vn->GetVnIpam();
    Ip4Address vm_addr = vm_intf->GetIpAddr();
    unsigned int i;
    for (i = 0; i < list.size(); i++) {
        if (list[i].IsSubnetMember(vm_addr))
            break;
    }
    if (i == list.size()) {
        return false;
    }
    gw = list[i].default_gw.to_string();
    return true;
}

bool UveClient::FrameIntfMsg(const VmPortInterface *vm_intf, 
                             VmInterfaceAgent *s_intf) {
    if (vm_intf->GetCfgName() == Agent::GetInstance()->NullString()) {
        return false;
    }
    s_intf->set_name(vm_intf->GetCfgName());
    s_intf->set_vm_name(vm_intf->GetVmName());
    if (vm_intf->GetVnEntry() != NULL) {
        s_intf->set_virtual_network(vm_intf->GetVnEntry()->GetName());
    } else {
        s_intf->set_virtual_network("");
    }
    s_intf->set_ip_address(vm_intf->GetIpAddr().to_string());
    s_intf->set_mac_address(vm_intf->GetVmMacAddr());

    vector<VmFloatingIPAgent> uve_fip_list;
    if (vm_intf->HasFloatingIp()) {
        const VmPortInterface::FloatingIpList fip_list = 
            vm_intf->GetFloatingIpList();
        VmPortInterface::FloatingIpList::const_iterator it = 
            fip_list.begin();
        while(it != fip_list.end()) {
            const VmPortInterface::FloatingIp &ip = *it;
            VmFloatingIPAgent uve_fip;
            uve_fip.set_ip_address(ip.floating_ip_.to_string());
            uve_fip.set_virtual_network(ip.vn_.get()->GetName());
            uve_fip_list.push_back(uve_fip);
            it++;
        }
    }
    s_intf->set_floating_ips(uve_fip_list);
    s_intf->set_label(vm_intf->GetLabel());
    s_intf->set_active(vm_intf->GetActiveState());
    string gw;
    if (GetVmIntfGateway(vm_intf, gw)) {
        s_intf->set_gateway(gw);
    }

    return true;
}

bool UveClient::FrameIntfStatsMsg(const VmPortInterface *vm_intf, 
                                  VmInterfaceAgentStats *s_intf) {
    uint64_t in_band, out_band;
    if (vm_intf->GetCfgName() == Agent::GetInstance()->NullString()) {
        return false;
    }
    s_intf->set_name(vm_intf->GetCfgName());

    const Interface *intf = static_cast<const Interface *>(vm_intf);
    AgentStatsCollector::IfStats *s = 
        AgentUve::GetInstance()->GetStatsCollector()->GetIfStats(intf);
    if (s == NULL) {
        return false;
    }

    s_intf->set_in_pkts(s->in_pkts);
    s_intf->set_in_bytes(s->in_bytes);
    s_intf->set_out_pkts(s->out_pkts);
    s_intf->set_out_bytes(s->out_bytes);
    in_band = GetVmPortBandwidth(s, true);
    out_band = GetVmPortBandwidth(s, false);
    s_intf->set_in_bandwidth_usage(in_band);
    s_intf->set_out_bandwidth_usage(out_band);
    s->stats_time = UTCTimestampUsec();

    return true;
}

void UveClient::VnVmListSend(const string &vn) {
    VnVmSet::iterator it = vn_vm_set_.lower_bound(UveVnVmEntry(vn, ""));
    UveVirtualNetworkAgent s_vn;
    int vm_count = 0;

    s_vn.set_name(vn);
    vector<string> vm_list;

    while (it != vn_vm_set_.end()) {
        const UveVnVmEntry &entry = *it;
        if (entry.vn_name_ != vn) {
            break;
        }
        vm_list.push_back(entry.vm_name_);
        it++;
        vm_count++;
    }
    s_vn.set_virtualmachine_list(vm_list);
    vn_vmlist_updates_++;
    UveVirtualNetworkAgentTrace::Send(s_vn);
}

void UveClient::AddVmToVn(const VmPortInterface *intf, const string vm_name, const string vn_name) {

    if (vm_name == Agent::GetInstance()->NullString() || vn_name == Agent::GetInstance()->NullString()) {
        return;
    }

    VnVmSet::iterator it = vn_vm_set_.find(UveVnVmEntry(vn_name, vm_name));
    if (it == vn_vm_set_.end()) {
        vn_vm_set_.insert(UveVnVmEntry(vn_name, vm_name));
        VnVmListSend(vn_name);
    }
}

void UveClient::DelVmFromVn(const VmPortInterface *intf, const string vm_name, const string vn_name) {

    if (vm_name == Agent::GetInstance()->NullString() || vn_name == Agent::GetInstance()->NullString()) {
        return;
    }

    VnVmSet::iterator it = vn_vm_set_.find(UveVnVmEntry(vn_name, vm_name));
    if (it == vn_vm_set_.end()) {
        return;
    }

    vn_vm_set_.erase(it);
    VnVmListSend(vn_name);
}

void UveClient::AddIntfToIfStatsTree(const Interface *intf) {
    AgentStatsCollector *collector = AgentUve::GetInstance()->GetStatsCollector();
    collector->AddIfStatsEntry(intf);
}

void UveClient::DelIntfFromIfStatsTree(const Interface *intf) {
    AgentStatsCollector *collector = AgentUve::GetInstance()->GetStatsCollector();
    collector->DelIfStatsEntry(intf);
}

void UveClient::SendVmAndVnMsg(const VmPortInterface* vm_port) {
    const VmEntry *vm = vm_port->GetVmEntry();
    const VnEntry *vn = vm_port->GetVnEntry();
    if (vm) {
        SendVmMsg(vm, false);
    }

    if (vn) {
        SendVnMsg(vn, false);
    }
}

void UveClient::VrfNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VrfEntry *vrf = static_cast<const VrfEntry *>(e);
    UveState *state = static_cast<UveState *>
                      (e->GetState(partition->parent(), vrf_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            AgentStatsCollector *collector = AgentUve::GetInstance()->GetStatsCollector();
            collector->DelVrfStatsEntry(vrf);
            delete state;
            e->ClearState(partition->parent(), vrf_listener_id_);
        }
    } else {
        if (!state) {
            state = new UveState();
            e->SetState(partition->parent(), vrf_listener_id_, state);
        }
        AgentStatsCollector *collector = AgentUve::GetInstance()->GetStatsCollector();
        collector->AddUpdateVrfStatsEntry(vrf);
    }
}

void UveClient::IntfNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const Interface *intf = static_cast<const Interface *>(e);
    bool set_state = false, reset_state = false;

    UveState *state = static_cast<UveState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    bool vmport_active = false;
    const VmPortInterface *vm_port = NULL;
    switch(intf->GetType()) {
    case Interface::VMPORT: {
        vm_port = static_cast<const VmPortInterface*>(intf);
        if (vm_port->GetCfgName() == Agent::GetInstance()->NullString()) {
            return;
        }
        if (!e->IsDeleted() && !state) {
            set_state = true;
            vmport_active = vm_port->GetActiveState();
            VrouterObjectIntfNotify(intf, true);
        } else if (e->IsDeleted()) {
            if (state) {
                reset_state = true;
                VrouterObjectIntfNotify(intf, true);
            }
        } else {
            if (state && vm_port->GetActiveState() != state->vmport_active_) { 
                VrouterObjectIntfNotify(intf, false);
                state->vmport_active_ = vm_port->GetActiveState();
            }
        }

        // Send update for interface port
        // VM it belong to, and also the VN
        if (e->IsDeleted() || (intf->GetActiveState() == false)) {
            if (state) {
                DelIntfFromVm(intf);
                DelIntfFromVn(intf);
                DelIntfFromIfStatsTree(intf);
                DelVmFromVn(vm_port, state->vm_name_, state->vn_name_);
            }
            SendVmAndVnMsg(vm_port);
        } else {
            const VmEntry *vm = vm_port->GetVmEntry();
            const VnEntry *vn = vm_port->GetVnEntry();

            AddIntfToVm(vm, intf);
            AddIntfToVn(vn, intf);
            AddIntfToIfStatsTree(intf);
            AddVmToVn(vm_port, vm->GetCfgName(), vn->GetName());
            SendVmAndVnMsg(vm_port);
            if (state) {
                state->vm_name_ = vm->GetCfgName();
                state->vn_name_ = vn->GetName();
            }
        }
        break;
    }
    case Interface::ETH:
        if (e->IsDeleted()) {
            if (state) {
                reset_state = true;
                DelIntfFromIfStatsTree(intf);
                phy_intf_set_.erase(intf);
            }
        } else {
            if (!state) {
                set_state = true;
                AddIntfToIfStatsTree(intf);
                phy_intf_set_.insert(intf);
            }
        }
        break;
    default:
        if (e->IsDeleted()) {
            DelIntfFromIfStatsTree(intf);
        } else {
            AddIntfToIfStatsTree(intf);
        }
    }
    if (set_state) {
        state = new UveState();
        state->vmport_active_ = vmport_active;
        if (vm_port) {
            const VmEntry *vm = vm_port->GetVmEntry();
            const VnEntry *vn = vm_port->GetVnEntry();
            state->vm_name_ = vm? vm->GetCfgName() : Agent::GetInstance()->NullString();
            state->vn_name_ = vn? vn->GetName() : Agent::GetInstance()->NullString();
        }
        e->SetState(partition->parent(), intf_listener_id_, state);
    } else if (reset_state) {
        delete state;
        e->ClearState(partition->parent(), intf_listener_id_);
    }
    return;
}

bool UveClient::FrameVmStatsMsg(const VmEntry *vm, L4PortBitmap *vm_port_bitmap,
                                UveVmEntry *uve) {
    bool changed = false;
    uve->uve_info.set_name(vm->GetCfgName());
    vector<VmInterfaceAgentStats> s_intf_list;
    vector<VmInterfaceAgentBMap> if_bmap_list;

    for (VmIntfMap::iterator it = vm_intf_map_.find(vm); 
         it != vm_intf_map_.end(); it++) {

        if (it->first != vm) {
            break;
        }

        VmInterfaceAgentStats s_intf;
        const Interface *intf = it->second.intf;
        const VmPortInterface *vm_port =
            static_cast<const VmPortInterface *>(intf);
        if (FrameIntfStatsMsg(vm_port, &s_intf)) {
            s_intf_list.push_back(s_intf);
        }
        PortBucketBitmap map;
        VmInterfaceAgentBMap vmif_map;
        L4PortBitmap &port_bmap = it->second.port_bitmap;
        port_bmap.Encode(map);
        vmif_map.set_name(vm_port->GetCfgName());
        vmif_map.set_port_bucket_bmap(map);
        if_bmap_list.push_back(vmif_map);
    }

    LastVmUveSet::iterator uve_it = last_vm_uve_set_.find(vm->GetCfgName());
    UveVirtualMachineAgent &last_uve = uve_it->second.uve_info;
    if (UveVmIfStatsListChanged(s_intf_list, last_uve)) {
        uve->uve_info.set_if_stats_list(s_intf_list);
        last_uve.set_if_stats_list(s_intf_list);
        changed = true;
    }
    
    if (last_uve.get_if_bmap_list() != if_bmap_list) {
        uve->uve_info.set_if_bmap_list(if_bmap_list);
        last_uve.set_if_bmap_list(if_bmap_list);
        changed = true;
    }

    if (SetVmPortBitmap(vm_port_bitmap, uve)) {
        changed = true;
    }
    return changed;
}

bool UveClient::FrameVmMsg(const VmEntry *vm, L4PortBitmap *vm_port_bitmap, 
                           UveVmEntry *uve) {
    bool changed = false;
    uve->uve_info.set_name(vm->GetCfgName());
    vector<VmInterfaceAgent> s_intf_list;

    for (VmIntfMap::iterator it = vm_intf_map_.find(vm); 
         it != vm_intf_map_.end(); it++) {

        if (it->first != vm) {
            break;
        }

        VmInterfaceAgent s_intf;
        const Interface *intf = it->second.intf;
        const VmPortInterface *vm_port =
            static_cast<const VmPortInterface *>(intf);
        if (FrameIntfMsg(vm_port, &s_intf)) {
            s_intf_list.push_back(s_intf);
        }
    }

    LastVmUveSet::iterator uve_it = last_vm_uve_set_.find(vm->GetCfgName());
    UveVirtualMachineAgent &last_uve = uve_it->second.uve_info;
    if (UveVmIfListChanged(s_intf_list, last_uve)) {
        uve->uve_info.set_interface_list(s_intf_list);
        last_uve.set_interface_list(s_intf_list);
        changed = true;
    }

    string hostname = Agent::GetInstance()->GetHostName();
    if (UveVmVRouterChanged(hostname, last_uve)) {
        uve->uve_info.set_vrouter(hostname);
        last_uve.set_vrouter(hostname);
        changed = true;
    }


    if (SetVmPortBitmap(vm_port_bitmap, uve)) {
        changed = true;
    }

    return changed;
}

void UveClient::SendVmMsg(const VmEntry *vm, bool stats) {
    if (vm->GetCfgName() == Agent::GetInstance()->NullString()) {
        return;
    }

    LastVmUveSet::iterator it = last_vm_uve_set_.find(vm->GetCfgName());
    if (it == last_vm_uve_set_.end()) {
        return;
    }

    bool ret = false;
    UveVmEntry uve;
    if (stats) {
        ret = FrameVmStatsMsg(vm, &it->second.port_bitmap, &uve);
    } else {
        ret = FrameVmMsg(vm, &it->second.port_bitmap, &uve);
    }
    if (ret) {
        UveVirtualMachineAgentTrace::Send(uve.uve_info);
    }
}

bool UveClient::UveVmVRouterChanged(string &new_value, 
                                    const UveVirtualMachineAgent &s_vm) {
    if (!s_vm.__isset.vrouter) {
        return true;
    }
    if (new_value.compare(s_vm.get_vrouter()) == 0) {
        return false;
    }
    return true;
}

bool UveClient::UveVmIfListChanged(vector<VmInterfaceAgent> &new_list, 
                                   const UveVirtualMachineAgent &s_vm) {
    vector<VmInterfaceAgent> old_list = s_vm.get_interface_list();
    if (new_list != old_list) {
        return true;
    }
    return false;
}

bool UveClient::UveVmIfStatsListChanged(vector<VmInterfaceAgentStats> &new_list,
                                        const UveVirtualMachineAgent &s_vm) {
    vector<VmInterfaceAgentStats> old_list = s_vm.get_if_stats_list();
    if (new_list != old_list) {
        return true;
    }
    return false;
}

void UveClient::SendVmStats(void) {
    //Go thru VN and send there stats
    const VmEntry *prev_vm = NULL;
    for (VmIntfMap::iterator it = vm_intf_map_.begin();
         it != vm_intf_map_.end(); ++it) {
        const VmEntry *vm = it->first;
        if (prev_vm == vm) {
            continue;
        }
        SendVmMsg(vm, true);
        prev_vm = vm;
    }
}

void UveClient::DeleteAllIntf(const VmEntry *e) {
    VmIntfMap::iterator it = vm_intf_map_.find(e);
    while (it != vm_intf_map_.end()) {
        vm_intf_map_.erase(it);
        it = vm_intf_map_.find(e);
    }
}

void UveClient::VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmEntry *vm = static_cast<const VmEntry *>(e);

    VmUveState *state = static_cast<VmUveState *>
        (e->GetState(partition->parent(), vm_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            VrouterObjectVmNotify(vm);
            DeleteAllIntf(vm);

            UveVirtualMachineAgent s_vm;
            s_vm.set_name(vm->GetCfgName());
            s_vm.set_deleted(true); 
            UveVirtualMachineAgentTrace::Send(s_vm);
            last_vm_uve_set_.erase(s_vm.get_name());
            if (Agent::GetInstance()->IsTestMode() == false) {
                VmStat::Stop(state->stat_);
            }
            e->ClearState(partition->parent(), vm_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new VmUveState();
        e->SetState(partition->parent(), vm_listener_id_, state);
        //Send vrouter object inly for a add/delete
        VrouterObjectVmNotify(vm);

        UveVmEntry uve;
        uve.uve_info.set_name(vm->GetCfgName());
        last_vm_uve_set_.insert(LastVmUvePair(vm->GetCfgName(), uve));

        //Create object to poll for VM stats
        if (Agent::GetInstance()->IsTestMode() == false) { 
            VmStat *stat = new VmStat(vm->GetUuid());
            stat->Start();
            state->stat_ = stat;
        }
    }
    SendVmMsg(vm, false);
}

bool UveClient::PopulateInterVnStats(string vn_name,
                                     UveVirtualNetworkAgent *s_vn) {
    bool changed = false;
    InterVnStatsCollector::VnStatsSet *stats_set = 
            AgentUve::GetInstance()->GetInterVnStatsCollector()->Find(vn_name);

    if (!stats_set) {
        return false;
    }
    vector<UveInterVnStats> in_list;
    vector<UveInterVnStats> out_list;
    
    InterVnStatsCollector::VnStatsSet::iterator it = stats_set->begin();
    VnStats *stats;
    while (it != stats_set->end()) {
        stats = *it;
        UveInterVnStats uve_stats;
        uve_stats.set_other_vn(stats->dst_vn);

        uve_stats.set_tpkts(stats->in_pkts);
        uve_stats.set_bytes(stats->in_bytes);
        in_list.push_back(uve_stats);

        uve_stats.set_tpkts(stats->out_pkts);
        uve_stats.set_bytes(stats->out_bytes);
        out_list.push_back(uve_stats);
        it++;
    }
    LastVnUveSet::iterator uve_it = last_vn_uve_set_.find(s_vn->get_name());
    UveVirtualNetworkAgent &last_uve = uve_it->second.uve_info;

    if (!in_list.empty()) {
        if (uve_it == last_vn_uve_set_.end() ||
            UveInterVnInStatsChanged(in_list, last_uve)) {
            s_vn->set_in_stats(in_list);
            last_uve.set_in_stats(in_list);
            changed = true;
        }
    }
    if (!out_list.empty()) {
        if (uve_it == last_vn_uve_set_.end() ||
            UveInterVnOutStatsChanged(out_list, last_uve)) {
            s_vn->set_out_stats(out_list);
            last_uve.set_out_stats(out_list);
            changed = true;
        }
    }
    return changed;
}

bool UveClient::SetVRouterPortBitmap(VrouterStatsAgent &vr_stats) {
    bool changed = false;

    vector<uint32_t> tcp_sport;
    if (port_bitmap_.tcp_sport_.Sync(tcp_sport)) {
        vr_stats.set_tcp_sport_bitmap(tcp_sport);
        changed = true;
    }

    vector<uint32_t> tcp_dport;
    if (port_bitmap_.tcp_dport_.Sync(tcp_dport)) {
        vr_stats.set_tcp_dport_bitmap(tcp_dport);
        changed = true;
    }

    vector<uint32_t> udp_sport;
    if (port_bitmap_.udp_sport_.Sync(udp_sport)) {
        vr_stats.set_udp_sport_bitmap(udp_sport);
        changed = true;
    }

    vector<uint32_t> udp_dport;
    if (port_bitmap_.udp_dport_.Sync(udp_dport)) {
        vr_stats.set_udp_dport_bitmap(udp_dport);
        changed = true;
    }
    return changed;
}

bool UveClient::SetVmPortBitmap(L4PortBitmap *port_bitmap, UveVmEntry *uve) {
    bool changed = false;

    vector<uint32_t> tcp_sport;
    if (port_bitmap->tcp_sport_.Sync(tcp_sport)) {
        uve->uve_info.set_tcp_sport_bitmap(tcp_sport);
        changed = true;
    }

    vector<uint32_t> tcp_dport;
    if (port_bitmap->tcp_dport_.Sync(tcp_dport)) {
        uve->uve_info.set_tcp_dport_bitmap(tcp_dport);
        changed = true;
    }

    vector<uint32_t> udp_sport;
    if (port_bitmap->udp_sport_.Sync(udp_sport)) {
        uve->uve_info.set_udp_sport_bitmap(udp_sport);
        changed = true;
    }

    vector<uint32_t> udp_dport;
    if (port_bitmap->udp_dport_.Sync(udp_dport)) {
        uve->uve_info.set_udp_dport_bitmap(udp_dport);
        changed = true;
    }
    return changed;
}

bool UveClient::SetVnPortBitmap(L4PortBitmap *port_bitmap, UveVnEntry *uve) {
    bool changed = false;

    vector<uint32_t> tcp_sport;
    if (port_bitmap->tcp_sport_.Sync(tcp_sport)) {
        uve->uve_info.set_tcp_sport_bitmap(tcp_sport);
        changed = true;
    }

    vector<uint32_t> tcp_dport;
    if (port_bitmap->tcp_dport_.Sync(tcp_dport)) {
        uve->uve_info.set_tcp_dport_bitmap(tcp_dport);
        changed = true;
    }

    vector<uint32_t> udp_sport;
    if (port_bitmap->udp_sport_.Sync(udp_sport)) {
        uve->uve_info.set_udp_sport_bitmap(udp_sport);
        changed = true;
    }

    vector<uint32_t> udp_dport;
    if (port_bitmap->udp_dport_.Sync(udp_dport)) {
        uve->uve_info.set_udp_dport_bitmap(udp_dport);
        changed = true;
    }
    return changed;
}

bool UveClient::FrameVnMsg(const VnEntry *vn, L4PortBitmap *vn_port_bitmap, 
                           UveVnEntry *uve) {
    bool changed = false;
    uve->uve_info.set_name(vn->GetName());
    
    vector<string> s_intf_list;
    int fip_count = 0;
    for (VnIntfMap::iterator it = vn_intf_map_.find(vn); 
         it != vn_intf_map_.end(); it++) {

        if (it->first != vn) {
            break;
        }

        string intf_name;
        const Interface *intf = it->second;
        const VmPortInterface *vm_port = 
            static_cast<const VmPortInterface *>(intf);
        intf_name = vm_port->GetCfgName();
        fip_count += vm_port->GetFloatingIpCount();
        s_intf_list.push_back(intf_name);
    }

    LastVnUveSet::iterator it = last_vn_uve_set_.find(vn->GetName());
    UveVirtualNetworkAgent &last_uve = it->second.uve_info;

    if (s_intf_list.size()) {
        if (UveVnIfListChanged(s_intf_list, last_uve)) {
            uve->uve_info.set_interface_list(s_intf_list);
            last_uve.set_interface_list(s_intf_list);
            changed = true;
        }
    }

    string acl_name;
    int acl_rule_count;
    if (vn->GetAcl()) {
        acl_name = vn->GetAcl()->GetName();
        acl_rule_count = vn->GetAcl()->Size();
    } else {
        acl_name = Agent::GetInstance()->NullString();
        acl_rule_count = 0;
    }

    if (UveVnAclChanged(acl_name, last_uve)) {  
        uve->uve_info.set_acl(acl_name);
        last_uve.set_acl(acl_name);
        changed = true;
    }
    
    if (UveVnAclRuleCountChanged(acl_rule_count, last_uve)) {
        uve->uve_info.set_total_acl_rules(acl_rule_count);
        last_uve.set_total_acl_rules(acl_rule_count);
        changed = true;
    }

    if (vn->GetMirrorCfgAcl()) {
        acl_name = vn->GetMirrorCfgAcl()->GetName();
    } else {
        acl_name = Agent::GetInstance()->NullString();
    }
    if (it == last_vn_uve_set_.end() ||
        UveVnMirrorAclChanged(acl_name, last_uve)) {
        uve->uve_info.set_mirror_acl(acl_name);
        last_uve.set_mirror_acl(acl_name);
        changed = true;
    }

    if (SetVnPortBitmap(vn_port_bitmap, uve)) {
        changed = true;
    }

    return changed;
}

bool UveClient::UpdateVnFlowCount(const VnEntry *vn, LastVnUveSet::iterator &it,
                                  UveVirtualNetworkAgent *s_vn) {
    int flow_count = FlowTable::GetFlowTableObject()->VnFlowSize(vn);
    if (UveVnFlowCountChanged(flow_count, it->second.uve_info)) {
        s_vn->set_flow_count(flow_count);
        it->second.uve_info.set_flow_count(flow_count);
        return true;
    }
    return false;
}

bool UveClient::UpdateVnFipCount(LastVnUveSet::iterator &it, int count, 
                                 UveVirtualNetworkAgent *s_vn) {
    if (UveVnFipCountChanged(count, it->second.uve_info)) {
        s_vn->set_associated_fip_count(count);
        it->second.uve_info.set_associated_fip_count(count);
        return true;
    }
    return false;
}

bool UveClient::GetUveVnEntry(const string vn_name, UveVnEntry &entry) {
    LastVnUveSet::iterator it = last_vn_uve_set_.find(vn_name);
    if (it == last_vn_uve_set_.end()) {
        return false;
    }
    entry = it->second;
    return true;
}

bool UveClient::FrameVnStatsMsg(const VnEntry *vn,
                                L4PortBitmap *vn_port_bitmap, UveVnEntry *uve) {
    bool changed = false;
    uve->uve_info.set_name(vn->GetName());

    uint64_t in_pkts = 0;
    uint64_t in_bytes = 0;
    uint64_t out_pkts = 0;
    uint64_t out_bytes = 0;

    int fip_count = 0;
    for (VnIntfMap::iterator it = vn_intf_map_.find(vn); 
         it != vn_intf_map_.end(); it++) {

        if (it->first != vn) {
            break;
        }

        const Interface *intf = it->second;
        const VmPortInterface *vm_port = static_cast<const VmPortInterface *>(intf);
        fip_count += vm_port->GetFloatingIpCount();

        const AgentStatsCollector::IfStats *s = 
            AgentUve::GetInstance()->GetStatsCollector()->GetIfStats(intf);
        if (s == NULL) {
            continue;
        }
        in_pkts += s->in_pkts;
        in_bytes += s->in_bytes;
        out_pkts += s->out_pkts;
        out_bytes += s->out_bytes;
    }
    
    LastVnUveSet::iterator it = last_vn_uve_set_.find(vn->GetName());
    UveVnEntry &prev_uve_vn_entry = it->second;
    UveVirtualNetworkAgent &last_uve = prev_uve_vn_entry.uve_info;

    uint64_t diff_in_bytes = 0;
    if (UveVnIfInStatsChanged(in_bytes, in_pkts, last_uve)) {
        uve->uve_info.set_in_tpkts(in_pkts);
        uve->uve_info.set_in_bytes(in_bytes);
        last_uve.set_in_tpkts(in_pkts);
        last_uve.set_in_bytes(in_bytes);
        changed = true;
    }

    uint64_t diff_out_bytes = 0;
    if (UveVnIfOutStatsChanged(out_bytes, out_pkts, last_uve)) {
        uve->uve_info.set_out_tpkts(out_pkts);
        uve->uve_info.set_out_bytes(out_bytes);
        last_uve.set_out_tpkts(out_pkts);
        last_uve.set_out_bytes(out_bytes);
        changed = true;
    }

    uint64_t diff_seconds = 0;
    uint64_t cur_time = UTCTimestampUsec();
    bool send_bandwidth = false;
    uint64_t in_band, out_band;
    if (it->second.prev_stats_update_time == 0) {
        in_band = out_band = 0;
        send_bandwidth = true;
        it->second.prev_stats_update_time = cur_time;
    } else {
        diff_seconds = (cur_time - it->second.prev_stats_update_time) / 
                       bandwidth_intvl_;
        if (diff_seconds > 0) {
            diff_in_bytes = in_bytes - prev_uve_vn_entry.prev_in_bytes;
            diff_out_bytes = out_bytes - prev_uve_vn_entry.prev_out_bytes;
            in_band = (diff_in_bytes * 8)/diff_seconds;
            out_band = (diff_out_bytes * 8)/diff_seconds;
            it->second.prev_stats_update_time = cur_time;
            prev_uve_vn_entry.prev_in_bytes = in_bytes;
            prev_uve_vn_entry.prev_out_bytes = out_bytes;
            send_bandwidth = true;
        }
    }
    if (send_bandwidth && UveVnInBandChanged(in_band, last_uve)) {
        uve->uve_info.set_in_bandwidth_usage(in_band);
        last_uve.set_in_bandwidth_usage(in_band);
        changed = true;
    }

    if (send_bandwidth && UveVnOutBandChanged(out_band, last_uve)) {
        uve->uve_info.set_out_bandwidth_usage(out_band);
        last_uve.set_out_bandwidth_usage(out_band);
        changed = true;
    }

    int acl_rule_count;
    if (vn->GetAcl()) {
        acl_rule_count = vn->GetAcl()->Size();
    } else {
        acl_rule_count = 0;
    }
    /* We have not registered for ACL notification. So total_acl_rules
     * field is updated during stats updation
     */
    if (UveVnAclRuleCountChanged(acl_rule_count, last_uve)) {
        uve->uve_info.set_total_acl_rules(acl_rule_count);
        last_uve.set_total_acl_rules(acl_rule_count);
        changed = true;
    }

    if (UpdateVnFlowCount(vn, it, &uve->uve_info)) {
        changed = true;
    }

    if (UpdateVnFipCount(it, fip_count, &uve->uve_info)) {
        changed = true;
    }

    /* VM interface list for VN is sent whenever any VM is added or
     * removed from VN. That message has only two fields set - vn name
     * and virtualmachine_list */

    if (PopulateInterVnStats(vn->GetName(), &uve->uve_info)) {
        changed = true;
    }

    if (SetVnPortBitmap(vn_port_bitmap, uve)) {
        changed = true;
    }

    VrfEntry *vrf = vn->GetVrf();
    if (vrf) {
        UveVrfStats vrf_stats;
        vector<UveVrfStats> vlist;

        AgentStatsCollector::VrfStats *s = 
              AgentUve::GetInstance()->GetStatsCollector()->GetVrfStats(vrf->GetVrfId());
        if (s != NULL) {
            vrf_stats.set_name(s->name);
            vrf_stats.set_discards(s->discards);
            vrf_stats.set_resolves(s->resolves);
            vrf_stats.set_receives(s->receives);
            vrf_stats.set_tunnels(s->tunnels);
            vrf_stats.set_composites(s->composites);
            vrf_stats.set_encaps(s->encaps);
            vlist.push_back(vrf_stats);
            if (UveVnVrfStatsChanged(vlist, last_uve)) {
                uve->uve_info.set_vrf_stats_list(vlist);
                last_uve.set_vrf_stats_list(vlist);
                changed = true;
            }
        }
    }
    return changed;
}

void UveClient::SendVnMsg(const VnEntry *vn, bool stats) {
    if (vn->GetName() == Agent::GetInstance()->NullString()) {
       return;
    }

    LastVnUveSet::iterator it = last_vn_uve_set_.find(vn->GetName());
    if (it == last_vn_uve_set_.end()) {
        return;
    }

    UveVnEntry uve;
    bool send;
    if (stats) {
        send = FrameVnStatsMsg(vn, &it->second.port_bitmap, &uve);
    } else {
        send = FrameVnMsg(vn, &it->second.port_bitmap, &uve);
    }
    if (send) {
        UveVirtualNetworkAgentTrace::Send(uve.uve_info);
    }
}

void UveClient::SendUnresolvedVnMsg(string vn_name) {
    LastVnUveSet::iterator it = last_vn_uve_set_.find(vn_name);
    if (it == last_vn_uve_set_.end()) {
        return;
    }

    bool changed;
    UveVnEntry uve;
    uve.uve_info.set_name(vn_name);
    changed = PopulateInterVnStats(vn_name, &uve.uve_info);

    AgentStatsCollector *collector = AgentUve::GetInstance()->GetStatsCollector();
    /* Send Nameless VrfStats as part of Unknown VN */
    if (vn_name.compare(*FlowHandler::UnknownVn()) == 0) {
        UveVirtualNetworkAgent &last_uve = it->second.uve_info;
        AgentStatsCollector::VrfStats *s = collector->GetVrfStats(
                                               collector->GetNamelessVrfId());
        if (s) {
            UveVrfStats vrf_stats;
            vector<UveVrfStats> vlist;
            vrf_stats.set_name(s->name);
            vrf_stats.set_discards(s->discards);
            vrf_stats.set_resolves(s->resolves);
            vrf_stats.set_receives(s->receives);
            vrf_stats.set_tunnels(s->tunnels);
            vrf_stats.set_composites(s->composites);
            vrf_stats.set_encaps(s->encaps);
            vlist.push_back(vrf_stats);
            if (UveVnVrfStatsChanged(vlist, last_uve)) {
                uve.uve_info.set_vrf_stats_list(vlist);
                last_uve.set_vrf_stats_list(vlist);
                changed = true;
            }
        }
    }

    if (changed) {
        UveVirtualNetworkAgentTrace::Send(uve.uve_info);
    }
}

bool UveClient::UveVnAclRuleCountChanged(int32_t size, 
                                const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.total_acl_rules) {
        return true;
    }
    if (size != s_vn.get_total_acl_rules()) {
        return true;
    }
    return false;
}

bool UveClient::UveVnFlowCountChanged(int32_t size, 
                                const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.flow_count) {
        return true;
    }
    if (size != s_vn.get_flow_count()) {
        return true;
    }
    return false;
}

bool UveClient::UveVnFipCountChanged(int32_t size, 
                                     const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.associated_fip_count) {
        return true;
    }
    if (size != s_vn.get_associated_fip_count()) {
        return true;
    }
    return false;
}

bool UveClient::UveVnAclChanged(string name, 
                                const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.acl) {
        return true;
    }
    if (name.compare(s_vn.get_acl()) != 0) {
        return true;
    }
    return false;
}

bool UveClient::UveVnMirrorAclChanged(string name, const 
                                      UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.mirror_acl) {
        return true;
    }
    if (name.compare(s_vn.get_mirror_acl()) != 0) {
        return true;
    }
    return false;
}

bool UveClient::UveVnIfInStatsChanged(uint64_t bytes, uint64_t pkts, 
                                      const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.in_bytes || !s_vn.__isset.in_tpkts) {
        return true;
    }
    uint64_t bytes2 = (uint64_t)s_vn.get_in_bytes();
    uint64_t  pkts2 = (uint64_t)s_vn.get_in_tpkts();

    if ((bytes != bytes2) || (pkts != pkts2)) {
        return true;
    }
    return false;
}

bool UveClient::UveVnIfOutStatsChanged(uint64_t bytes, uint64_t pkts, 
                                       const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.out_bytes || !s_vn.__isset.out_tpkts) {
        return true;
    }
    uint64_t bytes2 = (uint64_t)s_vn.get_out_bytes();
    uint64_t  pkts2 = (uint64_t)s_vn.get_out_tpkts();

    if ((bytes != bytes2) || (pkts != pkts2)) {
        return true;
    }
    return false;
}

bool UveClient::UveVnInBandChanged(uint64_t in_band, 
                                   const UveVirtualNetworkAgent &prev_vn) {
    if (!prev_vn.__isset.in_bandwidth_usage) {
        return true;
    }
    if (in_band != prev_vn.get_in_bandwidth_usage()) {
        return true;
    }
    return false;
}

bool UveClient::UveVnOutBandChanged(uint64_t out_band, 
                                    const UveVirtualNetworkAgent &prev_vn) {
    if (!prev_vn.__isset.out_bandwidth_usage) {
        return true;
    }
    if (out_band != prev_vn.get_out_bandwidth_usage()) {
        return true;
    }
    return false;
}

bool UveClient::UveVnVrfStatsChanged(vector<UveVrfStats> &vlist, 
                                    const UveVirtualNetworkAgent &prev_vn) {
    if (!prev_vn.__isset.vrf_stats_list) {
        return true;
    }
    if (vlist != prev_vn.get_vrf_stats_list()) {
        return true;
    }
    return false;
}

bool UveClient::UveVnIfListChanged(vector<string> new_list, 
                                   const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.interface_list) {
        return true;
    }
    if (new_list != s_vn.get_interface_list()) {
        return true;
    }
    return false;
}

bool UveClient::UveInterVnInStatsChanged(vector<UveInterVnStats> new_list, 
                                         const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.in_stats) {
        return true;
    }
    if (new_list != s_vn.get_in_stats()) {
        return true;
    }
    return false;
}

bool UveClient::UveInterVnOutStatsChanged(vector<UveInterVnStats> new_list, 
                                          const UveVirtualNetworkAgent &s_vn) {
    if (!s_vn.__isset.out_stats) {
        return true;
    }
    if (new_list != s_vn.get_out_stats()) {
        return true;
    }
    return false;
}

void UveClient::SendVnStats(void) {
    //Go thru VN and send there stats
    const VnEntry *prev_vn = NULL;
    for (VnIntfMap::iterator it = vn_intf_map_.begin(); 
         it != vn_intf_map_.end(); ++it) {
        const VnEntry *vn = it->first;
        if (prev_vn == vn) {
            continue;
        }
        SendVnMsg(vn, true);
        prev_vn = vn;
    }
    SendUnresolvedVnMsg(*FlowHandler::UnknownVn());
    SendUnresolvedVnMsg(*FlowHandler::LinkLocalVn());
}

void UveClient::DeleteAllIntf(const VnEntry *e) {
    VnIntfMap::iterator it = vn_intf_map_.find(e);
    while (it != vn_intf_map_.end()) {
        vn_intf_map_.erase(it);
        it = vn_intf_map_.find(e);
    }
}
 
void UveClient::VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VnEntry *vn = static_cast<const VnEntry *>(e);

    UveState *state = static_cast<UveState *>
        (e->GetState(partition->parent(), vn_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            VrouterObjectVnNotify(vn);
            AgentUve::GetInstance()->GetInterVnStatsCollector()->Remove(vn->GetName());
            DeleteAllIntf(vn);

            UveVirtualNetworkAgent s_vn;
            s_vn.set_name(vn->GetName());
            s_vn.set_deleted(true); 
            UveVirtualNetworkAgentTrace::Send(s_vn);
            last_vn_uve_set_.erase(s_vn.get_name());

            delete state;
            e->ClearState(partition->parent(), vn_listener_id_);

        }
        return;
    }

    if (!state) {
        state = new UveState();
        e->SetState(partition->parent(), vn_listener_id_, state);
        VrouterObjectVnNotify(vn);

        AddLastVnUve(vn->GetName());
    }

    SendVnMsg(vn, false);
}

void UveClient::VnWalkDone(DBTableBase *base, 
                           std::vector<std::string> *vn_list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(Agent::GetInstance()->GetHostName());
    vrouter_agent.set_connected_networks(*vn_list);
    UveVrouterAgent::Send(vrouter_agent);
    delete vn_list;
}

bool UveClient::AppendVn(DBTablePartBase *part, DBEntryBase *entry, 
                         std::vector<std::string> *vn_list) {
    VnEntry *vn = static_cast<VnEntry *>(entry);

    if (!vn->IsDeleted()) {
        vn_list->push_back(vn->GetName());
    }
    return true;
}

void UveClient::SendVrouterUve() {
    VrouterAgent vrouter_agent;
    bool changed = false;
    static bool first = true, build_info = false;
    vrouter_agent.set_name(Agent::GetInstance()->GetHostName());
    Ip4Address rid = Agent::GetInstance()->GetRouterId();

    if (first) {
        vector<string> core_list;
        MiscUtils::GetCoreFileList(Agent::GetInstance()->GetProgramName(), core_list);
        if (core_list.size()) {
            vrouter_agent.set_core_files_list(core_list);
        }

        //Physical interface list
        vector<AgentInterface> phy_if_list;
        PhyIntfSet::iterator it = phy_intf_set_.begin();
        while (it != phy_intf_set_.end()) {
            AgentInterface pitf;
            const Interface *intf = *it;
            const EthInterface *port = static_cast<const EthInterface *>(intf);
            pitf.set_name(intf->GetName());
            pitf.set_mac_address(GetMacAddress(port->GetMacAddr()));
            phy_if_list.push_back(pitf);
            ++it;
        }
        vrouter_agent.set_phy_if(phy_if_list);

        //vhost attributes
        VirtualHostInterfaceKey key(nil_uuid(), Agent::GetInstance()->GetVirtualHostInterfaceName());
        const Interface *vhost = static_cast<const Interface *>(Agent::GetInstance()->GetInterfaceTable()->FindActiveEntry(&key));
        if (vhost) {
            AgentInterface vitf;
            vitf.set_name(vhost->GetName());
            vitf.set_mac_address(GetMacAddress(vhost->GetMacAddr()));
            vrouter_agent.set_vhost_if(vitf);
        }
        vrouter_agent.set_control_ip(Agent::GetInstance()->GetMgmtIp());
        first = false;
        changed = true;
    }

    if (!build_info) {
        string build_info_str;
        build_info = Agent::GetInstance()->GetBuildInfo(build_info_str);
        if (prev_vrouter_.get_build_info() != build_info_str) {
            vrouter_agent.set_build_info(build_info_str);
            prev_vrouter_.set_build_info(build_info_str);
            changed = true;
        }

    }

    std::vector<AgentXmppPeer> xmpp_list;
    for (int count = 0; count < MAX_XMPP_SERVERS; count++) {
        AgentXmppPeer peer;
        if (!Agent::GetInstance()->GetXmppServer(count).empty()) {
            peer.set_ip(Agent::GetInstance()->GetXmppServer(count));
            AgentXmppChannel *ch = Agent::GetInstance()->GetAgentXmppChannel(count);
            if (ch == NULL) {
                continue;
            }
            XmppChannel *xc = ch->GetXmppChannel();
            if (xc == NULL) {
                continue;
            }
            if (xc->GetPeerState() == xmps::READY) {
                peer.set_status(true);
            } else {
                peer.set_status(false);
            }
            peer.set_setup_time(Agent::GetInstance()->GetAgentXmppChannelSetupTime(count));
            if (Agent::GetInstance()->GetXmppCfgServerIdx() == count) {
                peer.set_primary(true);
            } else {
                peer.set_primary(false);
            }
            xmpp_list.push_back(peer);
        }
    }

    if (!prev_vrouter_.__isset.xmpp_peer_list ||
        prev_vrouter_.get_xmpp_peer_list() != xmpp_list) {
        vrouter_agent.set_xmpp_peer_list(xmpp_list);
        prev_vrouter_.set_xmpp_peer_list(xmpp_list);
        changed = true;
    }
    vector<string> ip_list;
    ip_list.push_back(rid.to_string());
    if (!prev_vrouter_.__isset.self_ip_list ||
        prev_vrouter_.get_self_ip_list() != ip_list) {

        vrouter_agent.set_self_ip_list(ip_list);
        prev_vrouter_.set_self_ip_list(ip_list);
        changed = true;
    }

    AgentConfig *config = AgentConfig::GetInstance();
    const AgentCmdLineParams cmd_line = config->GetCmdLineParams();
    AgentVhostConfig vhost_cfg;
    AgentXenConfig xen_cfg;
    vector<string> dns_list;
    string xen_ll_name;
    Ip4Address xen_ll_addr;
    int xen_ll_plen;

    if (!prev_vrouter_.__isset.log_file ||
        prev_vrouter_.get_log_file() != cmd_line.log_file_) {
        vrouter_agent.set_log_file(cmd_line.log_file_);
        prev_vrouter_.set_log_file(cmd_line.log_file_);
        changed = true;
    }

    if (!prev_vrouter_.__isset.config_file ||
        prev_vrouter_.get_config_file() != cmd_line.cfg_file_) {
        vrouter_agent.set_config_file(cmd_line.cfg_file_);
        prev_vrouter_.set_config_file(cmd_line.cfg_file_);
        changed = true;
    }

    if (!prev_vrouter_.__isset.log_local ||
        prev_vrouter_.get_log_local() != cmd_line.log_local_) {
        vrouter_agent.set_log_local(cmd_line.log_local_);
        prev_vrouter_.set_log_local(cmd_line.log_local_);
        changed = true;
    }

    if (!prev_vrouter_.__isset.log_category ||
        prev_vrouter_.get_log_category() != cmd_line.log_category_) {
        vrouter_agent.set_log_category(cmd_line.log_category_);
        prev_vrouter_.set_log_category(cmd_line.log_category_);
        changed = true;
    }

    if (!prev_vrouter_.__isset.log_level ||
        prev_vrouter_.get_log_level() != cmd_line.log_level_) {
        vrouter_agent.set_log_level(cmd_line.log_level_);
        prev_vrouter_.set_log_level(cmd_line.log_level_);
        changed = true;
    }
    if (!prev_vrouter_.__isset.sandesh_http_port ||
        prev_vrouter_.get_sandesh_http_port() != cmd_line.http_server_port_) {
        vrouter_agent.set_sandesh_http_port(cmd_line.http_server_port_);
        prev_vrouter_.set_sandesh_http_port(cmd_line.http_server_port_);
        changed = true;
    }

    vhost_cfg.set_name(config->GetVHostName());
    vhost_cfg.set_ip(rid.to_string());
    vhost_cfg.set_ip_prefix_len(config->GetVHostPlen());
    vhost_cfg.set_gateway(config->GetVHostGateway());
    if (!prev_vrouter_.__isset.vhost_cfg ||
        prev_vrouter_.get_vhost_cfg() != vhost_cfg) {
        vrouter_agent.set_vhost_cfg(vhost_cfg);
        prev_vrouter_.set_vhost_cfg(vhost_cfg);
        changed = true;
    }

    if (!prev_vrouter_.__isset.eth_name ||
        prev_vrouter_.get_eth_name() != config->GetEthPort()) {
        vrouter_agent.set_eth_name(config->GetEthPort());
        prev_vrouter_.set_eth_name(config->GetEthPort());
        changed = true;
    }

    if (!prev_vrouter_.__isset.tunnel_type ||
        prev_vrouter_.get_tunnel_type() != config->GetTunnelType()) {
        vrouter_agent.set_tunnel_type(config->GetTunnelType());
        prev_vrouter_.set_tunnel_type(config->GetTunnelType());
        changed = true;
    }

    string hypervisor;
    if (config->isKvmMode()) {
        hypervisor = "kvm";
    } else if (config->isXenMode()) {
        hypervisor = "xen";
        config->GetXenInfo(xen_ll_name, xen_ll_addr, xen_ll_plen);
        xen_cfg.set_xen_ll_port(xen_ll_name);
        xen_cfg.set_xen_ll_ip(xen_ll_addr.to_string());
        xen_cfg.set_xen_ll_prefix_len(xen_ll_plen);
        if (!prev_vrouter_.__isset.xen_cfg ||
            prev_vrouter_.get_xen_cfg() != xen_cfg) {
            vrouter_agent.set_xen_cfg(xen_cfg);
            prev_vrouter_.set_xen_cfg(xen_cfg);
            changed = true;
        }
    }

    if (!prev_vrouter_.__isset.hypervisor ||
        prev_vrouter_.get_hypervisor() != hypervisor) {
        vrouter_agent.set_hypervisor(hypervisor);
        prev_vrouter_.set_hypervisor(hypervisor);
        changed = true;
    }

    if (!prev_vrouter_.__isset.ds_addr ||
        prev_vrouter_.get_ds_addr() != config->GetDiscoveryServer()) {
        vrouter_agent.set_ds_addr(config->GetDiscoveryServer());
        prev_vrouter_.set_ds_addr(config->GetDiscoveryServer());
        changed = true;
    }

    if (!prev_vrouter_.__isset.ds_xs_instances ||
        prev_vrouter_.get_ds_xs_instances() != config->GetDiscoveryXmppServerInstances()) {
        vrouter_agent.set_ds_xs_instances(config->GetDiscoveryXmppServerInstances());
        prev_vrouter_.set_ds_xs_instances(config->GetDiscoveryXmppServerInstances());
        changed = true;
    }

    for (int idx = 0; idx < MAX_XMPP_SERVERS; idx++) {
        if (!Agent::GetInstance()->GetDnsXmppServer(idx).empty()) {
            dns_list.push_back(Agent::GetInstance()->GetDnsXmppServer(idx));
        }
    }

    if (!prev_vrouter_.__isset.dns_servers ||
        prev_vrouter_.get_dns_servers() != dns_list) {
        vrouter_agent.set_dns_servers(dns_list);
        prev_vrouter_.set_dns_servers(dns_list);
        changed = true;
    }

    if (changed) {
        UveVrouterAgent::Send(vrouter_agent);
    }
}

void UveClient::VrouterObjectVnNotify(const VnEntry *vn) {
    std::vector<std::string> *vn_list = new std::vector<std::string>();
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    walker->WalkTable(Agent::GetInstance()->GetVnTable(), NULL, 
                  boost::bind(&UveClient::AppendVn, singleton_, _1, _2, vn_list),
                  boost::bind(&UveClient::VnWalkDone, singleton_, _1, vn_list));
}

void UveClient::VmWalkDone(DBTableBase *base, 
                           std::vector<std::string> *vm_list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(Agent::GetInstance()->GetHostName());
    vrouter_agent.set_virtual_machine_list(*vm_list);
    UveVrouterAgent::Send(vrouter_agent);
    delete vm_list;
}

bool UveClient::AppendVm(DBTablePartBase *part, DBEntryBase *entry, 
                         std::vector<std::string> *vm_list) {
    VmEntry *vm = static_cast<VmEntry *>(entry);

    if (!vm->IsDeleted()) {
        std::ostringstream ostr;
        ostr << vm->GetUuid();
        vm_list->push_back(ostr.str());
    }
    return true;
}

void UveClient::VrouterObjectVmNotify(const VmEntry *vm) {
    std::vector<std::string> *vm_list = new std::vector<std::string>();
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    walker->WalkTable(Agent::GetInstance()->GetVmTable(), NULL,
        boost::bind(&UveClient::AppendVm, singleton_, _1, _2, vm_list),
        boost::bind(&UveClient::VmWalkDone, singleton_, _1, vm_list));
}

void UveClient::IntfWalkDone(DBTableBase *base, 
                             std::vector<std::string> *intf_list,
                             std::vector<std::string> *err_if_list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(Agent::GetInstance()->GetHostName());
    if (intf_list) {
        vrouter_agent.set_interface_list(*intf_list);
    }
    vrouter_agent.set_error_intf_list(*err_if_list);
    UveVrouterAgent::Send(vrouter_agent);
    if (intf_list) {
        delete intf_list;
    }
    delete err_if_list;
}

bool UveClient::AppendIntf(DBTablePartBase *part, DBEntryBase *entry, 
                         std::vector<std::string> *intf_list,
                         std::vector<std::string> *err_if_list) {
    Interface *intf = static_cast<Interface *>(entry);

    if (intf->GetType() == Interface::VMPORT) {
        const VmPortInterface *port = static_cast<const VmPortInterface *>(intf);
        if (intf_list && !entry->IsDeleted()) {
            intf_list->push_back(port->GetCfgName());
        }
        if (!intf->GetActiveState() && !entry->IsDeleted()) {
           err_if_list->push_back(port->GetCfgName());
        }
    }
    return true;
}

void UveClient::VrouterObjectIntfNotify(const Interface *intf, bool if_list) {
    std::vector<std::string> *intf_list = NULL;
    if (if_list) {
        intf_list = new std::vector<std::string>();
    }
    std::vector<std::string> *err_if_list = new std::vector<std::string>();
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    walker->WalkTable(Agent::GetInstance()->GetInterfaceTable(), NULL,
        boost::bind(&UveClient::AppendIntf, singleton_,_1, _2, intf_list, err_if_list),
        boost::bind(&UveClient::IntfWalkDone, singleton_, _1, intf_list, err_if_list));
}

string UveClient::GetMacAddress(const ether_addr &mac) {
    stringstream ss;
    ss << setbase(16) << setfill('0') << setw(2) 
      << static_cast<unsigned int>(mac.ether_addr_octet[0])
      << static_cast<unsigned int>(mac.ether_addr_octet[1])
      << static_cast<unsigned int>(mac.ether_addr_octet[2])
      << static_cast<unsigned int>(mac.ether_addr_octet[3])
      << static_cast<unsigned int>(mac.ether_addr_octet[4])
      << static_cast<unsigned int>(mac.ether_addr_octet[5]);
    return ss.str();
}

uint8_t UveClient::CalculateBandwitdh(uint64_t bytes, int speed_mbps, int diff_seconds) {
    if (bytes == 0 || speed_mbps == 0) {
        return 0;
    }
    uint64_t bits = bytes * 8;
    if (diff_seconds == 0) {
        return 0;
    }
    uint64_t speed_bps = speed_mbps * 1024 * 1024;
    uint64_t bps = bits/diff_seconds;
    return bps/speed_bps * 100;
}

uint64_t UveClient::GetVmPortBandwidth(AgentStatsCollector::IfStats *s, bool dir_in) {
    if (s->stats_time == 0) {
        if (dir_in) {
            s->prev_in_bytes = s->in_bytes;
        } else {
            s->prev_out_bytes = s->out_bytes;
        }
        return 0;
    }
    uint64_t bits;
    if (dir_in) {
        bits = (s->in_bytes - s->prev_in_bytes) * 8;
        s->prev_in_bytes = s->in_bytes;
    } else {
        bits = (s->out_bytes - s->prev_out_bytes) * 8;
        s->prev_out_bytes = s->out_bytes;
    }
    uint64_t cur_time = UTCTimestampUsec();
    uint64_t diff_seconds = (cur_time - s->stats_time) / bandwidth_intvl_;
    if (diff_seconds == 0) {
        return 0;
    }
    return bits/diff_seconds;
}

uint8_t UveClient::GetBandwidthUsage(AgentStatsCollector::IfStats *s, bool dir_in, int mins) {
    uint64_t bytes;
    if (dir_in) {
        switch (mins) {
            case 1:
                bytes = s->in_bytes - s->prev_in_bytes;
                s->prev_in_bytes = s->in_bytes;
                break;
            case 5:
                bytes = s->in_bytes - s->prev_5min_in_bytes;
                s->prev_5min_in_bytes = s->in_bytes;
                break;
            default:
                bytes = s->in_bytes - s->prev_10min_in_bytes;
                s->prev_10min_in_bytes = s->in_bytes;
                break;
        }
    } else {
        switch (mins) {
            case 1:
                bytes = s->out_bytes - s->prev_out_bytes;
                s->prev_out_bytes = s->out_bytes;
                break;
            case 5:
                bytes = s->out_bytes - s->prev_5min_out_bytes;
                s->prev_5min_out_bytes = s->out_bytes;
                break;
            default:
                bytes = s->out_bytes - s->prev_10min_out_bytes;
                s->prev_10min_out_bytes = s->out_bytes;
                break;
        }
    }
    return CalculateBandwitdh(bytes, s->speed, (mins * 60));
}

bool UveClient::BuildPhyIfList(vector<AgentIfStats> &phy_if_list) {
    bool changed = false;
    PhyIntfSet::iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentStatsCollector::IfStats *s = 
              AgentUve::GetInstance()->GetStatsCollector()->GetIfStats(intf);
        if (s == NULL) {
            continue;
        }
        AgentIfStats phy_stat_entry;
        phy_stat_entry.set_name(intf->GetName());
        phy_stat_entry.set_in_pkts(s->in_pkts);
        phy_stat_entry.set_in_bytes(s->in_bytes);
        phy_stat_entry.set_out_pkts(s->out_pkts);
        phy_stat_entry.set_out_bytes(s->out_bytes);
        phy_stat_entry.set_speed(s->speed);
        phy_stat_entry.set_duplexity(s->duplexity);
        phy_if_list.push_back(phy_stat_entry);
        changed = true;
        ++it;
    }
    return changed;
}

bool UveClient::BuildPhyIfBand(vector<AgentIfBandwidth> &phy_if_list, uint8_t mins) {
    uint8_t in_band, out_band;
    bool changed = false;

    PhyIntfSet::iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentStatsCollector::IfStats *s = 
              AgentUve::GetInstance()->GetStatsCollector()->GetIfStats(intf);
        if (s == NULL) {
            continue;
        }
        AgentIfBandwidth phy_stat_entry;
        phy_stat_entry.set_name(intf->GetName());
        in_band = GetBandwidthUsage(s, true, mins);
        out_band = GetBandwidthUsage(s, false, mins);
        phy_stat_entry.set_in_bandwidth_usage(in_band);
        phy_stat_entry.set_out_bandwidth_usage(out_band);
        phy_if_list.push_back(phy_stat_entry);
        changed = true;
        ++it;
    }
    return changed;
}

void UveClient::InitPrevStats() {
    PhyIntfSet::iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentStatsCollector::IfStats *s = 
              AgentUve::GetInstance()->GetStatsCollector()->GetIfStats(intf);
        if (s == NULL) {
            continue;
        }
        s->prev_in_bytes = s->in_bytes;
        s->prev_5min_in_bytes = s->in_bytes;
        s->prev_10min_in_bytes = s->in_bytes;
        s->prev_out_bytes = s->out_bytes;
        s->prev_5min_out_bytes = s->out_bytes;
        s->prev_10min_out_bytes = s->out_bytes;
        ++it;
    }
}

void UveClient::FetchDropStats(AgentDropStats &ds) {
    vr_drop_stats_req stats = AgentUve::GetInstance()->GetStatsCollector()->GetDropStats();
    ds.ds_discard = stats.get_vds_discard();
    ds.ds_pull = stats.get_vds_pull();
    ds.ds_invalid_if = stats.get_vds_invalid_if();
    ds.ds_arp_not_me = stats.get_vds_arp_not_me();
    ds.ds_garp_from_vm = stats.get_vds_garp_from_vm();
    ds.ds_invalid_arp = stats.get_vds_invalid_arp();
    ds.ds_trap_no_if = stats.get_vds_trap_no_if();
    ds.ds_nowhere_to_go = stats.get_vds_nowhere_to_go();
    ds.ds_flow_queue_limit_exceeded = stats.get_vds_flow_queue_limit_exceeded();
    ds.ds_flow_no_memory = stats.get_vds_flow_no_memory();
    ds.ds_flow_invalid_protocol = stats.get_vds_flow_invalid_protocol();
    ds.ds_flow_nat_no_rflow = stats.get_vds_flow_nat_no_rflow();
    ds.ds_flow_action_drop = stats.get_vds_flow_action_drop();
    ds.ds_flow_action_invalid = stats.get_vds_flow_action_invalid();
    ds.ds_flow_unusable = stats.get_vds_flow_unusable();
    ds.ds_flow_table_full = stats.get_vds_flow_table_full();
    ds.ds_interface_tx_discard = stats.get_vds_interface_tx_discard();
    ds.ds_interface_drop = stats.get_vds_interface_drop();
    ds.ds_duplicated = stats.get_vds_duplicated();
    ds.ds_push = stats.get_vds_push();
    ds.ds_ttl_exceeded = stats.get_vds_ttl_exceeded();
    ds.ds_invalid_nh = stats.get_vds_invalid_nh();
    ds.ds_invalid_label = stats.get_vds_invalid_label();
    ds.ds_invalid_protocol = stats.get_vds_invalid_protocol();
    ds.ds_interface_rx_discard = stats.get_vds_interface_rx_discard();
    ds.ds_invalid_mcast_source = stats.get_vds_invalid_mcast_source();
    ds.ds_head_alloc_fail = stats.get_vds_head_alloc_fail();
    ds.ds_head_space_reserve_fail = stats.get_vds_head_space_reserve_fail();
    ds.ds_pcow_fail = stats.get_vds_pcow_fail();
    ds.ds_flood = stats.get_vds_flood();
    ds.ds_mcast_clone_fail = stats.get_vds_mcast_clone_fail();
    ds.ds_composite_invalid_interface = stats.get_vds_composite_invalid_interface();
    ds.ds_rewrite_fail = stats.get_vds_rewrite_fail();
    ds.ds_misc = stats.get_vds_misc();
    ds.ds_invalid_packet = stats.get_vds_invalid_packet();
    ds.ds_cksum_err = stats.get_vds_cksum_err();
}

void UveClient::NewFlow(const FlowEntry *flow) {
    uint8_t proto = 0;
    uint16_t sport = 0;
    uint16_t dport = 0;

    // Update vrouter port bitmap
    flow->GetPort(proto, sport, dport);
    port_bitmap_.AddPort(proto, sport, dport);

    // Update source-vn port bitmap
    LastVnUveSet::iterator vn_it = last_vn_uve_set_.find(flow->data.source_vn);
    if (vn_it != last_vn_uve_set_.end()) {
        vn_it->second.port_bitmap.AddPort(proto, sport, dport);
    }

    // Update dest-vn port bitmap
    vn_it = last_vn_uve_set_.find(flow->data.dest_vn);
    if (vn_it != last_vn_uve_set_.end()) {
        vn_it->second.port_bitmap.AddPort(proto, sport, dport);
    }

    const Interface *intf = flow->data.intf_entry.get();
    if (intf == NULL) {
        return;
    }

    if (intf->GetType() != Interface::VMPORT) {
        return;
    }

    const VmPortInterface *port = static_cast<const VmPortInterface *>(intf);
    const VmEntry *vm = port->GetVmEntry();
    if (vm == NULL) {
        return;
    }

    // Update VM port bitmap
    LastVmUveSet::iterator vm_it = last_vm_uve_set_.find(vm->GetCfgName());
    if (vm_it != last_vm_uve_set_.end()) {
        vm_it->second.port_bitmap.AddPort(proto, sport, dport);
    }

    // Update Intf port bitmap in VM
    for (VmIntfMap::iterator it = vm_intf_map_.find(vm);
         it != vm_intf_map_.end(); it++) {
        if (vm == it->first && intf == it->second.intf) {
            it->second.port_bitmap.AddPort(proto, sport, dport);
            break;
        }

        if (vm != it->first) {
            break;
        }
    }
}

void UveClient::BuildXmppStatsList(vector<AgentXmppStats> &list) {
    for (int count = 0; count < MAX_XMPP_SERVERS; count++) {
        AgentXmppStats peer;
        if (!Agent::GetInstance()->GetXmppServer(count).empty()) {
            AgentXmppChannel *ch = Agent::GetInstance()->GetAgentXmppChannel(count);
            if (ch == NULL) {
                continue;
            }
            XmppChannel *xc = ch->GetXmppChannel();
            if (xc == NULL) {
                continue;
            }
            peer.set_ip(Agent::GetInstance()->GetXmppServer(count));
            peer.set_reconnects(AgentStats::GetInstance()->GetXmppReconnect(count));
            peer.set_in_msgs(AgentStats::GetInstance()->GetXmppInMsgs(count));
            peer.set_out_msgs(AgentStats::GetInstance()->GetXmppOutMsgs(count));
            list.push_back(peer);
        }
    }
}

void UveClient::DeleteFlow(const FlowEntry *flow) {
    /* We need not reset bitmaps on flow deletion. We will have to 
     * provide introspect to reset this */
}

bool UveClient::SendAgentStats() {
    static bool first = true;
    bool change = false;
    static uint8_t count = 0;
    static uint8_t cpu_stats_count = 0;
    VrouterStatsAgent stats;

    SendVrouterUve();

    stats.set_name(Agent::GetInstance()->GetHostName());

    if (prev_stats_.get_in_tpkts() != 
            AgentStats::GetInstance()->GetInPkts() || first) {
        stats.set_in_tpkts(AgentStats::GetInstance()->GetInPkts());
        prev_stats_.set_in_tpkts(AgentStats::GetInstance()->GetInPkts());
        change = true;
    }

    if (prev_stats_.get_in_bytes() != 
            AgentStats::GetInstance()->GetInBytes() || first) {
        stats.set_in_bytes(AgentStats::GetInstance()->GetInBytes());
        prev_stats_.set_in_bytes(AgentStats::GetInstance()->GetInBytes());
        change = true;
    }

    if (prev_stats_.get_out_tpkts() != 
            AgentStats::GetInstance()->GetOutPkts() || first) {
        stats.set_out_tpkts(AgentStats::GetInstance()->GetOutPkts());
        prev_stats_.set_out_tpkts(AgentStats::GetInstance()->GetOutPkts());
        change = true;
    }

    if (prev_stats_.get_out_bytes() != 
            AgentStats::GetInstance()->GetOutBytes() || first) {
        stats.set_out_bytes(AgentStats::GetInstance()->GetOutBytes());
        prev_stats_.set_out_bytes(AgentStats::GetInstance()->GetOutBytes());
        change = true;
    }

    vector<AgentXmppStats> xmpp_list;
    BuildXmppStatsList(xmpp_list);
    if (prev_stats_.get_xmpp_stats_list() != xmpp_list) {
        stats.set_xmpp_stats_list(xmpp_list);
        prev_stats_.set_xmpp_stats_list(xmpp_list);
        change = true;
    }

    //stats.set_collector_reconnects(AgentStats::GetInstance()->GetSandeshReconnects());
    //stats.set_in_sandesh_msgs(AgentStats::GetInstance()->GetSandeshInMsgs());
    //stats.set_out_sandesh_msgs(AgentStats::GetInstance()->GetSandeshOutMsgs());
    //stats.set_sandesh_http_sessions(AgentStats::GetInstance()->GetSandeshHttpSessions());

    if (prev_stats_.get_exception_packets() != 
            AgentStats::GetInstance()->GetPktExceptions() || first) {
        stats.set_exception_packets(AgentStats::GetInstance()->GetPktExceptions());
        prev_stats_.set_exception_packets(AgentStats::GetInstance()->GetPktExceptions());
        change = true;
    }

    if (prev_stats_.get_exception_packets_dropped() != 
            AgentStats::GetInstance()->GetPktDropped() || first) {
        stats.set_exception_packets_dropped(AgentStats::GetInstance()->GetPktDropped());
        prev_stats_.set_exception_packets_dropped(AgentStats::GetInstance()->GetPktDropped());
        change = true;
    }

    //stats.set_exception_packets_denied();
    uint64_t e_pkts_allowed = (AgentStats::GetInstance()->GetPktExceptions() -
                     AgentStats::GetInstance()->GetPktDropped());
    if (prev_stats_.get_exception_packets_allowed() != e_pkts_allowed) {
        stats.set_exception_packets_allowed(e_pkts_allowed);
        prev_stats_.set_exception_packets_allowed(e_pkts_allowed);
        change = true;
    }

    if (prev_stats_.get_total_flows() != 
            AgentStats::GetInstance()->GetFlowCreated() || first) {
        stats.set_total_flows(AgentStats::GetInstance()->GetFlowCreated());
        prev_stats_.set_total_flows(AgentStats::GetInstance()->GetFlowCreated());
        change = true;
    }

    if (prev_stats_.get_active_flows() != 
            AgentStats::GetInstance()->GetFlowActive() || first) {
        stats.set_active_flows(AgentStats::GetInstance()->GetFlowActive());
        prev_stats_.set_active_flows(AgentStats::GetInstance()->GetFlowActive());
        change = true;
    }

    if (prev_stats_.get_aged_flows() != 
            AgentStats::GetInstance()->GetFlowAged() || first) {
        stats.set_aged_flows(AgentStats::GetInstance()->GetFlowAged());
        prev_stats_.set_aged_flows(AgentStats::GetInstance()->GetFlowAged());
        change = true;
    }
    //stats.set_aged_flows_dp();
    //stats.active_flows_dp();

    cpu_stats_count++;
    if ((cpu_stats_count % 6) == 0) {
        static bool cpu_first = true; //to track whether cpu-info is being sent for first time
        CpuLoadInfo cpu_load_info;
        CpuLoadData::FillCpuInfo(cpu_load_info, true);
        if (prev_stats_.get_cpu_info() != cpu_load_info || cpu_first) {
            stats.set_cpu_info(cpu_load_info);
            if ((prev_stats_.get_cpu_info().get_cpu_share() != 
                cpu_load_info.get_cpu_share()) || cpu_first) {
                stats.set_cpu_share(cpu_load_info.get_cpu_share());
            }
            if ((prev_stats_.get_cpu_info().get_meminfo().get_virt() != 
                cpu_load_info.get_meminfo().get_virt()) || cpu_first) {
                stats.set_virt_mem(cpu_load_info.get_meminfo().get_virt());
            }
            if ((prev_stats_.get_cpu_info().get_sys_mem_info().get_used() != 
                cpu_load_info.get_sys_mem_info().get_used()) || cpu_first) {
                stats.set_used_sys_mem(cpu_load_info.get_sys_mem_info().get_used());
            }
            if ((prev_stats_.get_cpu_info().get_cpuload().get_one_min_avg() != 
                cpu_load_info.get_cpuload().get_one_min_avg()) || cpu_first) {
                stats.set_one_min_avg_cpuload(
                        cpu_load_info.get_cpuload().get_one_min_avg());
            }
            prev_stats_.set_cpu_info(cpu_load_info);
            change = true;
            cpu_first = false;
        }
        cpu_stats_count = 0;
    }
    vector<AgentIfStats> phy_if_list;
    BuildPhyIfList(phy_if_list);
    if (prev_stats_.get_phy_if_stats_list() != phy_if_list) {
        stats.set_phy_if_stats_list(phy_if_list);
        prev_stats_.set_phy_if_stats_list(phy_if_list);
        change = true;
    }
    count++;
    if (first) {
        InitPrevStats();
        //First time bandwidth is sent at 1.1, 5.1 and 10.1 minutes
        count = 0;
    }
    // 1 minute bandwidth
    if ((count % 6) == 0) {
        vector<AgentIfBandwidth> phy_if_blist;
        BuildPhyIfBand(phy_if_blist, 1);
        if (prev_stats_.get_phy_if_1min_usage() != phy_if_blist) {
            stats.set_phy_if_1min_usage(phy_if_blist);
            prev_stats_.set_phy_if_1min_usage(phy_if_blist);
            change = true;

            vector<AgentIfBandwidth>::iterator it = phy_if_blist.begin();
            int num_intfs = 0, in_band = 0, out_band = 0;
            while(it != phy_if_blist.end()) {
                AgentIfBandwidth band = *it;
                in_band += band.get_in_bandwidth_usage();
                out_band += band.get_out_bandwidth_usage();
                num_intfs++;
                ++it;
            }
            stats.set_total_in_bandwidth_utilization((in_band/num_intfs));
            stats.set_total_out_bandwidth_utilization((out_band/num_intfs));
        }
    }

    // 5 minute bandwidth
    if ((count % 30) == 0) {
        vector<AgentIfBandwidth> phy_if_blist;
        BuildPhyIfBand(phy_if_blist, 5);
        if (prev_stats_.get_phy_if_5min_usage() != phy_if_blist) {
            stats.set_phy_if_5min_usage(phy_if_blist);
            prev_stats_.set_phy_if_5min_usage(phy_if_blist);
            change = true;
        }
    }

    // 10 minute bandwidth
    if ((count % 60) == 0) {
        vector<AgentIfBandwidth> phy_if_blist;
        BuildPhyIfBand(phy_if_blist, 10);
        if (prev_stats_.get_phy_if_10min_usage() != phy_if_blist) {
            stats.set_phy_if_10min_usage(phy_if_blist);
            prev_stats_.set_phy_if_10min_usage(phy_if_blist);
            change = true;
        }
        //The following avoids handling of count overflow cases.
        count = 0;
    }
    VirtualHostInterfaceKey key(nil_uuid(), Agent::GetInstance()->GetVirtualHostInterfaceName());
    const Interface *vhost = static_cast<const Interface *>(Agent::GetInstance()->GetInterfaceTable()->FindActiveEntry(&key));
    const AgentStatsCollector::IfStats *s = 
        AgentUve::GetInstance()->GetStatsCollector()->GetIfStats(vhost);
    if (s != NULL) {
        AgentIfStats vhost_stats;
        vhost_stats.set_name(Agent::GetInstance()->GetVirtualHostInterfaceName());
        vhost_stats.set_in_pkts(s->in_pkts);
        vhost_stats.set_in_bytes(s->in_bytes);
        vhost_stats.set_out_pkts(s->out_pkts);
        vhost_stats.set_out_bytes(s->out_bytes);
        vhost_stats.set_speed(s->speed);
        vhost_stats.set_duplexity(s->duplexity);
        if (prev_stats_.get_vhost_stats() != vhost_stats) {
            stats.set_vhost_stats(vhost_stats);
            prev_stats_.set_vhost_stats(vhost_stats);
            change = true;
        }
    }

    if (SetVRouterPortBitmap(stats)) {
        change = true;
    }

    AgentDropStats drop_stats;
    FetchDropStats(drop_stats);
    stats.set_drop_stats(drop_stats);
    if (first) {
        stats.set_uptime(start_time_);
    }

    if (change) {
        VrouterStats::Send(stats);
    }
    first = false;
    agent_stats_timer_->Cancel();
    agent_stats_timer_->Start(10*1000, 
                              boost::bind(&UveClient::AgentStatsTimer, this),
                              NULL);

    return true;
}

bool UveClient::AgentStatsTimer() {
    agent_stats_trigger_->Set();
    return false;
}

void UveClient::AddLastVnUve(string vn_name) {
    UveVnEntry uve;
    uve.uve_info.set_name(vn_name);
    last_vn_uve_set_.insert(LastVnUvePair(vn_name, uve));
}

void HandleSigChild(const boost::system::error_code& error,
        int signal_number) {
    if (!error) {
        int status;
        while (::waitpid(-1, &status, WNOHANG) > 0);
        UveClient::GetInstance()->RegisterSigHandler();
    }
}

void UveClient::RegisterSigHandler() {
    signal_.async_wait(HandleSigChild);
}

void UveClient::InitSigHandler() {
    boost::system::error_code ec;
    signal_.add(SIGCHLD, ec);
    if (ec) {
        LOG(ERROR, "SIGCHLD registration failed");
    }
    RegisterSigHandler();
}

uint32_t UveClient::GetCpuCount() {
    return prev_stats_.get_cpu_info().get_num_cpu();
}

bool UveClient::Process(VmStatData *vm_stat_data) {
    VmStat::ProcessData(vm_stat_data->GetVmStat(), vm_stat_data->GetVmStatCb());
    delete vm_stat_data;
    return true;
}

void UveClient::EnqueueVmStatData(VmStatData *data) {
    singleton_->event_queue_->Enqueue(data);
}

void UveClient::Init(uint64_t bandwidth_intvl) {
    singleton_ = new UveClient(bandwidth_intvl);

    VmTable *vm_table = Agent::GetInstance()->GetVmTable();
    singleton_->vm_listener_id_ = vm_table->Register
        (boost::bind(&UveClient::VmNotify, singleton_, _1, _2));

    VnTable *vn_table = Agent::GetInstance()->GetVnTable();
    singleton_->vn_listener_id_ = vn_table->Register
        (boost::bind(&UveClient::VnNotify, singleton_, _1, _2));

    InterfaceTable *intf_table = Agent::GetInstance()->GetInterfaceTable();
    singleton_->intf_listener_id_ = intf_table->Register
        (boost::bind(&UveClient::IntfNotify, singleton_, _1, _2));

    VrfTable *vrf_table = Agent::GetInstance()->GetVrfTable();
    singleton_->vrf_listener_id_ = vrf_table->Register
        (boost::bind(&UveClient::VrfNotify, singleton_, _1, _2));

    CpuLoadData::Init();

    singleton_->agent_stats_trigger_ = 
        new TaskTrigger(boost::bind(&UveClient::SendAgentStats, singleton_),
                TaskScheduler::GetInstance()->GetTaskId("Agent::Uve"), 0);

    singleton_->agent_stats_timer_ = 
        TimerManager::CreateTimer(*Agent::GetInstance()->GetEventManager()->io_service(),
                                 "Agent stats collector timer");
    singleton_->agent_stats_timer_->Start(10*1000,
                             boost::bind(&UveClient::AgentStatsTimer, singleton_), 
                             NULL);
    singleton_->InitSigHandler();
}

UveClient::~UveClient() {
    delete event_queue_;
    singleton_->agent_stats_timer_->Cancel();
    singleton_->agent_stats_trigger_->Reset();
    boost::system::error_code ec;
    singleton_->signal_.cancel(ec);
    delete singleton_->agent_stats_trigger_;
}

void UveClient::Shutdown(void) {
    Agent::GetInstance()->GetVmTable()->Unregister(singleton_->vm_listener_id_);
    Agent::GetInstance()->GetVnTable()->Unregister(singleton_->vn_listener_id_);
    Agent::GetInstance()->GetInterfaceTable()->Unregister(singleton_->intf_listener_id_);
    delete singleton_;
    singleton_ = NULL;
}

VmStat::VmStat(const uuid &vm_uuid):
    vm_uuid_(vm_uuid), mem_usage_(0), virt_memory_(0), virt_memory_peak_(0),
    vm_memory_quota_(0), prev_cpu_stat_(0), cpu_usage_(0), prev_cpu_snapshot_time_(0), 
    prev_vcpu_snapshot_time_(0), input_(*(Agent::GetInstance()->GetEventManager()->io_service())),
    timer_(TimerManager::CreateTimer(*(Agent::GetInstance()->GetEventManager())->io_service(),
    "VmStatTimer")), marked_delete_(false), pid_(0), retry_(0) {
}

VmStat::~VmStat() {
    TimerManager::DeleteTimer(timer_);
}

void VmStat::ReadData(const boost::system::error_code &ec,
                      size_t read_bytes, DoneCb &cb) {
    if (read_bytes) {
        data_<< rx_buff_;
    } 
    
    if (ec) {
        boost::system::error_code close_ec;
        input_.close(close_ec);
        //Enqueue a request to process data
        VmStatData *vm_stat_data = new VmStatData(this, cb);
        UveClient::GetInstance()->EnqueueVmStatData(vm_stat_data);
    } else {
        bzero(rx_buff_, sizeof(rx_buff_));
        async_read(input_, boost::asio::buffer(rx_buff_, kBufLen),
                   boost::bind(&VmStat::ReadData, this, placeholders::error,
                   placeholders::bytes_transferred, cb));
    }
}

void VmStat::ProcessData(VmStat *vm_stat, DoneCb &cb) {
    if (vm_stat->marked_delete_) {
        delete vm_stat;
    } else {
        cb();
    }
}

void VmStat::ExecCmd(std::string cmd, DoneCb cb) {
    char *argv[4];
    char shell[80] = "/bin/sh";
    char option[80] = "-c";
    char ccmd[80];
    strncpy(ccmd, cmd.c_str(), 80);

    argv[0] = shell;
    argv[1] = option;
    argv[2] = ccmd;
    argv[3] = 0;

    int out[2];
    if (pipe(out) < 0) {
        return;
    }

    if (vfork() == 0) {
        //Close read end of pipe
        close(out[0]);
        dup2(out[1], STDOUT_FILENO);
        //Close out[1] as stdout is a exact replica of out[1]
        close(out[1]);
        execvp(argv[0], argv);
        perror("execvp");
        exit(127);
    }

    //Close write end of pipe
    close(out[1]);

    boost::system::error_code ec;
    input_.assign(::dup(out[0]), ec);
    close(out[0]);
    if (ec) {
        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    async_read(input_, boost::asio::buffer(rx_buff_, kBufLen),
               boost::bind(&VmStat::ReadData, this, placeholders::error,
                           placeholders::bytes_transferred, cb));
}

void VmStat::ReadCpuStat() {
    std::string tmp;
    //Typical output from command
    //Total:
    //    cpu_time         16754.635764199 seconds

    //Get Total cpu stats
    double cpu_stat = 0;
    while (data_ >> tmp) {
        if (tmp == "cpu_time") {
            data_ >> cpu_stat;
            break;
        }
    }

    uint32_t num_of_cpu = UveClient::GetInstance()->GetCpuCount();
    if (num_of_cpu == 0) {
        GetVcpuStat();
        return;
    }

    time_t now;
    time(&now);
    if (prev_cpu_snapshot_time_) {
        cpu_usage_ = (cpu_stat - prev_cpu_stat_)/difftime(now, prev_cpu_snapshot_time_);
        cpu_usage_ *= 100;
        cpu_usage_ /= num_of_cpu;
    }

    prev_cpu_stat_ = cpu_stat;
    prev_cpu_snapshot_time_ = now;

    //Clear buffer
    data_.str(" ");
    data_.clear();

    //Trigger a request to start vcpu stat collection
    GetVcpuStat();
}

void VmStat::ReadVcpuStat() {
    std::string tmp;
    uint32_t index = 0;
    std::vector<double> vcpu_usage;
    //Read latest VCPU usage time
    while(data_ >> tmp) {
        if (tmp == "VCPU:") {
            //Current VCPU index
            data_ >> index;
        }

        if (tmp == "time:") {
            double usage = 0;
            data_ >> usage;
            vcpu_usage.push_back(usage);
        }
    }

    vcpu_usage_percent_.clear();
    if (prev_vcpu_usage_.size() != vcpu_usage.size()) {
        //In case a new VCPU get added
        prev_vcpu_usage_ = vcpu_usage;
    }

    time_t now;
    time(&now);
    //Calculate VCPU usage
    if (prev_vcpu_snapshot_time_) {
        for (uint32_t i = 0; i < vcpu_usage.size(); i++) {
            double cpu_usage = (vcpu_usage[i] - prev_vcpu_usage_[i])/
                               difftime(now, prev_vcpu_snapshot_time_);
            cpu_usage *= 100;
            vcpu_usage_percent_.push_back(cpu_usage);
        }
    }

    prev_vcpu_usage_ = vcpu_usage;
    prev_vcpu_snapshot_time_ = now;

    data_.str(" ");
    data_.clear();
    //Trigger a request to start mem stat
    GetMemStat();
}

void VmStat::ReadMemStat() {
    if (pid_) {
        std::ostringstream proc_file;
        proc_file << "/proc/"<<pid_<<"/status";
        std::ifstream file(proc_file.str().c_str());

        bool vmsize = false;
        bool peak = false;
        bool rss = false;
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("VmSize") != std::string::npos) {
                std::stringstream vm(line);
                std::string tmp; vm >> tmp; vm >> virt_memory_;
                vmsize = true;
            }
            if (line.find("VmRSS") != std::string::npos) {
                std::stringstream vm(line);                                                                                                                         
                std::string tmp; vm >> tmp; vm >> mem_usage_;                                                                                                         
                rss = true;                                                                                                                                         
            }                                                                                                                                                       
            if (line.find("VmPeak") != std::string::npos) {                                                                                                         
                std::stringstream vm(line);                                                                                                                         
                std::string tmp; vm >> tmp; vm >> virt_memory_peak_;                                                                                                    
                peak = true;                                                                                                                                        
            }                                                                                                                                                       
            if (rss && vmsize && peak)                                                                                                                              
                break;                                                                                                                                              
        }          
    }    

    data_.str(" ");
    data_.clear();
    //Send Stats
    UveVirtualMachineAgent vm_agent;
    vm_agent.set_name(UuidToString(vm_uuid_));
    
    UveVirtualMachineStats stats;
    stats.set_cpu_one_min_avg(cpu_usage_);
    stats.set_vcpu_one_min_avg(vcpu_usage_percent_);
    stats.set_vm_memory_quota(vm_memory_quota_);
    stats.set_rss(mem_usage_);
    stats.set_virt_memory(virt_memory_);
    stats.set_peak_virt_memory(virt_memory_peak_);   

    if (stats != prev_stats_){
        vm_agent.set_vm_stats(stats);
        UveVirtualMachineAgentTrace::Send(vm_agent);
        prev_stats_ = stats;
    }
    StartTimer();    
}

void VmStat::GetCpuStat() {
    std::ostringstream cmd;
    cmd << "virsh cpu-stats " << Agent::GetInstance()->GetUuidStr(vm_uuid_) << " --total";
    ExecCmd(cmd.str(), boost::bind(&VmStat::ReadCpuStat, this));
}

void VmStat::GetVcpuStat() {
    std::ostringstream cmd;
    cmd << "virsh vcpuinfo " << Agent::GetInstance()->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStat::ReadVcpuStat, this));
}

void VmStat::GetMemStat() {
    ReadMemStat();
}

void VmStat::ReadMemoryQuota() {
    std::string tmp;
    while (data_ >> tmp) {
        if (tmp == "actual") {
            data_ >> vm_memory_quota_;
        }
    }
    GetCpuStat();
}

void VmStat::GetMemoryQuota() {
    std::ostringstream cmd;
    cmd << "virsh dommemstat " << Agent::GetInstance()->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStat::ReadMemoryQuota, this));
}

bool VmStat::TimerExpiry() {
    if (pid_ == 0) {
        GetPid();
    } else {
        //Get CPU and memory stats
        GetCpuStat();
    }
    return false;
}

void VmStat::StartTimer() {
    timer_->Cancel();
    timer_->Start(kTimeout, boost::bind(&VmStat::TimerExpiry, this));
}

void VmStat::ReadPid() {
    std::string tmp;
    uint32_t pid;

    while (data_) {
        data_ >> pid;
        data_ >> tmp;
        if (tmp.find("qemu") != std::string::npos) {
            //Copy PID 
            pid_ = pid;
            break;
        }
        //Flush out this line
        data_.ignore(512, '\n');
    }

    data_.str(" ");
    data_.clear();      
    if (pid_) {
        //Successfully read pid of process, collect other data
        GetMemoryQuota();
    } else {
        retry_++;
        //Retry after timeout
        if (retry_ < kRetryCount) {
            StartTimer();
        }
    }
}

void VmStat::GetPid() {
    std::ostringstream cmd;
    cmd << "ps -eo pid,cmd | grep " << Agent::GetInstance()->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStat::ReadPid, this));
}

void VmStat::Start() {
    GetPid();
}

void VmStat::Stop(VmStat *vm_stat) {
    vm_stat->marked_delete_ = true;
    if (vm_stat->timer_->running() || vm_stat->retry_ == kRetryCount) {
        //If timer is fired, then we are in middle of 
        //vm stat collection, in such case dont delete the vm stat
        //entry as asio may be using it
        delete vm_stat;
    }
}
