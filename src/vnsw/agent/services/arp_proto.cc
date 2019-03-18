/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "base/address_util.h"
#include "init/agent_init.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/mirror_table.h"
#include "oper/route_common.h"
#include "pkt/pkt_init.h"
#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services_init.h"

ArpProto::ArpProto(Agent *agent, boost::asio::io_service &io,
                   bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::ARP, io),
    run_with_vrouter_(run_with_vrouter), ip_fabric_interface_index_(-1),
    ip_fabric_interface_(NULL), max_retries_(kMaxRetries),
    retry_timeout_(kRetryTimeout), aging_timeout_(kAgingTimeout) {
    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent->params()->services_queue_limit());
    work_queue_.SetBounded(true);

    vrf_table_listener_id_ = agent->vrf_table()->Register(
                             boost::bind(&ArpProto::VrfNotify, this, _1, _2));
    interface_table_listener_id_ = agent->interface_table()->Register(
                                   boost::bind(&ArpProto::InterfaceNotify,
                                               this, _2));
    nexthop_table_listener_id_ = agent->nexthop_table()->Register(
                                 boost::bind(&ArpProto::NextHopNotify, this, _2));
}

ArpProto::~ArpProto() {
}

void ArpProto::Shutdown() {
    // we may have arp entries in arp cache without ArpNH, empty them
    for (ArpIterator it = arp_cache_.begin(); it != arp_cache_.end(); ) {
        it = DeleteArpEntry(it);
    }

    for (GratuitousArpIterator it = gratuitous_arp_cache_.begin();
            it != gratuitous_arp_cache_.end(); it++) {
        for (ArpEntrySet::iterator sit = it->second.begin();
             sit != it->second.end();) {
            ArpEntry *entry = *sit;
            it->second.erase(sit++);
            delete entry;
        }
    }
    gratuitous_arp_cache_.clear();
    agent_->vrf_table()->Unregister(vrf_table_listener_id_);
    agent_->interface_table()->Unregister(interface_table_listener_id_);
    agent_->nexthop_table()->Unregister(nexthop_table_listener_id_);
}

ProtoHandler *ArpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new ArpHandler(agent(), info, io);
}

void ArpProto::VrfNotify(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    ArpVrfState *state;

    state = static_cast<ArpVrfState *>(entry->GetState(part->parent(),
                                                   vrf_table_listener_id_));
    if (entry->IsDeleted()) {
        if (state) {
            for (ArpProto::ArpIterator it = arp_cache_.begin();
                 it != arp_cache_.end();) {
                ArpEntry *arp_entry = it->second;
                if (arp_entry->key().vrf == vrf && arp_entry->DeleteArpRoute()) {
                    it = DeleteArpEntry(it);
                } else
                    it++;
            }
            for (GratuitousArpIterator it = gratuitous_arp_cache_.begin();
                 it != gratuitous_arp_cache_.end(); ) {
                 ArpKey key = it->first;
                 if (key.vrf == vrf) {
                     for (ArpEntrySet::iterator sit = it->second.begin();
                          sit != it->second.end();) {
                         ArpEntry *entry = *sit;
                         it->second.erase(sit++);
                         delete entry;
                     }
                    gratuitous_arp_cache_.erase(it++);
                 }else {
                    it++;
                 }
            }
            state->Delete();
        }
        return;
    }

    if (!state){
        state = new ArpVrfState(agent_, this, vrf,
                                vrf->GetInet4UnicastRouteTable(),
                                vrf->GetEvpnRouteTable());
        state->route_table_listener_id = vrf->
            GetInet4UnicastRouteTable()->
            Register(boost::bind(&ArpVrfState::RouteUpdate, state,  _1, _2));
        state->evpn_route_table_listener_id = vrf->GetEvpnRouteTable()->
            Register(boost::bind(&ArpVrfState::EvpnRouteUpdate, state,  _1, _2));
        entry->SetState(part->parent(), vrf_table_listener_id_, state);
    }
}

void intrusive_ptr_add_ref(ArpPathPreferenceState *aps) {
    aps->refcount_.fetch_and_increment();
}

void intrusive_ptr_release(ArpPathPreferenceState *aps) {
    ArpVrfState *state = aps->vrf_state();
    int prev = aps->refcount_.fetch_and_decrement();
    if (prev == 1) {
        state->Erase(aps->ip(), aps->plen());
        delete aps;
    }
}

ArpPathPreferenceState::ArpPathPreferenceState(ArpVrfState *state,
                                               uint32_t vrf_id,
                                               const IpAddress &ip,
                                               uint8_t plen):
    vrf_state_(state), arp_req_timer_(NULL), vrf_id_(vrf_id),
    vm_ip_(ip), plen_(plen) {
    refcount_ = 0;
}

