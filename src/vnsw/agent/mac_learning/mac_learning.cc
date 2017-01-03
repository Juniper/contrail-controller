/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <oper/vn.h>
#include <oper/sg.h>
#include <oper/vm.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/route_common.h>
#include "mac_learning_proto.h"
#include "mac_learning_proto_handler.h"
#include "mac_learning_init.h"
#include "mac_learning.h"
#include "mac_learning_mgmt.h"
#include "mac_aging.h"
#include "vr_bridge.h"
#include "vrouter/ksync/ksync_init.h"
#include "vrouter/ksync/ksync_bridge_table.h"

MacLearningEntry::MacLearningEntry(MacLearningTable *table, uint32_t vrf_id,
                                   const MacAddress &mac,
                                   uint32_t index):
    mac_learning_table_(table), key_(vrf_id, mac), index_(index) {
    vrf_ = table->agent()->vrf_table()->
               FindVrfFromIdIncludingDeletedVrf(key_.vrf_id_);
}

void MacLearningEntry::Delete() {
    if (vrf_ == NULL) {
        return;
    }
    mac_learning_table_->agent()->fabric_evpn_table()->
        DeleteReq(mac_learning_table_->agent()->local_peer(),
                  vrf()->GetName(), mac(), Ip4Address(0), 0, NULL);
}

MacLearningEntryLocal::MacLearningEntryLocal(MacLearningTable *table,
                                             uint32_t vrf_id,
                                             const MacAddress &mac,
                                             uint32_t index,
                                             InterfaceConstRef intf):
    MacLearningEntry(table, vrf_id, mac, index), intf_(intf) {
}

void MacLearningEntryLocal::Add() {
    if (intf_ == NULL || vrf_ == NULL) {
        return;
    }

    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(intf_.get());
    if (vm_intf == NULL) {
        return;
    }

    if (vrf()->IsActive() ==  false) {
        return;
    }

    ethernet_tag_ = vm_intf->ethernet_tag();
    SecurityGroupList sg_list;
    vm_intf->CopySgIdList(&sg_list);

    mac_learning_table_->agent()->fabric_evpn_table()->
        AddLocalVmRouteReq(mac_learning_table_->agent()->local_peer(),
                           vrf()->GetName(),
                           mac(), vm_intf, Ip4Address(0),
                           vm_intf->l2_label(),
                           vm_intf->vn()->GetName(), sg_list,
                           PathPreference(),
                           vm_intf->ethernet_tag(),
                           vm_intf->etree_leaf());
}

MacLearningEntryRemote::MacLearningEntryRemote(MacLearningTable *table,
                                               uint32_t vrf_id,
                                               const MacAddress &mac,
                                               uint32_t index,
                                               IpAddress remote_ip):
    MacLearningEntry(table, vrf_id, mac, index), remote_ip_(remote_ip) {
}

void MacLearningEntryRemote::Add() {

}

MacLearningEntryPBB::MacLearningEntryPBB(MacLearningTable *table,
                                         uint32_t vrf_id,
                                         const MacAddress &mac,
                                         uint32_t index,
                                         const std::string &evpn_vrf,
                                         const MacAddress &bmac,
                                         uint32_t isid):
    MacLearningEntry(table, vrf_id, mac, index), evpn_vrf_(evpn_vrf),
    bmac_(bmac), isid_(isid) {
}

void MacLearningEntryPBB::Add() {
    if (vrf_ == NULL) {
        return;
    }

    PBBRoute *data = new PBBRoute(VrfKey(evpn_vrf_), bmac_, isid_, VnListType(),
                                   SecurityGroupList(), CommunityList());
    EvpnAgentRouteTable::AddRemoteVmRouteReq(mac_learning_table_->agent()->local_peer(),
                                             vrf()->GetName(),
                                             mac(), Ip4Address(0), 0, data);
}

MacLearningTable::MacLearningTable(Agent *agent, uint32_t id):
    agent_(agent), id_(id),
    request_queue_(agent_->task_scheduler()->GetTaskId(kTaskMacLearning), id,
                   boost::bind(&MacLearningTable::RequestHandler,
                               this, _1)) {
        aging_partition_.reset(new MacAgingPartition(agent, id));
}

MacLearningTable::~MacLearningTable() {
    assert(mac_learning_table_.size() == 0);
}


bool MacLearningTable::RequestHandler(MacLearningEntryRequestPtr ptr) {
    switch(ptr->event()) {
    case MacLearningEntryRequest::VROUTER_MSG:
        agent()->mac_learning_proto()->ProcessProto(ptr->pkt_info());
        break;

    case MacLearningEntryRequest::ADD_MAC:
        Add(ptr->mac_learning_entry());
        break;

    case MacLearningEntryRequest::RESYNC_MAC:
        Resync(ptr->mac_learning_entry());
        break;

    case MacLearningEntryRequest::DELETE_MAC:
        Delete(ptr->mac_learning_entry());
        break;

    case MacLearningEntryRequest::FREE_DB_ENTRY:
        agent()->mac_learning_module()->
            mac_learning_db_client()->FreeDBState(ptr->db_entry());
        break;

    default:
        assert(0);
    }
    return true;
}

void MacLearningTable::EnqueueMgmtReq(MacLearningEntryPtr ptr, bool add) {
    MacLearningMgmtRequest::Event e = MacLearningMgmtRequest::ADD_MAC;
    if (add == false) {
        e = MacLearningMgmtRequest::DELETE_MAC;
    }
    MacLearningMgmtRequestPtr req(new MacLearningMgmtRequest(e, ptr));
    agent()->mac_learning_module()->mac_learning_mgmt()->Enqueue(req);
}

void MacLearningTable::Add(MacLearningEntryPtr ptr) {
    MacLearningKey key(ptr->vrf_id(), ptr->mac());
    mac_learning_table_[key] = ptr;
    ptr->Add();
    EnqueueMgmtReq(ptr, true);

    MacLearningEntryRequestPtr aging_req(new MacLearningEntryRequest(
                                   MacLearningEntryRequest::ADD_MAC, ptr));
    aging_partition_->Enqueue(aging_req);
}

void MacLearningTable::Delete(const MacLearningEntryPtr ptr) {
    MacLearningKey key(ptr->vrf_id(), ptr->mac());
    if (mac_learning_table_[key] == NULL) {
        return;
    }

    mac_learning_table_.erase(key);
    ptr->Delete();
    EnqueueMgmtReq(ptr, false);

    MacLearningEntryRequestPtr aging_req(new MacLearningEntryRequest(
                                         MacLearningEntryRequest::DELETE_MAC,
                                         ptr));
    aging_partition_->Enqueue(aging_req);
}

void MacLearningTable::Resync(MacLearningEntryPtr ptr) {
    MacLearningKey key(ptr->vrf_id(), ptr->mac());
    if (mac_learning_table_.find(key) == mac_learning_table_.end()) {
        return;
    }
    ptr->Resync();
}

void MacLearningTable::Enqueue(MacLearningEntryRequestPtr req) {
    request_queue_.Enqueue(req);
}
