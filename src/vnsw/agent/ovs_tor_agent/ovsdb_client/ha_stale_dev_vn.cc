/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <ovs_tor_agent/tor_agent_init.h>
#include <ovsdb_client.h>
#include <ovsdb_route_peer.h>
#include <ha_stale_dev_vn.h>
#include <ha_stale_vn.h>
#include <ha_stale_l2_route.h>
#include <oper/physical_device_vn.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/agent_path.h>
#include <oper/bridge_route.h>

using namespace OVSDB;

HaStaleDevVnEntry::HaStaleDevVnEntry(OvsdbDBObject *table,
        const std::string &logical_switch) : OvsdbDBEntry(table),
    logical_switch_name_(logical_switch), route_table_(NULL),
    oper_route_table_(NULL) {
}

HaStaleDevVnEntry::~HaStaleDevVnEntry() {
    assert(route_table_ == NULL);
}

void HaStaleDevVnEntry::AddMsg(struct ovsdb_idl_txn *txn) {
    // if table is scheduled for delete, delete the entry
    if (table_->delete_scheduled()) {
        DeleteMsg(txn);
        return;
    }

    // On device ip, vxlan id or Agent Route Table pointer change delete 
    // previous route table and add new with updated vxlan id
    if (route_table_ != NULL &&
        (route_table_->GetDBTable() != oper_route_table_ ||
         route_table_->vxlan_id() != vxlan_id_ ||
         route_table_->dev_ip() != dev_ip_)) {
        DeleteMsg(txn);
    }

    // create route table and register
    if (route_table_ == NULL) {
        route_table_ = new HaStaleL2RouteTable(this, oper_route_table_);
    }
}

void HaStaleDevVnEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
    AddMsg(txn);
}

void HaStaleDevVnEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
    // delete route table.
    if (route_table_ != NULL) {
        route_table_->DeleteTable();
        route_table_ = NULL;
    }
}

bool HaStaleDevVnEntry::Sync(DBEntry *db_entry) {
    bool change = false;
    // check if route table is available.
    const PhysicalDeviceVn *dev_vn =
        static_cast<const PhysicalDeviceVn *>(db_entry);
    const VrfEntry *vrf = dev_vn->vn()->GetVrf();
    if (vrf == NULL) {
        // trigger change and wait for VRF
        oper_route_table_ = NULL;
        change = true;
    } else if (oper_route_table_ != vrf->GetBridgeRouteTable()) {
        oper_route_table_ = vrf->GetBridgeRouteTable();
        change = true;
    }
    if (dev_ip_ != dev_vn->tor_ip()) {
        dev_ip_ = dev_vn->tor_ip();
        change = true;
    }
    if (vn_name_ != dev_vn->vn()->GetName()) {
        vn_name_ = dev_vn->vn()->GetName();
        change = true;
    }
    if (vxlan_id_ != (uint32_t)dev_vn->vxlan_id()) {
        vxlan_id_ = dev_vn->vxlan_id();
        change = true;
    }
    return change;
}

bool HaStaleDevVnEntry::IsLess(const KSyncEntry &entry) const {
    const HaStaleDevVnEntry &vrf_entry =
        static_cast<const HaStaleDevVnEntry &>(entry);
    return (logical_switch_name_.compare(vrf_entry.logical_switch_name_) < 0);
}

KSyncEntry* HaStaleDevVnEntry::UnresolvedReference() {
    HaStaleDevVnTable *table =
        static_cast<HaStaleDevVnTable *>(table_);
    HaStaleVnEntry key(table->vn_table_,
                               StringToUuid(logical_switch_name_));
    HaStaleVnEntry *entry = static_cast<HaStaleVnEntry*>
        (table->vn_table_->GetReference(&key));
    if (!entry->IsResolved()) {
        return entry;
    }

    if (oper_route_table_ == NULL) {
        oper_route_table_ = entry->bridge_table();
    }

    return NULL;
}

Agent *HaStaleDevVnEntry::GetAgentPtr() {
    HaStaleDevVnTable *reflector_table =
        static_cast<HaStaleDevVnTable *>(table_);
    return reflector_table->GetAgentPtr();
}

OvsPeer *HaStaleDevVnEntry::route_peer() {
    HaStaleDevVnTable *reflector_table =
        static_cast<HaStaleDevVnTable *>(table_);
    return reflector_table->route_peer();
}