ArpPathPreferenceState::~ArpPathPreferenceState() {
    if (arp_req_timer_) {
        arp_req_timer_->Cancel();
        TimerManager::DeleteTimer(arp_req_timer_);
    }
    assert(refcount_ == 0);
}

void ArpPathPreferenceState::StartTimer() {
    if (arp_req_timer_ == NULL) {
        arp_req_timer_ = TimerManager::CreateTimer(
                *(vrf_state_->agent->event_manager()->io_service()),
                "Arp Entry timer for VM",
                TaskScheduler::GetInstance()->
                GetTaskId("Agent::Services"), PktHandler::ARP);
    }
    arp_req_timer_->Start(kTimeout,
                          boost::bind(&ArpPathPreferenceState::SendArpRequest,
                                      this));
}

bool ArpPathPreferenceState::SendArpRequest(WaitForTrafficIntfMap
                                            &wait_for_traffic_map,
                                            ArpTransmittedIntfMap
                                            &arp_transmitted_map) {
    bool ret = false;
    boost::shared_ptr<PktInfo> pkt(new PktInfo(vrf_state_->agent,
                                               ARP_TX_BUFF_LEN,
                                               PktHandler::ARP, 0));
    ArpHandler arp_handler(vrf_state_->agent, pkt,
            *(vrf_state_->agent->event_manager()->io_service()));

    WaitForTrafficIntfMap::iterator it = wait_for_traffic_map.begin();
    for (;it != wait_for_traffic_map.end(); it++) {
        const VmInterface *vm_intf = static_cast<const VmInterface *>(
                vrf_state_->agent->interface_table()->FindInterface(it->first));
        if (!vm_intf) {
            continue;
        }

        InterfaceArpPathPreferenceInfo &data = it->second;
        bool inserted = arp_transmitted_map.insert(it->first).second;
        ++data.arp_retry_count;
        if (inserted == false) {
            //ARP request already sent due to IP route
            continue;
        }
        if ((plen_ != 32) && (!data.prev_responded_ip.is_unspecified())) {
            if ((data.arp_send_count >= 1) && (data.arp_reply_count == 0)) {
                ++data.arp_failure_count;
            }
            if (data.arp_failure_count >= ArpProto::kMaxFailures) {
                data.prev_responded_ip = Ip4Address(0);
                data.arp_failure_count = 0;
                data.arp_reply_count = 0;
                data.arp_send_count = 0;
            }
        }
        ++data.arp_send_count;
        MacAddress smac = vm_intf->GetVifMac(vrf_state_->agent);
        arp_handler.SendArpRequestByPlen(vm_intf, smac, this,
                                         data.prev_responded_ip);

        // reduce the frequency of ARP requests after some tries
        if (data.arp_send_count >= kMaxRetry) {
            // change frequency only if not in gateway mode with remote VMIs
            if (vm_intf->vmi_type() != VmInterface::REMOTE_VM)
                arp_req_timer_->Reschedule(kTimeout * 5);
        }

        ret = true;
    }
    return ret;
}

bool ArpPathPreferenceState::SendArpRequest() {
    if (l3_wait_for_traffic_map_.size() == 0 &&
        evpn_wait_for_traffic_map_.size() == 0) {
        return false;
    }

    bool ret = false;
    ArpTransmittedIntfMap arp_transmitted_map;
    if (SendArpRequest(l3_wait_for_traffic_map_, arp_transmitted_map)) {
        ret = true;
    }

    if (SendArpRequest(evpn_wait_for_traffic_map_, arp_transmitted_map)) {
        ret = true;
    }

    return ret;
}

