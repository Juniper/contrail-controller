/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "net/address_util.h"
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
    ip_fabric_interface_(NULL), gratuitous_arp_entry_(NULL),
    max_retries_(kMaxRetries), retry_timeout_(kRetryTimeout),
    aging_timeout_(kAgingTimeout) {

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
    del_gratuitous_arp_entry();
    // we may have arp entries in arp cache without ArpNH, empty them
    for (ArpIterator it = arp_cache_.begin(); it != arp_cache_.end(); ) {
        it = DeleteArpEntry(it);
    }
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
        state->Erase(aps->ip());
        delete aps;
    }
}

ArpPathPreferenceState::ArpPathPreferenceState(ArpVrfState *state,
                                               uint32_t vrf_id,
                                               const IpAddress &ip,
                                               uint8_t plen):
    vrf_state_(state), arp_req_timer_(NULL), vrf_id_(vrf_id),
    vm_ip_(ip), plen_(plen), gw_ip_(Ip4Address(0)) {
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
        MacAddress smac = vm_intf->GetVifMac(vrf_state_->agent);
        bool inserted = arp_transmitted_map.insert(it->first).second;
        it->second++;
        if (inserted == false) {
            //ARP request already sent due to IP route
            continue;
        }
        arp_handler.SendArp(ARPOP_REQUEST, smac,
                            gw_ip_.to_v4().to_ulong(),
                            MacAddress(), vm_ip_.to_v4().to_ulong(),
                            it->first, vrf_id_);
        vrf_state_->arp_proto->IncrementStatsVmArpReq();

        // reduce the frequency of ARP requests after some tries
        if (it->second >= kMaxRetry) {
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
void ArpPathPreferenceState::SendArpRequestForAllIntf(const AgentRoute *route) {
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

            //Pick service IP from IP route,
            //we could always send ARP probe with zero IP??
            if (dynamic_cast<const InetUnicastRouteEntry *>(route)) {
                gw_ip_ = path->subnet_service_ip();
            }
            uint32_t intf_id = intf->id();
            //Build new list of interfaces in active state
            WaitForTrafficIntfMap::const_iterator wait_for_traffic_it =
                wait_for_traffic_map.find(intf_id);
            if (wait_for_traffic_it == wait_for_traffic_map.end()) {
                new_wait_for_traffic_map.insert(std::make_pair(intf_id, 0));
            } else {
                new_wait_for_traffic_map.insert(std::make_pair(intf_id,
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
    sg_list_(0), policy_(false), resolve_route_(false) {
    if (plen == Address::kMaxV4PrefixLen && ip != Ip4Address(0)) {
       arp_path_preference_state_.reset(vrf_state->Locate(ip));
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
        start_iter->second->Resync(policy_, vn_, sg_list_);
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

    if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        resolve_route_ = true;
    }

    bool policy = ip_rt->GetActiveNextHop()->PolicyEnabled();
    const SecurityGroupList sg = ip_rt->GetActivePath()->sg_list();
    

    if (policy_ != policy || sg != sg_list_ ||
        vn_ != ip_rt->GetActivePath()->dest_vn_name()) {
        policy_ = policy;
        sg_list_ = sg;
        vn_ = ip_rt->GetActivePath()->dest_vn_name();
        if (resolve_route_) {
            UpdateArpRoutes(ip_rt);
        }
    }
}

void ArpVrfState::EvpnRouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    EvpnRouteEntry *route = static_cast<EvpnRouteEntry *>(entry);

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

    if (entry->IsDeleted() || deleted) {
        if (state) {
            if (arp_proto->gratuitous_arp_entry() &&
                arp_proto->gratuitous_arp_entry()->key().ip ==
                    route->addr().to_v4().to_ulong()) {
                arp_proto->del_gratuitous_arp_entry();
            }
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
        route->GetActiveNextHop()->GetType() == NextHop::RECEIVE) {
        arp_proto->del_gratuitous_arp_entry();
        //Send Grat ARP
        arp_proto->SendArpIpc(ArpProto::ARP_SEND_GRATUITOUS,
                              route->addr().to_v4().to_ulong(), route->vrf(),
                              arp_proto->ip_fabric_interface());
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

bool ArpVrfState::DeleteEvpnRouteState(DBTablePartBase *part, DBEntryBase *entry) {
    EvpnRouteUpdate(part, entry);
    return true;
}

void ArpVrfState::Delete() {
    deleted = true;
    DBTableWalker *walker = agent->db()->GetWalker();
    walk_id_ = walker->WalkTable(rt_table, NULL,
                          boost::bind(&ArpVrfState::DeleteRouteState, this, _1, _2),
                          boost::bind(&ArpVrfState::WalkDone, _1, this));
    evpn_walk_id_ = walker->WalkTable(evpn_rt_table, NULL,
            boost::bind(&ArpVrfState::DeleteEvpnRouteState, this, _1, _2),
            boost::bind(&ArpVrfState::WalkDone, _1, this));
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

void ArpVrfState::WalkDone(DBTableBase *partition, ArpVrfState *state) {
    if (partition == state->rt_table) {
        state->walk_id_ = DBTableWalker::kInvalidWalkerId;
        state->l3_walk_completed_ = true;
    } else {
        state->evpn_walk_id_ = DBTableWalker::kInvalidWalkerId;
        state->evpn_walk_completed_ = true;
    }

    if (state->PreWalkDone(partition)) {
        delete state;
    }
}

ArpPathPreferenceState* ArpVrfState::Locate(const IpAddress &ip) {
    ArpPathPreferenceState* ptr = arp_path_preference_map_[ip];

    if (ptr == NULL) {
        ptr = new ArpPathPreferenceState(this, vrf->vrf_id(), ip, 32);
        arp_path_preference_map_[ip] = ptr;
    }
    return ptr;
}

void ArpVrfState::Erase(const IpAddress &ip) {
    arp_path_preference_map_.erase(ip);
}

ArpVrfState::ArpVrfState(Agent *agent_ptr, ArpProto *proto, VrfEntry *vrf_entry,
                         AgentRouteTable *table, AgentRouteTable *evpn_table):
    agent(agent_ptr), arp_proto(proto), vrf(vrf_entry), rt_table(table),
    evpn_rt_table(evpn_table), route_table_listener_id(DBTableBase::kInvalidId),
    evpn_route_table_listener_id(DBTableBase::kInvalidId),
    table_delete_ref(this, table->deleter()),
    evpn_table_delete_ref(this, evpn_table->deleter()),
    deleted(false), l3_walk_completed_(false), evpn_walk_completed_(false) {
}

ArpVrfState::~ArpVrfState() {
    assert(arp_path_preference_map_.size() == 0);
}

void ArpProto::InterfaceNotify(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
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
        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(NULL);
            set_ip_fabric_interface_index(-1);
        }
    } else {
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
    if (arp_cache_.find(key) != arp_cache_.end())
        SendArpIpc((ArpProto::ArpMsgType)timer_type, key, itf);
    return false;
}

ArpEntry *ArpProto::gratuitous_arp_entry() const {
    return gratuitous_arp_entry_;
}

void ArpProto::set_gratuitous_arp_entry(ArpEntry *entry) {
    gratuitous_arp_entry_ = entry;
}

void ArpProto::del_gratuitous_arp_entry() {
    if (gratuitous_arp_entry_) {
        delete gratuitous_arp_entry_;
        gratuitous_arp_entry_ = NULL;
    }
}

void ArpProto::SendArpIpc(ArpProto::ArpMsgType type, in_addr_t ip,
                          const VrfEntry *vrf, const Interface* itf) {
    ArpIpc *ipc = new ArpIpc(type, ip, vrf, itf);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::ARP, ipc);
}

void ArpProto::SendArpIpc(ArpProto::ArpMsgType type, ArpKey &key,
                          const Interface* itf) {
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
    uint32_t intf_id = entry->interface()->id();
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

    if (vrf_state->walk_id_ != DBTableWalker::kInvalidWalkerId ||
        vrf_state->evpn_walk_id_ != DBTableWalker::kInvalidWalkerId) {
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
