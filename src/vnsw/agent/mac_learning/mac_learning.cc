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
    mac_learning_table_(table), key_(vrf_id, mac), index_(index),
    deleted_(false) {
    vrf_ = table->agent()->vrf_table()->
               FindVrfFromIdIncludingDeletedVrf(key_.vrf_id_);
}

void MacLearningEntry::Delete() {
    deleted_ = true;
    AddToken(mac_learning_table_->agent()->mac_learning_proto()->
             GetToken(MacLearningEntryRequest::DELETE_MAC));

    mac_learning_table_->agent()->fabric_evpn_table()->
        DeleteReq(mac_learning_table_->agent()->mac_learning_peer(),
                  vrf()->GetName(), mac(), Ip4Address(0), 0, NULL);
}

void MacLearningEntry::AddWithToken() {
    if (Add()) {
        AddToken(mac_learning_table_->agent()->mac_learning_proto()->
                 GetToken(MacLearningEntryRequest::ADD_MAC));
    }
}

void MacLearningEntry::Resync() {
    if (Add()) {
        AddToken(mac_learning_table_->agent()->mac_learning_proto()->
                 GetToken(MacLearningEntryRequest::RESYNC_MAC));
    }
}

MacLearningEntryLocal::MacLearningEntryLocal(MacLearningPartition *table,
                                             uint32_t vrf_id,
                                             const MacAddress &mac,
                                             uint32_t index,
                                             InterfaceConstRef intf):
    MacLearningEntry(table, vrf_id, mac, index), intf_(intf) {
}