//Send ARP request on interface in Active-BackUp mode
//So that preference of route can be incremented if the VM replies to ARP
void ArpPathPreferenceState::SendArpRequestForAllIntf(const
                                                      AgentRoute *route) {
    WaitForTrafficIntfMap new_wait_for_traffic_map;
    WaitForTrafficIntfMap wait_for_traffic_map = evpn_wait_for_traffic_map_;
    if (dynamic_cast<const InetUnicastRouteEntry *>(route)) {
        wait_for_traffic_map = l3_wait_for_traffic_map_;
    }

    for (Route::PathList::const_iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() &&
            path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
            const NextHop *nh = path->ComputeNextHop(vrf_state_->agent);
            if (nh->GetType() != NextHop::INTERFACE) {
                continue;
            }
            if (path->is_health_check_service()) {
                // skip sending ARP request for Health Check Service IP
                continue;
            }

            const InterfaceNH *intf_nh =
                static_cast<const  InterfaceNH *>(nh);
            const Interface *intf =
                static_cast<const Interface *>(intf_nh->GetInterface());
            if (intf->type() != Interface::VM_INTERFACE) {
                //Ignore non vm interface nexthop
                continue;
            }
            if (path->subnet_service_ip().is_v4() == false) {
                continue;
            }
            if (path->path_preference().IsDependentRt() == true) {
                continue;
            }
            uint32_t intf_id = intf->id();
            WaitForTrafficIntfMap::const_iterator wait_for_traffic_it =
                wait_for_traffic_map.find(intf_id);
            if (wait_for_traffic_it == wait_for_traffic_map.end()) {
                InterfaceArpPathPreferenceInfo data;
                new_wait_for_traffic_map.insert(WaitForTrafficIntfPair(intf_id,
                                                                       data));
            } else {
                new_wait_for_traffic_map.insert(WaitForTrafficIntfPair(intf_id,
                    wait_for_traffic_it->second));
            }
        }
    }

    if (dynamic_cast<const InetUnicastRouteEntry *>(route)) {
        l3_wait_for_traffic_map_ = new_wait_for_traffic_map;
    } else {
        evpn_wait_for_traffic_map_ = new_wait_for_traffic_map;
    }
    if (new_wait_for_traffic_map.size() > 0) {
        SendArpRequest();
        StartTimer();
    }
}

ArpDBState::ArpDBState(ArpVrfState *vrf_state, uint32_t vrf_id, IpAddress ip,
                       uint8_t plen) : vrf_state_(vrf_state),
    sg_list_(), tag_list_(), policy_(false), resolve_route_(false) {
    if (ip != Ip4Address(0)) {
       arp_path_preference_state_.reset(vrf_state->Locate(ip, plen));
    }
}

ArpDBState::~ArpDBState() {
}

void ArpDBState::UpdateArpRoutes(const InetUnicastRouteEntry *rt) {
    int plen = rt->plen();
    uint32_t start_ip = rt->addr().to_v4().to_ulong();
    ArpKey start_key(start_ip, rt->vrf());

    ArpProto::ArpIterator start_iter =
        vrf_state_->arp_proto->FindUpperBoundArpEntry(start_key);


    while (start_iter != vrf_state_->arp_proto->arp_cache().end() &&
           start_iter->first.vrf == rt->vrf() &&
           IsIp4SubnetMember(Ip4Address(start_iter->first.ip),
                             rt->addr().to_v4(), plen)) {
        start_iter->second->Resync(policy_, vn_list_, sg_list_, tag_list_);
        start_iter++;
    }
}

void ArpDBState::Delete(const InetUnicastRouteEntry *rt) {
    int plen = rt->plen();
    uint32_t start_ip = rt->addr().to_v4().to_ulong();

    ArpKey start_key(start_ip, rt->vrf());

    ArpProto::ArpIterator start_iter =
        vrf_state_->arp_proto->FindUpperBoundArpEntry(start_key);

    while (start_iter != vrf_state_->arp_proto->arp_cache().end() &&
           start_iter->first.vrf == rt->vrf() &&
           IsIp4SubnetMember(Ip4Address(start_iter->first.ip),
                             rt->addr().to_v4(), plen)) {
        ArpProto::ArpIterator tmp = start_iter++;
        if (tmp->second->DeleteArpRoute()) {
            vrf_state_->arp_proto->DeleteArpEntry(tmp->second);
        }
    }
}

void ArpDBState::Update(const AgentRoute *rt) {
    if (arp_path_preference_state_) {
        arp_path_preference_state_->SendArpRequestForAllIntf(rt);
    }

    const InetUnicastRouteEntry *ip_rt =
        dynamic_cast<const InetUnicastRouteEntry *>(rt);
    if (ip_rt == NULL) {
        return;
    }

    if (ip_rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        resolve_route_ = true;
    }

    bool policy = ip_rt->GetActiveNextHop()->PolicyEnabled();
    const SecurityGroupList sg = ip_rt->GetActivePath()->sg_list();
    const TagList tag = ip_rt->GetActivePath()->tag_list();


    if (policy_ != policy || sg != sg_list_ || tag != tag_list_ ||
        vn_list_ != ip_rt->GetActivePath()->dest_vn_list()) {
        policy_ = policy;
        sg_list_ = sg;
        tag_list_ = tag;
        vn_list_ = ip_rt->GetActivePath()->dest_vn_list();
        if (resolve_route_) {
            UpdateArpRoutes(ip_rt);
        }
    }
}

