/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#include <oper/vn.h>
#include <oper/sg.h>
#include <oper/vm.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/interface_common.h>
#include <oper/route_common.h>
#include "mac_learning_proto.h"
#include "mac_learning_proto_handler.h"
#include "mac_learning_init.h"
#include "mac_learning.h"
#include "mac_aging.h"
#include "mac_learning_mgmt.h"
#include "vr_bridge.h"
#include "vrouter/ksync/ksync_init.h"
#include "vrouter/ksync/ksync_bridge_table.h"

MacAgingEntry::MacAgingEntry(MacLearningEntryPtr ptr):
    mac_learning_entry_(ptr), packets_(0), deleted_(false) {
    last_modified_time_ = UTCTimestampUsec();
    addition_time_ = UTCTimestampUsec();
}

void MacAgingEntry::FillSandesh(SandeshMacEntry *smac) const {
    smac->set_vrf(mac_learning_entry_->vrf()->GetName());
    smac->set_mac(mac_learning_entry_->mac().ToString());
    smac->set_index(mac_learning_entry_->index());
    smac->set_packets(packets_);
    std::string time_since_addition =
        duration_usecs_to_string(UTCTimestampUsec() - addition_time_);
    smac->set_time_since_add(time_since_addition);
    std::string last_stats_change =
        duration_usecs_to_string(UTCTimestampUsec() - last_modified_time_);
    smac->set_last_stats_change(last_stats_change);
}

MacAgingTable::MacAgingTable(Agent *agent, const VrfEntry *vrf) :
    agent_(agent), timeout_msec_(kDefaultAgingTimeout), vrf_(vrf) {
}

MacAgingTable::~MacAgingTable() {
}

void MacAgingTable::Add(MacLearningEntryPtr ptr) {
    MacAgingEntryTable::iterator it = aging_table_.find(ptr.get());
    if (it != aging_table_.end()) {
       it->second->set_deleted(false);
       return;
    }

    MacAgingEntryPtr aging_entry_ptr(new MacAgingEntry(ptr));
    aging_table_.insert(MacAgingPair(ptr.get(), aging_entry_ptr));
    Trace("Adding MAC entry", aging_entry_ptr.get());
}

void MacAgingTable::Delete(MacLearningEntryPtr ptr) {
    MacAgingEntryTable::iterator it = aging_table_.find(ptr.get());
    if (it != aging_table_.end()) {
        Trace("Deleting MAC entry", it->second.get());
        aging_table_.erase(it);
    }
}

void MacAgingTable::ReadStats(MacAgingEntry *ptr) {
    uint32_t index = ptr->mac_learning_entry()->index();
    vr_bridge_entry *vr_entry = agent_->ksync()->ksync_bridge_memory()->
                                    GetBridgeEntry(index);
    if (vr_entry == NULL) {
        ptr->set_packets(0);
    } else {
        ptr->set_packets(vr_entry->be_packets);
    }
}


bool MacAgingTable::ShouldBeAged(MacAgingEntry *ptr,
                                 uint64_t curr_time) {
    uint64_t packets = ptr->packets();

    ReadStats(ptr);

    if (packets == ptr->packets()) {
        if (curr_time - ptr->last_modified_time() > timeout_in_usecs()) {
            return true;
        }
        return false;
    }

    ptr->set_last_modified_time(curr_time);
    return false;
}

void MacAgingTable::Trace(const std::string &str, MacAgingEntry *ptr) {
    std::string vrf = "";
    if (ptr->mac_learning_entry()->vrf() != NULL) {
        vrf = ptr->mac_learning_entry()->vrf()->GetName();
    }

    MAC_AGING_TRACE(MacLearningTraceBuf, vrf,
                    ptr->mac_learning_entry()->mac().ToString(),
                    ptr->mac_learning_entry()->index(),
                    ptr->packets(), str);
}

void MacAgingTable::SendDeleteMsg(MacAgingEntry *ptr) {
    Trace("Aging", ptr);
    ptr->set_deleted(true);
    MacLearningEntryRequestPtr req(new MacLearningEntryRequest(
                MacLearningEntryRequest::DELETE_MAC, ptr->mac_learning_entry()));
    ptr->mac_learning_entry()->mac_learning_table()->Enqueue(req);
}

