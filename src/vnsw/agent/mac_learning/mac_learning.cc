/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#include <oper/vn.h>
#include <oper/sg.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/interface_common.h>
#include <oper/route_common.h>
#include "mac_learning_proto.h"
#include "mac_learning_proto_handler.h"
#include "mac_learning_init.h"
#include "mac_learning.h"
#include "mac_learning_mgmt.h"
#include "mac_aging.h"
#include "vr_bridge.h"

MacLearningEntry::MacLearningEntry(MacLearningPartition *table, uint32_t vrf_id,
                                   const MacAddress &mac,
                                   uint32_t index):
    mac_learning_table_(table), key_(vrf_id, mac), index_(index) {
    vrf_ = table->agent()->vrf_table()->
               FindVrfFromIdIncludingDeletedVrf(key_.vrf_id_);
}

void MacLearningEntry::Delete() {
    mac_learning_table_->agent()->fabric_evpn_table()->
        DeleteReq(mac_learning_table_->agent()->mac_learning_peer(),
                  vrf()->GetName(), mac(), Ip4Address(0), 0, NULL);
}

MacLearningEntryLocal::MacLearningEntryLocal(MacLearningPartition *table,
                                             uint32_t vrf_id,
                                             const MacAddress &mac,
                                             uint32_t index,
                                             InterfaceConstRef intf):
    MacLearningEntry(table, vrf_id, mac, index), intf_(intf) {
}

void MacLearningEntryLocal::Add() {
    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(intf_.get());
    assert(vm_intf);

    if (vrf()->IsActive() ==  false) {
        return;
    }

    if (vm_intf->vn() == NULL) {
        return;
    }

    ethernet_tag_ = vm_intf->ethernet_tag();
    SecurityGroupList sg_list;
    vm_intf->CopySgIdList(&sg_list);

    mac_learning_table_->agent()->fabric_evpn_table()->
        AddLocalVmRouteReq(mac_learning_table_->agent()->mac_learning_peer(),
                           vrf()->GetName(),
                           mac(), vm_intf, Ip4Address(0),
                           vm_intf->l2_label(),
                           vm_intf->vn()->GetName(), sg_list,
                           PathPreference(),
                           vm_intf->ethernet_tag(),
                           vm_intf->etree_leaf());
}

MacLearningEntryRemote::MacLearningEntryRemote(MacLearningPartition *table,
                                               uint32_t vrf_id,
                                               const MacAddress &mac,
                                               uint32_t index,
                                               IpAddress remote_ip):
    MacLearningEntry(table, vrf_id, mac, index), remote_ip_(remote_ip) {
}

void MacLearningEntryRemote::Add() {

}

MacLearningEntryPBB::MacLearningEntryPBB(MacLearningPartition *table,
                                         uint32_t vrf_id,
                                         const MacAddress &mac,
                                         uint32_t index,
                                         const MacAddress &bmac) :
    MacLearningEntry(table, vrf_id, mac, index), bmac_(bmac) {
}

void MacLearningEntryPBB::Add() {
    assert(vrf_);
    uint32_t isid = vrf_->isid();
    std::string bmac_vrf_name = vrf_->bmac_vrf_name();
    PBBRoute *data = new PBBRoute(VrfKey(bmac_vrf_name), bmac_, isid,
                                   VnListType(),
                                   SecurityGroupList());
    EvpnAgentRouteTable::AddRemoteVmRouteReq(mac_learning_table_->agent()->
                                             mac_learning_peer(),
                                             vrf()->GetName(),
                                             mac(), Ip4Address(0), 0, data);
}

MacLearningPartition::MacLearningPartition(Agent *agent, uint32_t id):
    agent_(agent), id_(id),
    request_queue_(agent_->task_scheduler()->GetTaskId(kTaskMacLearning), id,
                   boost::bind(&MacLearningPartition::RequestHandler,
                               this, _1)) {
        aging_partition_.reset(new MacAgingPartition(agent, id));
}

MacLearningPartition::~MacLearningPartition() {
    assert(mac_learning_table_.size() == 0);
}


bool MacLearningPartition::RequestHandler(MacLearningEntryRequestPtr ptr) {
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

void MacLearningPartition::EnqueueMgmtReq(MacLearningEntryPtr ptr, bool add) {
    MacLearningMgmtRequest::Event e = MacLearningMgmtRequest::ADD_MAC;
    if (add == false) {
        e = MacLearningMgmtRequest::DELETE_MAC;
    }
    MacLearningMgmtRequestPtr req(new MacLearningMgmtRequest(e, ptr));
    agent()->mac_learning_module()->mac_learning_mgmt()->Enqueue(req);
}

void MacLearningPartition::Add(MacLearningEntryPtr ptr) {
    MacLearningKey key(ptr->vrf_id(), ptr->mac());
    mac_learning_table_[key] = ptr;
    ptr->Add();
    EnqueueMgmtReq(ptr, true);

    MacLearningEntryRequestPtr aging_req(new MacLearningEntryRequest(
                                   MacLearningEntryRequest::ADD_MAC, ptr));
    aging_partition_->Enqueue(aging_req);
}

void MacLearningPartition::Delete(const MacLearningEntryPtr ptr) {
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

void MacLearningPartition::Resync(MacLearningEntryPtr ptr) {
    MacLearningKey key(ptr->vrf_id(), ptr->mac());
    if (mac_learning_table_.find(key) == mac_learning_table_.end()) {
        return;
    }
    ptr->Resync();
}

void MacLearningPartition::Enqueue(MacLearningEntryRequestPtr req) {
    request_queue_.Enqueue(req);
}