void ArpVrfState::EvpnRouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    EvpnRouteEntry *route = static_cast<EvpnRouteEntry *>(entry);
    /* Ignore route updates for Non-IPv4 addresses */
    if (!route->ip_addr().is_v4()) {
        return;
    }

    ArpDBState *state = static_cast<ArpDBState *>(entry->GetState(part->parent(),
                                                  evpn_route_table_listener_id));

    if (entry->IsDeleted() || deleted) {
        if (state) {
            entry->ClearState(part->parent(), evpn_route_table_listener_id);
            delete state;
        }
        return;
    }

    if (state == NULL) {
        state = new ArpDBState(this, route->vrf_id(), route->ip_addr(),
                               route->GetVmIpPlen());
        entry->SetState(part->parent(), evpn_route_table_listener_id, state);
    }

    state->Update(route);
}

void ArpVrfState::RouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    InetUnicastRouteEntry *route = static_cast<InetUnicastRouteEntry *>(entry);

    ArpDBState *state =
        static_cast<ArpDBState *>
        (entry->GetState(part->parent(), route_table_listener_id));

    const InterfaceNH *intf_nh = dynamic_cast<const InterfaceNH *>(
            route->GetActiveNextHop());
    const Interface *intf = (intf_nh) ?
        static_cast<const Interface *>(intf_nh->GetInterface()) : NULL;

    ArpKey key(route->addr().to_v4().to_ulong(), route->vrf());
    ArpEntry *arpentry = arp_proto->GratuitousArpEntry(key, intf);
    if (entry->IsDeleted() || deleted) {
        if (state) {
            arp_proto->DeleteGratuitousArpEntry(arpentry);
            entry->ClearState(part->parent(), route_table_listener_id);
            state->Delete(route);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new ArpDBState(this, route->vrf_id(), route->addr(),
                               route->plen());
        entry->SetState(part->parent(), route_table_listener_id, state);
    }

    if (route->vrf()->GetName() == agent->fabric_vrf_name() &&
        route->GetActiveNextHop()->GetType() == NextHop::RECEIVE &&
        arp_proto->agent()->router_id() == route->addr().to_v4()) {
        //Send Grat ARP
        arp_proto->AddGratuitousArpEntry(key);
        arp_proto->SendArpIpc(ArpProto::ARP_SEND_GRATUITOUS,
                              route->addr().to_v4().to_ulong(), route->vrf(),
                              arp_proto->ip_fabric_interface());
    } else {
        if (intf_nh) {
            if (intf->type() == Interface::VM_INTERFACE &&
                static_cast<const VmInterface*>(intf)->IsActive()) {
                ArpKey intf_key(route->addr().to_v4().to_ulong(), route->vrf());
                arp_proto->AddGratuitousArpEntry(intf_key);
                arp_proto->SendArpIpc(ArpProto::ARP_SEND_GRATUITOUS,
                        route->addr().to_v4().to_ulong(), intf->vrf(), intf);
            }
        }
    }

    //Check if there is a local VM path, if yes send a
    //ARP request, to trigger route preference state machine
    if (state && route->vrf()->GetName() != agent->fabric_vrf_name()) {
        state->Update(route);
    }
}

bool ArpVrfState::DeleteRouteState(DBTablePartBase *part, DBEntryBase *entry) {
    RouteUpdate(part, entry);
    return true;
}

bool ArpVrfState::DeleteEvpnRouteState(DBTablePartBase *part,
                                       DBEntryBase *entry) {
    EvpnRouteUpdate(part, entry);
    return true;
}


void ArpVrfState::Delete() {
    if (managed_delete_walk_ref.get() == NULL)
        return;

    rt_table->WalkAgain(managed_delete_walk_ref);
    if (evpn_walk_ref_.get())
        evpn_rt_table->WalkAgain(evpn_walk_ref_);
    deleted = true;
}

void ArpVrfState::WalkDone(DBTableBase *partition, ArpVrfState *state) {
    if (partition == state->rt_table) {
        state->rt_table->ReleaseWalker(state->managed_delete_walk_ref);
        state->managed_delete_walk_ref = NULL;
        state->l3_walk_completed_ = true;
    } else {
        //Get rt table from partition
        state->evpn_rt_table->ReleaseWalker(state->evpn_walk_ref_);
        state->evpn_walk_ref_ = NULL;
        state->evpn_walk_completed_ = true;
    }
    if (state->PreWalkDone(partition)) {
        delete state;
    }
}

bool ArpVrfState::PreWalkDone(DBTableBase *partition) {
    if (arp_proto->ValidateAndClearVrfState(vrf, this) == false) {
        return false;
    }

    rt_table->Unregister(route_table_listener_id);
    table_delete_ref.Reset(NULL);

    evpn_rt_table->Unregister(evpn_route_table_listener_id);
    evpn_table_delete_ref.Reset(NULL);
    return true;
}