uint32_t
MacAgingTable::CalculateEntriesPerIteration(uint32_t aging_table_entry_count) {
    uint32_t entry_count = aging_table_entry_count;

    if (vrf_) {
        timeout_msec_ = vrf_->mac_aging_time() * 1000;
    }

    if (timeout_msec_ == 0) {
        return 0;
    }

    //We want to scan all the entries 2 times before aging timeout
    uint32_t table_scan_time = timeout_msec_ / 10;

    uint32_t no_of_iteration = table_scan_time /
                                   MacAgingPartition::kMinIterationTimeout;

    if (no_of_iteration == 0) {
        no_of_iteration = 1;
    }

    uint32_t entry_count_per_iteration = entry_count / no_of_iteration;

    if (entry_count_per_iteration < kMinEntriesPerScan) {
        entry_count_per_iteration = kMinEntriesPerScan;
    }
    return entry_count_per_iteration;
}

bool MacAgingTable::Run() {
    uint64_t curr_time = UTCTimestampUsec();

    MacAgingEntryTable::const_iterator it = aging_table_.upper_bound(last_key_);
    if (it == aging_table_.end()) {
        it = aging_table_.begin();
    }

    uint32_t i = 0;
    uint32_t entries = CalculateEntriesPerIteration(aging_table_.size());
    while (it != aging_table_.end() && i < entries) {
        if (it->second->deleted() == false &&
                ShouldBeAged(it->second.get(), curr_time)) {
            SendDeleteMsg(it->second.get());
        }
        last_key_ = it->first;
        it++;
        i++;
    }

    if (aging_table_.size() == 0) {
        return false;
    }

    return true;
}

MacAgingPartition::MacAgingPartition(Agent *agent, uint32_t partition_id) :
    agent_(agent), partition_id_(partition_id),
    request_queue_(agent_->task_scheduler()->GetTaskId(kTaskMacAging),
                           partition_id,
                           boost::bind(&MacAgingPartition::RequestHandler,
                                       this, _1)),
    timer_(TimerManager::CreateTimer(*(agent->event_manager()->io_service()),
                                       "MacAgingTimer",
                                       agent->task_scheduler()->
                                       GetTaskId(kTaskMacAging), partition_id)) {
}

MacAgingPartition::~MacAgingPartition() {
    TimerManager::DeleteTimer(timer_);
}

void MacAgingPartition::Enqueue(MacLearningEntryRequestPtr req) {
    request_queue_.Enqueue(req);
}

void MacAgingPartition::Add(MacLearningEntryPtr mle) {
    uint32_t vrf_id = mle->vrf_id();

    if (aging_table_map_[vrf_id] == NULL) {
        const VrfEntry *vrf = agent_->vrf_table()->FindVrfFromId(vrf_id);
        assert(vrf->IsActive() == true);
        MacAgingTablePtr aging_table(new MacAgingTable(agent_, vrf));
        aging_table_map_[vrf_id] = aging_table;
    }

    aging_table_map_[vrf_id]->Add(mle);

    if (timer_->running() == false) {
        timer_->Start(kMinIterationTimeout,
                boost::bind(&MacAgingPartition::Run, this));
    }
}

void MacAgingPartition::Delete(MacLearningEntryPtr mle) {
    uint32_t vrf_id = mle->vrf_id();
    if (aging_table_map_[vrf_id] != NULL) {
        aging_table_map_[vrf_id]->Delete(mle);
    }
}

bool MacAgingPartition::Run() {
    bool ret = false;
    MacAgingTableMap::iterator it = aging_table_map_.begin();
    for (;it != aging_table_map_.end(); it++) {
        if (it->second.get() && it->second->Run()) {
            ret = true;
        }
    }

    return ret;
}

void MacAgingPartition::DeleteVrf(uint32_t id) {
    aging_table_map_[id].reset();
}

bool MacAgingPartition::RequestHandler(MacLearningEntryRequestPtr req) {
    switch(req->event()) {
    case MacLearningEntryRequest::ADD_MAC:
        Add(req->mac_learning_entry());
        break;

    case MacLearningEntryRequest::DELETE_MAC:
        Delete(req->mac_learning_entry());
        break;

    case MacLearningEntryRequest::DELETE_VRF:
        DeleteVrf(req->vrf_id());
        break;

    default:
        assert(0);
    }
    return true;
}