const std::string &HaStaleDevVnEntry::dev_name() {
    HaStaleDevVnTable *reflector_table =
        static_cast<HaStaleDevVnTable *>(table_);
    return reflector_table->dev_name();
}

ConnectionStateEntry *HaStaleDevVnEntry::state() {
    HaStaleDevVnTable *reflector_table =
        static_cast<HaStaleDevVnTable *>(table_);
    return reflector_table->state();
}

IpAddress HaStaleDevVnEntry::dev_ip() {
    return dev_ip_;
}

const std::string &HaStaleDevVnEntry::vn_name() {
    return vn_name_;
}

uint32_t HaStaleDevVnEntry::vxlan_id() {
    return vxlan_id_;
}

HaStaleDevVnTable::HaStaleDevVnTable(Agent *agent,
        OvsPeerManager *manager, ConnectionStateEntry *state,
        std::string &dev_name) :
    OvsdbDBObject(NULL, false), agent_(agent), manager_(manager),
    dev_name_(dev_name), state_(state),
    vn_table_(new HaStaleVnTable(agent, this)) {
    vn_reeval_queue_ = new WorkQueue<std::string>(
            TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
            boost::bind(&HaStaleDevVnTable::VnReEval, this, _1));
    Ip4Address zero_ip;
    route_peer_.reset(manager->Allocate(zero_ip));
    route_peer_->set_route_reflector(true);
    OvsdbRegisterDBTable((DBTable *)agent->physical_device_vn_table());
}

HaStaleDevVnTable::~HaStaleDevVnTable() {
    vn_reeval_queue_->Shutdown();
    delete vn_reeval_queue_;
    manager_->Free(route_peer_.release());
}

KSyncEntry *HaStaleDevVnTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const HaStaleDevVnEntry *k_entry =
        static_cast<const HaStaleDevVnEntry *>(key);
    HaStaleDevVnEntry *entry =
        new HaStaleDevVnEntry(this, k_entry->logical_switch_name_);
    return entry;
}

KSyncEntry *HaStaleDevVnTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const PhysicalDeviceVn *entry =
        static_cast<const PhysicalDeviceVn *>(db_entry);
    HaStaleDevVnEntry *key =
        new HaStaleDevVnEntry(this, UuidToString(entry->vn()->GetUuid()));
    return static_cast<KSyncEntry *>(key);
}

KSyncDBObject::DBFilterResp HaStaleDevVnTable::OvsdbDBEntryFilter(
        const DBEntry *entry, const OvsdbDBEntry *ovsdb_entry) {
    const PhysicalDeviceVn *dev_vn =
        static_cast<const PhysicalDeviceVn *>(entry);

    // Delete the entry which has invalid VxLAN id associated.
    if (dev_vn->vxlan_id() == 0) {
        return DBFilterDelete;
    }

    // Accept only devices with name matched to dev_name_
    if (dev_vn->device_display_name() != dev_name_) {
        return DBFilterDelete;
    }

    if (vn_table_ == NULL) {
        // delete table delete notify the entry
        return DBFilterDelete;
    }

    return DBFilterAccept;
}

Agent *HaStaleDevVnTable::GetAgentPtr() {
    return agent_;
}

void HaStaleDevVnTable::DeleteTableDone() {
    if (vn_table_ != NULL) {
        vn_table_->DeleteTable();
        vn_table_ = NULL;
    }
}

void HaStaleDevVnTable::EmptyTable() {
    OvsdbDBObject::EmptyTable();
    // unregister the object if emptytable is called with
    // object being scheduled for delete
    if (delete_scheduled()) {
        KSyncObjectManager::Unregister(this);
    }
}

void HaStaleDevVnTable::VnReEvalEnqueue(std::string vn_name) {
    vn_reeval_queue_->Enqueue(vn_name);
}

bool HaStaleDevVnTable::VnReEval(std::string vn_name) {
    HaStaleDevVnEntry key(this, vn_name);
    HaStaleDevVnEntry *entry =
        static_cast<HaStaleDevVnEntry*>(Find(&key));
    if (entry && !entry->IsDeleted()) {
        Change(entry);
    }
    return true;
}

OvsPeer *HaStaleDevVnTable::route_peer() {
    return route_peer_.get();
}

const std::string &HaStaleDevVnTable::dev_name() {
    return dev_name_;
}

ConnectionStateEntry *HaStaleDevVnTable::state() {
    return state_.get();
}