ArpPathPreferenceState* ArpVrfState::Locate(const IpAddress &ip, uint8_t plen) {
    ArpPathPreferenceStateKey key(ip, plen);
    ArpPathPreferenceState* ptr = NULL;
    ArpPathPreferenceStateMap::iterator it = arp_path_preference_map_.find(key);
    if (it == arp_path_preference_map_.end()) {
        ptr = new ArpPathPreferenceState(this, vrf->vrf_id(), ip, plen);
        arp_path_preference_map_.insert(ArpPathPreferenceStatePair(key, ptr));
    } else {
        ptr = it->second;
    }
    return ptr;
}

void ArpVrfState::Erase(const IpAddress &ip, uint8_t plen) {
    ArpPathPreferenceStateKey key(ip, plen);
    ArpPathPreferenceStateMap::iterator it = arp_path_preference_map_.find(key);
    if (it != arp_path_preference_map_.end()) {
        arp_path_preference_map_.erase(it);
    }
}


ArpPathPreferenceState* ArpVrfState::Get(const IpAddress ip, uint8_t plen) {
    ArpPathPreferenceStateKey key(ip, plen);
    ArpPathPreferenceStateMap::iterator it = arp_path_preference_map_.find(key);
    if (it != arp_path_preference_map_.end()) {
        return it->second;
    }
    return NULL;
}

ArpVrfState::ArpVrfState(Agent *agent_ptr, ArpProto *proto, VrfEntry *vrf_entry,
                         AgentRouteTable *table, AgentRouteTable *evpn_table):
    agent(agent_ptr), arp_proto(proto), vrf(vrf_entry), rt_table(table),
    evpn_rt_table(evpn_table), route_table_listener_id(DBTableBase::kInvalidId),
    evpn_route_table_listener_id(DBTableBase::kInvalidId),
    table_delete_ref(this, table->deleter()),
    evpn_table_delete_ref(this, evpn_table->deleter()),
    deleted(false),
    l3_walk_completed_(false), evpn_walk_completed_(false) {
    evpn_walk_ref_ = evpn_rt_table->AllocWalker(
            boost::bind(&ArpVrfState::DeleteEvpnRouteState, this, _1, _2),
            boost::bind(&ArpVrfState::WalkDone, _2, this));
    managed_delete_walk_ref = rt_table->AllocWalker(
            boost::bind(&ArpVrfState::DeleteRouteState, this, _1, _2),
            boost::bind(&ArpVrfState::WalkDone, _2, this));
}

ArpVrfState::~ArpVrfState() {
    assert(arp_path_preference_map_.size() == 0);
}

void ArpProto::InterfaceNotify(DBEntryBase *entry) {
    Interface *intf = static_cast<Interface *>(entry);
    ArpInterfaceState *state = static_cast<ArpInterfaceState *>
        (entry->GetState(entry->get_table_partition()->parent(),
                       interface_table_listener_id_));

    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (state) {
            intf->ClearState(intf->get_table_partition()->parent(),
                             interface_table_listener_id_);
            delete state;
        }

        InterfaceArpMap::iterator it = interface_arp_map_.find(itf->id());
        if (it != interface_arp_map_.end()) {
            InterfaceArpInfo &intf_entry = it->second;
            ArpKeySet::iterator key_it = intf_entry.arp_key_list.begin();
            while (key_it != intf_entry.arp_key_list.end()) {
                ArpKey key = *key_it;
                ++key_it;
                ArpIterator arp_it = arp_cache_.find(key);
                if (arp_it != arp_cache_.end()) {
                    ArpEntry *arp_entry = arp_it->second;
                    if (arp_entry->DeleteArpRoute()) {
                        DeleteArpEntry(arp_it);
                    }
                }
            }
            intf_entry.arp_key_list.clear();
            interface_arp_map_.erase(it);
        }

        if (itf->type() == Interface::VM_INTERFACE) {
            ArpKey key(static_cast<VmInterface *>
                       (itf)->primary_ip_addr().to_ulong(), itf->vrf());
            ArpEntry *arpentry = GratuitousArpEntry(key, itf);
            if (arpentry) {
                DeleteGratuitousArpEntry(arpentry);
            }
        }

        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(NULL);
            set_ip_fabric_interface_index(-1);
        }
    } else {
        if (state == NULL) {
            state = new ArpInterfaceState(intf);
            intf->SetState(entry->get_table_partition()->parent(),
                           interface_table_listener_id_, state);
        }

        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(itf);
            set_ip_fabric_interface_index(itf->id());
            if (run_with_vrouter_) {
                set_ip_fabric_interface_mac(itf->mac());
            } else {
                set_ip_fabric_interface_mac(MacAddress());
            }
        }

        if (itf->type() == Interface::VM_INTERFACE) {
            const VmInterface *vm_intf =
                static_cast<const VmInterface *>(itf);
            VrfEntry *forwarding_vrf = NULL;
            if (vm_intf->vrf()) {
                forwarding_vrf = vm_intf->vrf()->forwarding_vrf();
            }
            state->SetVrf(vm_intf->vrf(), forwarding_vrf);
        }
    }
}