bool MacLearningEntryLocal::Add() {
    const VmInterface *vm_intf = dynamic_cast<const VmInterface *>(intf_.get());
    assert(vm_intf);

    if (vrf()->IsActive() ==  false) {
        return false;
    }

    if (vm_intf->vn() == NULL) {
        return false;
    }

    if (vm_intf->vn() == NULL) {
        return false;
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
    return true;
}

MacLearningEntryRemote::MacLearningEntryRemote(MacLearningPartition *table,
                                               uint32_t vrf_id,
                                               const MacAddress &mac,
                                               uint32_t index,
                                               IpAddress remote_ip):
    MacLearningEntry(table, vrf_id, mac, index), remote_ip_(remote_ip) {
}

bool MacLearningEntryRemote::Add() {
    return true;
}

MacLearningEntryPBB::MacLearningEntryPBB(MacLearningPartition *table,
                                         uint32_t vrf_id,
                                         const MacAddress &mac,
                                         uint32_t index,
                                         const MacAddress &bmac) :
    MacLearningEntry(table, vrf_id, mac, index), bmac_(bmac) {
}

bool MacLearningEntryPBB::Add() {
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
    return true;
}

MacLearningRequestQueue::MacLearningRequestQueue(MacLearningPartition *partition,
                                                 TokenPool *pool):
    partition_(partition), pool_(pool),
    queue_(partition_->agent()->task_scheduler()->GetTaskId(kTaskMacLearning),
           partition->id(),
           boost::bind(&MacLearningRequestQueue::HandleEvent, this, _1)) {
    queue_.SetStartRunnerFunc(boost::bind(&MacLearningRequestQueue::TokenCheck,
                                          this));
}

bool MacLearningRequestQueue::TokenCheck() {
    if (pool_) {
        return pool_->TokenCheck();
    }

    return true;
}

bool MacLearningRequestQueue::HandleEvent(MacLearningEntryRequestPtr ptr) {
    return partition_->RequestHandler(ptr);
}

MacLearningPartition::MacLearningPartition(Agent *agent,
                                           MacLearningProto *proto,
                                           uint32_t id):
    agent_(agent), id_(id),
    add_request_queue_(this, proto->add_tokens()),
    change_request_queue_(this, proto->change_tokens()),
    delete_request_queue_(this, proto->delete_tokens()) {
    aging_partition_.reset(new MacAgingPartition(agent, id));
}

MacLearningPartition::~MacLearningPartition() {
    assert(mac_learning_table_.size() == 0);
}

void MacLearningPartition::MayBeStartRunner(TokenPool *pool) {
    if (agent_->mac_learning_proto()->add_tokens() == pool) {
        add_request_queue_.MayBeStartRunner();
    } else if (agent_->mac_learning_proto()->change_tokens() == pool) {
        change_request_queue_.MayBeStartRunner();
    } else {
        delete_request_queue_.MayBeStartRunner();
    }
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
            mac_learning_db_client()->FreeDBState(ptr->db_entry(),
                                                  ptr->gen_id());
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

    std::pair<MacLearningEntryTable::iterator, bool> it =
        mac_learning_table_.insert(MacLearningEntryPair(key, ptr));
    if (it.second == false) {
        //Entry already present, clear the entry and delete it from
        //aging tree
        ptr->CopyToken(it.first->second.get());
        if (it.first->second->deleted() == false) {
            MacLearningEntryRequestPtr aging_req(new MacLearningEntryRequest(
                        MacLearningEntryRequest::DELETE_MAC, it.first->second));
            aging_partition_->Enqueue(aging_req);
        }
        mac_learning_table_[key] = ptr;
    }

    ptr->AddWithToken();
    EnqueueMgmtReq(ptr, true);
    MacLearningEntryRequestPtr aging_req(new MacLearningEntryRequest(
                                   MacLearningEntryRequest::ADD_MAC, ptr));
    aging_partition_->Enqueue(aging_req);
}

void MacLearningPartition::Delete(const MacLearningEntryPtr ptr) {
    if (ptr->deleted() == true) {
        return;
    }

    MacLearningKey key(ptr->vrf_id(), ptr->mac());
    if (mac_learning_table_.find(key) == mac_learning_table_.end()) {
        return;
    }

    ptr->Delete();
    MacLearningEntryRequestPtr aging_req(new MacLearningEntryRequest(
                                         MacLearningEntryRequest::DELETE_MAC,
                                         ptr));
    aging_partition_->Enqueue(aging_req);
    EnqueueMgmtReq(ptr, false);
}

void MacLearningPartition::Resync(MacLearningEntryPtr ptr) {
    MacLearningKey key(ptr->vrf_id(), ptr->mac());
    if (mac_learning_table_.find(key) == mac_learning_table_.end()) {
        return;
    }

    ptr->Resync();
}

void MacLearningPartition::Enqueue(MacLearningEntryRequestPtr req) {
    switch(req->event()) {
    case MacLearningEntryRequest::VROUTER_MSG:
    case MacLearningEntryRequest::ADD_MAC:
    case MacLearningEntryRequest::FREE_DB_ENTRY:
    case MacLearningEntryRequest::DELETE_VRF:
        add_request_queue_.Enqueue(req);
        break;

    case MacLearningEntryRequest::RESYNC_MAC:
        change_request_queue_.Enqueue(req);
        break;

    case MacLearningEntryRequest::DELETE_MAC:
        delete_request_queue_.Enqueue(req);
        break;

    default:
        assert(0);
    }
    return;
}

MacLearningEntry*
MacLearningPartition::Find(const MacLearningKey &key) {
    MacLearningEntryTable::iterator it = mac_learning_table_.find(key);
    if (it == mac_learning_table_.end()) {
        return NULL;
    }
    return it->second.get();
}

MacLearningEntryPtr
MacLearningPartition::TestGet(const MacLearningKey &key) {
    MacLearningEntryTable::iterator it = mac_learning_table_.find(key);
    return it->second;
}

void MacLearningPartition::ReleaseToken(const MacLearningKey &key) {
    MacLearningEntry *mac_entry = Find(key);
    if (mac_entry) {
        mac_entry->ReleaseToken();
        if (mac_entry->deleted()) {
            mac_learning_table_.erase(key);
        }
    }
}

MacLearningSandeshResp::MacLearningSandeshResp(Agent *agent,
                                         MacEntryResp *resp,
                                         std::string resp_ctx,
                                         std::string key,
                                         const MacAddress &mac) :
    Task((TaskScheduler::GetInstance()->
                GetTaskId("Agent::MacLearningSandeshTask")), 0),
    agent_(agent), resp_(resp), resp_data_(resp_ctx), partition_id_(0),
    vrf_id_(0), mac_(MacAddress::ZeroMac()), exact_match_(false),
    user_given_mac_(mac) {
    if (key != agent_->NullString()) {
        SetMacKey(key);
    }
}

MacLearningSandeshResp::~MacLearningSandeshResp() {
}

bool MacLearningSandeshResp::SetMacKey(string key) {
    const char ch = kDelimiter;
    size_t n = std::count(key.begin(), key.end(), ch);
    if (n != 3) {
        return false;
    }

    stringstream ss(key);
    string item;

    if (getline(ss, item, ch)) {
        istringstream(item) >> partition_id_;
    }
    if (getline(ss, item, ch)) {
        istringstream(item) >> vrf_id_;
    }
    if (getline(ss, item, ch)) {
        mac_ = MacAddress::FromString(item);
    }
    if (getline(ss, item, ch)) {
        istringstream(item) >> exact_match_;
    }
    return true;
}

string
MacLearningSandeshResp::GetMacKey() {
    stringstream ss;
    ss << partition_id_ << kDelimiter;
    ss << vrf_id_ << kDelimiter;
    ss << mac_.ToString();
    ss << kDelimiter << exact_match_;
    return ss.str();
}

const MacLearningPartition*
MacLearningSandeshResp::GetPartition() {
    if (partition_id_ >= agent_->flow_thread_count()) {
        return NULL;
    }

    MacLearningPartition *mp = agent_->mac_learning_proto()->
                                Find(partition_id_);
    return mp;
}

bool
MacLearningSandeshResp::Run() {
    std::vector<SandeshMacEntry>& list =
        const_cast<std::vector<SandeshMacEntry>&>(resp_->get_mac_entry_list());
    uint32_t entries_count = 0;

    while (entries_count < kMaxResponse) {
        const MacLearningPartition *mp = GetPartition();
        if (mp == NULL) {
            break;
        }

        MacLearningKey key(vrf_id_, mac_);
        MacLearningPartition::MacLearningEntryTable::const_iterator it;
        if (user_given_mac_ != MacAddress::ZeroMac()) {
            it = mp->mac_learning_table_.find(key);
        } else {
            it = mp->mac_learning_table_.upper_bound(key);
        }

        while (entries_count < kMaxResponse) {
            if (exact_match_ && it->first.vrf_id_ != vrf_id_) {
                break;
            } else {
                vrf_id_ = it->first.vrf_id_;
            }

            if (it == mp->mac_learning_table_.end()) {
                break;
            }

            const MacAgingTable *at =
                mp->aging_partition()->Find(it->first.vrf_id_);
            //Find the aging entry
            const MacAgingEntry *aging_entry =  NULL;
            if (at) {
                aging_entry = at->Find(it->second.get());
            }
            if (aging_entry) {
                SandeshMacEntry data;
                data.set_partition(partition_id_);
                aging_entry->FillSandesh(&data);
                list.push_back(data);
            }

            entries_count++;
            if (user_given_mac_ != MacAddress::ZeroMac()) {
                break;
            }
            mac_ = it->first.mac_;
            it++;
        }

        if (entries_count >= kMaxResponse) {
            break;
        }

        if (exact_match_ == false) {
            vrf_id_++;
        }

        if (it == mp->mac_learning_table_.end()) {
            partition_id_++;
            if (exact_match_ == false) {
                vrf_id_ = 0;
            }
        }

        if (user_given_mac_ != MacAddress::ZeroMac()) {
            //If entry is found in current partition
            //move on to next partition
            if (it != mp->mac_learning_table_.end()) {
                partition_id_++;
            }
            mac_ = user_given_mac_;
        } else {
            mac_ = MacAddress::ZeroMac();
        }
    }

    if (partition_id_ < agent_->flow_thread_count()) {
        resp_->set_mac_key(GetMacKey());
    }

    SendResponse(resp_);
    return true;
}

void MacLearningSandeshResp::SendResponse(SandeshResponse *resp) {
    resp->set_context(resp_data_);
    resp->set_more(false);
    resp->Response();
}

void FetchMacEntry::HandleRequest() const {
    Agent *agent = Agent::GetInstance();

    std::ostringstream str;
    str << "0" << MacLearningSandeshResp::kDelimiter << get_vrf_id() <<
         MacLearningSandeshResp::kDelimiter << get_mac();

    bool exact_match = false;
    if (get_vrf_id() != 0 || get_mac() != agent->NullString()) {
        exact_match = true;
    }
    str << MacLearningSandeshResp::kDelimiter << exact_match;

    MacAddress mac = MacAddress::FromString(get_mac());

    MacEntryResp *resp = new MacEntryResp();
    MacLearningSandeshResp *task = new MacLearningSandeshResp(agent, resp,
                                                        context(),
                                                        str.str(), mac);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void NextMacEntrySet::HandleRequest() const {
    Agent *agent = Agent::GetInstance();

    MacEntryResp *resp = new MacEntryResp();
    MacLearningSandeshResp *task = new MacLearningSandeshResp(agent, resp,
                                                        context(),
                                                        get_mac_entry_key(),
                                                        MacAddress::ZeroMac());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}