ArpProto::InterfaceArpInfo& ArpProto::ArpMapIndexToEntry(uint32_t idx) {
    InterfaceArpMap::iterator it = interface_arp_map_.find(idx);
    if (it == interface_arp_map_.end()) {
        InterfaceArpInfo entry;
        std::pair<InterfaceArpMap::iterator, bool> ret;
        ret = interface_arp_map_.insert(InterfaceArpPair(idx, entry));
        return ret.first->second;
    } else {
        return it->second;
    }
}

void ArpProto::IncrementStatsArpRequest(uint32_t idx) {
    InterfaceArpInfo &entry = ArpMapIndexToEntry(idx);
    entry.stats.arp_req++;
}

void ArpProto::IncrementStatsArpReply(uint32_t idx) {
    InterfaceArpInfo &entry = ArpMapIndexToEntry(idx);
    entry.stats.arp_replies++;
}

void ArpProto::IncrementStatsResolved(uint32_t idx) {
    InterfaceArpInfo &entry = ArpMapIndexToEntry(idx);
    entry.stats.resolved++;
}

uint32_t ArpProto::ArpRequestStatsCounter(uint32_t idx) {
    InterfaceArpInfo &entry = ArpMapIndexToEntry(idx);
    return entry.stats.arp_req;
}

uint32_t ArpProto::ArpReplyStatsCounter(uint32_t idx) {
    InterfaceArpInfo &entry = ArpMapIndexToEntry(idx);
    return entry.stats.arp_replies;
}

uint32_t ArpProto::ArpResolvedStatsCounter(uint32_t idx) {
    InterfaceArpInfo &entry = ArpMapIndexToEntry(idx);
    return entry.stats.resolved;
}

void ArpProto::ClearInterfaceArpStats(uint32_t idx) {
    InterfaceArpInfo &entry = ArpMapIndexToEntry(idx);
    entry.stats.Reset();
}

void ArpProto::NextHopNotify(DBEntryBase *entry) {
    NextHop *nh = static_cast<NextHop *>(entry);

    switch(nh->GetType()) {
    case NextHop::ARP: {
        ArpNH *arp_nh = (static_cast<ArpNH *>(nh));
        if (arp_nh->IsDeleted()) {
            SendArpIpc(ArpProto::ARP_DELETE, arp_nh->GetIp()->to_ulong(),
                       arp_nh->GetVrf(), arp_nh->GetInterface());
        } else if (arp_nh->IsValid() == false && arp_nh->GetInterface()) {
            SendArpIpc(ArpProto::ARP_RESOLVE, arp_nh->GetIp()->to_ulong(),
                       arp_nh->GetVrf(), arp_nh->GetInterface());
        }
        break;
    }

    default:
        break;
    }
}

bool ArpProto::TimerExpiry(ArpKey &key, uint32_t timer_type,
                           const Interface* itf) {
    if (arp_cache_.find(key) != arp_cache_.end() ||
        gratuitous_arp_cache_.find(key) != gratuitous_arp_cache_.end()) {
        if (itf) {
            SendArpIpc((ArpProto::ArpMsgType)timer_type, key, itf);
        }
    }
    return false;
}

 void ArpProto::AddGratuitousArpEntry(ArpKey &key) {
     ArpEntrySet empty_set;
     gratuitous_arp_cache_.insert(GratuitousArpCachePair(key, empty_set));
}

void ArpProto::DeleteGratuitousArpEntry(ArpEntry *entry) {
    if (!entry)
        return ;

    ArpProto::GratuitousArpIterator iter = gratuitous_arp_cache_.find(entry->key());
    if (iter == gratuitous_arp_cache_.end()) {
        return;
    }

    iter->second.erase(entry);
    delete entry;
    if (iter->second.empty()) {
        gratuitous_arp_cache_.erase(iter);
    }
}

ArpEntry *
ArpProto::GratuitousArpEntry(const ArpKey &key, const Interface *intf) {
    ArpProto::GratuitousArpIterator it = gratuitous_arp_cache_.find(key);
    if (it == gratuitous_arp_cache_.end())
        return NULL;

    for (ArpEntrySet::iterator sit = it->second.begin();
         sit != it->second.end(); sit++) {
        ArpEntry *entry = *sit;
        if (entry->get_interface() == intf)
            return *sit;
    }

    return NULL;
}

ArpProto::GratuitousArpIterator
ArpProto::GratuitousArpEntryIterator(const ArpKey &key, bool *key_valid) {
    ArpProto::GratuitousArpIterator it = gratuitous_arp_cache_.find(key);
    if (it == gratuitous_arp_cache_.end())
        return it;
    const VrfEntry *vrf = key.vrf;
    if (!vrf)
        return it;
    const ArpVrfState *state = static_cast<const ArpVrfState *>
                         (vrf->GetState(vrf->get_table_partition()->parent(),
                          vrf_table_listener_id_));
    // If VRF is delete marked, do not add ARP entries to cache
    if (state == NULL || state->deleted == true)
        return it;
    *key_valid = true;
    return it;
}

void ArpProto::SendArpIpc(ArpProto::ArpMsgType type, in_addr_t ip,
                          const VrfEntry *vrf, InterfaceConstRef itf) {
    ArpIpc *ipc = new ArpIpc(type, ip, vrf, itf);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::ARP, ipc);
}

void ArpProto::SendArpIpc(ArpProto::ArpMsgType type, ArpKey &key,
                          InterfaceConstRef itf) {
    ArpIpc *ipc = new ArpIpc(type, key, itf);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::ARP, ipc);
}

bool ArpProto::AddArpEntry(ArpEntry *entry) {
    const VrfEntry *vrf = entry->key().vrf;
    const ArpVrfState *state = static_cast<const ArpVrfState *>
                         (vrf->GetState(vrf->get_table_partition()->parent(),
                          vrf_table_listener_id_));
    // If VRF is delete marked, do not add ARP entries to cache
    if (state == NULL || state->deleted == true)
        return false;

    bool ret = arp_cache_.insert(ArpCachePair(entry->key(), entry)).second;
    uint32_t intf_id = entry->get_interface()->id();
    InterfaceArpMap::iterator it = interface_arp_map_.find(intf_id);
    if (it == interface_arp_map_.end()) {
        InterfaceArpInfo intf_entry;
        intf_entry.arp_key_list.insert(entry->key());
        interface_arp_map_.insert(InterfaceArpPair(intf_id, intf_entry));
    } else {
        InterfaceArpInfo &intf_entry = it->second;
        ArpKeySet::iterator key_it = intf_entry.arp_key_list.find(entry->key());
        if (key_it == intf_entry.arp_key_list.end()) {
            intf_entry.arp_key_list.insert(entry->key());
        }
    }
    return ret;
}

bool ArpProto::DeleteArpEntry(ArpEntry *entry) {
    if (!entry)
        return false;

    ArpProto::ArpIterator iter = arp_cache_.find(entry->key());
    if (iter == arp_cache_.end()) {
        return false;
    }

    DeleteArpEntry(iter);
    return true;
}

ArpProto::ArpIterator
ArpProto::DeleteArpEntry(ArpProto::ArpIterator iter) {
    ArpEntry *entry = iter->second;
    arp_cache_.erase(iter++);
    delete entry;
    return iter;
}

ArpEntry *ArpProto::FindArpEntry(const ArpKey &key) {
    ArpIterator it = arp_cache_.find(key);
    if (it == arp_cache_.end())
        return NULL;
    return it->second;
}

bool ArpProto::ValidateAndClearVrfState(VrfEntry *vrf,
                                        const ArpVrfState *vrf_state) {
    if (!vrf_state->deleted) {
        ARP_TRACE(Trace, "ARP state not cleared - VRF is not delete marked",
                  "", vrf->GetName(), "");
        return false;
    }

    if (vrf_state->l3_walk_completed() == false) {
        return false;
    }

    if (vrf_state->evpn_walk_completed() == false) {
        return false;
    }

    if (vrf_state->managed_delete_walk_ref.get() != NULL ||
        vrf_state->evpn_walk_ref_.get() != NULL) {
        ARP_TRACE(Trace, "ARP state not cleared - Route table walk not complete",
                  "", vrf->GetName(), "");
        return false;
    }

    DBState *state = static_cast<DBState *>
        (vrf->GetState(vrf->get_table_partition()->parent(),
                       vrf_table_listener_id_));
    if (state) {
        vrf->ClearState(vrf->get_table_partition()->parent(),
                        vrf_table_listener_id_);
    }
    return true;
}

ArpProto::ArpIterator
ArpProto::FindUpperBoundArpEntry(const ArpKey &key) {
        return arp_cache_.upper_bound(key);
}

ArpProto::ArpIterator
ArpProto::FindLowerBoundArpEntry(const ArpKey &key) {
        return arp_cache_.lower_bound(key);
}

void ArpPathPreferenceState::HandleArpReply(Ip4Address sip, uint32_t itf) {
    WaitForTrafficIntfMap::iterator it = l3_wait_for_traffic_map_.find(itf);
    if (it == l3_wait_for_traffic_map_.end()) {
        return;
    }
    InterfaceArpPathPreferenceInfo &data = it->second;
    if (data.prev_responded_ip == sip) {
        ++data.arp_reply_count;
        data.arp_failure_count = 0;
    } else {
        data.prev_responded_ip = sip;
        data.arp_send_count = 0;
    }
}

void ArpProto::HandlePathPreferenceArpReply(const VrfEntry *vrf, uint32_t itf,
                                            Ip4Address sip) {
    if (!vrf) {
        return;
    }
    InetUnicastRouteEntry *rt = vrf->GetUcRoute(sip);
    if (!rt || rt->plen() == 32) {
        return;
    }
    ArpVrfState *state = static_cast<ArpVrfState *>
        (vrf->GetState(vrf->get_table_partition()->parent(),
                       vrf_table_listener_id_));
    if (!state) {
        return;
    }
    ArpPathPreferenceState* pstate = state->Get(sip, rt->plen());
    if (!pstate) {
        return;
    }
    pstate->HandleArpReply(sip, itf);
}

ArpInterfaceState::ArpInterfaceState(Interface *intf):
    intf_(intf), vrf_(NULL), fabric_vrf_(NULL), walk_ref_(NULL) {
}

ArpInterfaceState::~ArpInterfaceState() {
    if (walk_ref_.get() && vrf_) {
        vrf_->GetEvpnRouteTable()->ReleaseWalker(walk_ref_);
    }
}

void ArpInterfaceState::SetVrf(VrfEntry *vrf, VrfEntry *fabric_vrf) {
    bool walk = false;

    if (vrf_ != vrf) {
        if (walk_ref_.get() && vrf_) {
            vrf_->GetEvpnRouteTable()->ReleaseWalker(walk_ref_);
        }
        vrf_ = vrf;
    }

    if (fabric_vrf_ != fabric_vrf) {
        fabric_vrf_ = fabric_vrf;
        walk = true;
    }

    if (vrf_ == NULL) {
        return;
    }

    if (walk && walk_ref_.get() == NULL) {
        walk_ref_ = vrf_->GetEvpnRouteTable()->AllocWalker(
                boost::bind(&ArpInterfaceState::WalkNotify, this, _1, _2),
                boost::bind(&ArpInterfaceState::WalkDone, this, _2));
    }

    if (vrf_->vn() && vrf_->vn()->bridging()) {
        if (walk) {
            vrf_->GetEvpnRouteTable()->WalkAgain(walk_ref_);
        }
    }
}

void ArpInterfaceState::WalkDone(DBTableBase *part) {
}

bool ArpInterfaceState::WalkNotify(DBTablePartBase *partition,
                                   DBEntryBase *e) {
    const EvpnRouteEntry *evpn = static_cast<const EvpnRouteEntry *>(e);

    if (evpn->ip_addr().is_v4() == false) {
        return true;
    }

    if (evpn->ip_addr() == Ip4Address(0)) {
        return true;
    }

    const InterfaceNH *nh =
        dynamic_cast<const InterfaceNH *>(evpn->GetActiveNextHop());
    if (nh && nh->GetInterface() == intf_) {
        return true;
    }

    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>(evpn->get_table());
    const VmInterface *vmi = static_cast<const VmInterface *>(intf_.get());
    Agent *agent = table->agent();
    boost::shared_ptr<PktInfo> pkt(new PktInfo(agent, ARP_TX_BUFF_LEN,
                                   PktHandler::ARP, 0));
    ArpHandler arp_handler(agent, pkt, *(agent->event_manager()->io_service()));

    MacAddress smac = vmi->GetVifMac(agent);
    if (vrf_->forwarding_vrf() == NULL) {
        smac = evpn->mac();
    }

    agent->GetArpProto()->IncrementStatsVmGarpReq();
    arp_handler.SendArp(ARPOP_REQUEST, smac, evpn->ip_addr().to_v4().to_ulong(),
                        smac, vmi->vm_mac(), evpn->ip_addr().to_v4().to_ulong(),
                        intf_->id(), intf_->vrf()->vrf_id());
    return true;
}
