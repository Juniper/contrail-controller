/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/interface_common.h>
#include <oper/route_common.h>
#include "mac_learning_proto.h"
#include "mac_learning_proto_handler.h"
#include "mac_learning.h"
#include "mac_learning_mgmt.h"

MacLearningMgmtNode::MacLearningMgmtNode(MacLearningEntryPtr ptr):
    mac_entry_(ptr), intf_(this), vrf_(this), rt_(this) {
}

MacLearningMgmtNode::~MacLearningMgmtNode() {
    MacLearningMgmtDBEntry *entry = vrf_.get();
    vrf_.reset(NULL);
    if (entry) {
        entry->tree()->TryDelete(entry);
    }

    entry = intf_.get();
    intf_.reset(NULL);
    if (entry) {
        entry->tree()->TryDelete(entry);
    }

    entry = rt_.get();
    rt_.reset(NULL);
    if (entry) {
        entry->tree()->TryDelete(entry);
    }
}

void MacLearningMgmtNode::UpdateRef(MacLearningMgmtManager *mgr) {
    vrf_.reset(mgr->Locate(mac_entry_->vrf()));

    MacLearningEntryLocal *le =
        dynamic_cast<MacLearningEntryLocal *>(mac_entry_.get());
    if (le != NULL) {
        intf_.reset(mgr->Locate(le->intf()));
    }

    MacLearningEntryPBB *pbb_mac =
        dynamic_cast<MacLearningEntryPBB *>(mac_entry_.get());
    if (pbb_mac) {
        rt_.reset(mgr->Locate(pbb_mac->vrf()->bmac_vrf_name(),
                              pbb_mac->bmac()));
    }
}

MacLearningMgmtDBEntry::MacLearningMgmtDBEntry(Type type, const DBEntry *entry):
    type_(type), db_entry_(entry), deleted_(false) {
}

void MacLearningMgmtDBEntry::Change() {
    deleted_ = false;
    for (MacLearningEntryList::iterator iter = mac_entry_list_.begin();
            iter != mac_entry_list_.end(); iter++) {
        MacLearningMgmtNode *ptr = iter.operator->();
        MacLearningEntryRequestPtr req_ptr(new MacLearningEntryRequest(
                                           MacLearningEntryRequest::RESYNC_MAC,
                                           ptr->mac_learning_entry()));

        ptr->mac_learning_entry()->mac_learning_table()->Enqueue(req_ptr);
    }
}

void MacLearningMgmtDBEntry::Delete(bool set_deleted) {
    if (set_deleted) {
        deleted_ = true;
    }
    for (MacLearningEntryList::iterator iter = mac_entry_list_.begin();
            iter != mac_entry_list_.end(); iter++) {
        MacLearningMgmtNode *ptr = iter.operator->();
        MacLearningEntryRequestPtr req_ptr(new MacLearningEntryRequest(
                                           MacLearningEntryRequest::DELETE_MAC,
                                           ptr->mac_learning_entry()));
        ptr->mac_learning_entry()->mac_learning_table()->Enqueue(req_ptr);
    }
}

bool MacLearningMgmtDBEntry::TryDelete() {
    if (mac_entry_list_.empty() == false || tree_->Find(this) == NULL) {
        return false;
    }

    if (db_entry_ == NULL || deleted_ == true) {
        MacLearningEntryRequestPtr req_ptr(new MacLearningEntryRequest(
                    MacLearningEntryRequest::FREE_DB_ENTRY, db_entry_,
                    gen_id_));
        Agent::GetInstance()->mac_learning_proto()->
            Find(0)->Enqueue(req_ptr);
        tree_->Erase(this);
        return true;
    }

    return false;
}

MacLearningMgmtIntfEntry::MacLearningMgmtIntfEntry(const Interface *intf) :
    MacLearningMgmtDBEntry(INTERFACE, intf) {
}

MacLearningMgmtVrfEntry::MacLearningMgmtVrfEntry(const VrfEntry *vrf) :
    MacLearningMgmtDBEntry(VRF, vrf) {
}

MacLearningMgmtDBTree::MacLearningMgmtDBTree(MacLearningMgmtManager *mgr) :
        mac_learning_mac_manager_(mgr) {
}

MacLearningMgmtRouteEntry::MacLearningMgmtRouteEntry(const AgentRoute *rt):
    MacLearningMgmtDBEntry(BRIDGE, rt) {
    const BridgeRouteEntry *br_rt = dynamic_cast<const BridgeRouteEntry *>(rt);
    mac_ = br_rt->mac();
    vrf_ = br_rt->vrf()->GetName();
}

bool MacLearningMgmtRouteEntry::TryDelete() {
   bool ret = MacLearningMgmtDBEntry::TryDelete();
   if (ret == false) {
       return ret;
   }

   //If route entry can be deleted, check if VRF entry can be
   //deleted
   VrfKey vrf_key(vrf_);
   Agent *agent = tree_->mac_learning_mac_manager()->agent();
   const VrfEntry *vrf = static_cast<const VrfEntry *>(
           agent->vrf_table()->Find(&vrf_key, true));
   if (vrf) {
       MacLearningMgmtVrfEntry mgmt_vrf(vrf);
       MacLearningMgmtVrfEntry *mgmt_entry =
           static_cast<MacLearningMgmtVrfEntry *>(
                   tree_->mac_learning_mac_manager()->vrf_tree()->Find(&mgmt_vrf));
       if (mgmt_entry) {
           mgmt_entry->tree()->TryDelete(mgmt_entry);
       }
   }
   return ret;
}

bool MacLearningMgmtVrfEntry::TryDelete() {
    const VrfEntry *vrf = static_cast<const VrfEntry *>(db_entry_);
    if (tree_->mac_learning_mac_manager()->IsVrfRouteEmpty(vrf->GetName())) {
        return MacLearningMgmtDBEntry::TryDelete();
    }
    return false;
}

MacLearningMgmtRouteEntry::MacLearningMgmtRouteEntry(const std::string &vrf,
                                                     const MacAddress &mac):
    MacLearningMgmtDBEntry(BRIDGE, NULL), vrf_(vrf), mac_(mac) {
}

void MacLearningMgmtDBTree::Add(MacLearningMgmtDBEntry *entry) {
    tree_.insert(MacLearningMgmtDBPair(entry, entry));
    entry->set_tree(this);
}

void MacLearningMgmtDBTree::Change(MacLearningMgmtDBEntry *e) {
    MacLearningMgmtDBEntry *entry = Find(e);
    if (entry) {
        entry->Change();
    }
}

void MacLearningMgmtDBTree::Delete(MacLearningMgmtDBEntry *e) {
    MacLearningMgmtDBEntry *entry = Find(e);
    if (entry) {
        entry->Delete(true);
        TryDelete(entry);
    }
}

MacLearningMgmtDBEntry*
MacLearningMgmtDBTree::Find(MacLearningMgmtDBEntry *e) {
    Tree::iterator it = tree_.find(e);
    if (it == tree_.end()) {
        return NULL;
    }

    return it->second;
}

void MacLearningMgmtDBTree::Erase(MacLearningMgmtDBEntry *e) {
    tree_.erase(e);
}

void MacLearningMgmtDBTree::TryDelete(MacLearningMgmtDBEntry *e) {
    if (e->TryDelete()) {
        delete e;
    }
}

void
MacLearningMgmtManager::AddMacLearningEntry(MacLearningMgmtRequestPtr ptr) {
    MacLearningNodeTree::iterator it =
        mac_learning_node_tree_.find(ptr->mac_learning_entry()->key());
    if (it == mac_learning_node_tree_.end()) {
        MacLearningMgmtNodePtr node(new MacLearningMgmtNode(
                                             ptr->mac_learning_entry()));
        node->UpdateRef(this);
        MacLearningNodePair pair(ptr->mac_learning_entry()->key(), node);
        mac_learning_node_tree_.insert(pair);
    } else {
        it->second->set_mac_learning_entry(ptr->mac_learning_entry());
        it->second->UpdateRef(this);
    }
}

void
MacLearningMgmtManager::DeleteMacLearningEntry(MacLearningMgmtRequestPtr ptr) {
    mac_learning_node_tree_.erase(ptr->mac_learning_entry()->key());
}

void MacLearningMgmtManager::AddDBEntry(MacLearningMgmtRequestPtr ptr) {
    MacLearningMgmtDBEntry *entry = Locate(ptr->db_entry());
    entry->set_db_entry(ptr->db_entry());
    entry->Change();
}

void MacLearningMgmtManager::DeleteDBEntry(MacLearningMgmtRequestPtr ptr) {
    MacLearningMgmtDBEntry *entry = Find(ptr->db_entry());
    if (entry) {
        entry->Delete(true);
        entry->set_gen_id(ptr->gen_id());
        entry->tree()->TryDelete(entry);
    }
}

void MacLearningMgmtManager::DeleteAllEntry(MacLearningMgmtRequestPtr ptr) {
    MacLearningMgmtDBEntry *entry = Find(ptr->db_entry());
    if (entry) {
        entry->Delete(false);
    }
}

bool
MacLearningMgmtManager::RequestHandler(MacLearningMgmtRequestPtr ptr) {
    switch(ptr->event()) {
    case MacLearningMgmtRequest::ADD_MAC:
    case MacLearningMgmtRequest::CHANGE_MAC:
        AddMacLearningEntry(ptr);
        break;

    case MacLearningMgmtRequest::DELETE_MAC:
        DeleteMacLearningEntry(ptr);
        break;

    case MacLearningMgmtRequest::ADD_DBENTRY:
    case MacLearningMgmtRequest::CHANGE_DBENTRY:
        AddDBEntry(ptr);
        break;

    case MacLearningMgmtRequest::DELETE_DBENTRY:
        DeleteDBEntry(ptr);
        break;

    case MacLearningMgmtRequest::DELETE_ALL_MAC:
        DeleteAllEntry(ptr);
        break;
    default:
        assert(0);
    }

    return true;
}

void MacLearningMgmtManager::Enqueue(MacLearningMgmtRequestPtr &ptr) {
    request_queue_.Enqueue(ptr);
}

MacLearningMgmtDBEntry*
MacLearningMgmtManager::Locate(const std::string &vrf, const MacAddress &mac) {
    MacLearningMgmtRouteEntry mgmt_rt(vrf, mac);
    MacLearningMgmtRouteEntry *mgmt_entry =
        static_cast<MacLearningMgmtRouteEntry *>(rt_tree_.Find(&mgmt_rt));
    if (mgmt_entry != NULL) {
        return mgmt_entry;
    }

    mgmt_entry = new MacLearningMgmtRouteEntry(vrf, mac);
    rt_tree_.Add(mgmt_entry);
    return mgmt_entry;
}

bool MacLearningMgmtManager::IsVrfRouteEmpty(const std::string &vrf_name) {

    MacLearningMgmtRouteEntry rt_entry(vrf_name, MacAddress::ZeroMac());
    MacLearningMgmtRouteEntry *rt_entry_ptr =
        static_cast<MacLearningMgmtRouteEntry *>(rt_tree_.LowerBound(&rt_entry));
    if (rt_entry_ptr && rt_entry_ptr->vrf() == vrf_name) {
        return false;
    }

    return true;
}

MacLearningMgmtDBEntry*
MacLearningMgmtManager::Find(const DBEntry *e) {
    if (e == NULL) {
        return NULL;
    }

    MacLearningMgmtDBEntry *mgmt_entry = NULL;
    const Interface *intf = dynamic_cast<const Interface *>(e);
    if (intf != NULL) {
        MacLearningMgmtIntfEntry mgmt_intf(intf);
        mgmt_entry = intf_tree_.Find(&mgmt_intf);
        return mgmt_entry;
    }

    const VrfEntry *vrf = dynamic_cast<const VrfEntry *>(e);
    if (vrf != NULL) {
        MacLearningMgmtVrfEntry mgmt_vrf(vrf);
        mgmt_entry = vrf_tree_.Find(&mgmt_vrf);
        return mgmt_entry;
    }

    const AgentRoute *rt = dynamic_cast<const AgentRoute *>(e);
    if (rt != NULL) {
        MacLearningMgmtRouteEntry mgmt_rt(rt);
        mgmt_entry = rt_tree_.Find(&mgmt_rt);
        return mgmt_entry;
    }

    return NULL;
}


MacLearningMgmtDBEntry*
MacLearningMgmtManager::Locate(const DBEntry *e) {
    if (e == NULL) {
        return NULL;
    }

    MacLearningMgmtDBEntry *mgmt_entry = NULL;
    const Interface *intf = dynamic_cast<const Interface *>(e);
    if (intf != NULL) {
        MacLearningMgmtIntfEntry mgmt_intf(intf);
        mgmt_entry = intf_tree_.Find(&mgmt_intf);
        if (mgmt_entry != NULL) {
            return mgmt_entry;
        }

        mgmt_entry = new MacLearningMgmtIntfEntry(intf);
        intf_tree_.Add(mgmt_entry);
        return mgmt_entry;
    }

    const VrfEntry *vrf = dynamic_cast<const VrfEntry *>(e);
    if (vrf != NULL) {
        MacLearningMgmtVrfEntry mgmt_vrf(vrf);
        mgmt_entry = vrf_tree_.Find(&mgmt_vrf);
        if (mgmt_entry != NULL) {
            return mgmt_entry;
        }

        mgmt_entry = new MacLearningMgmtVrfEntry(vrf);
        vrf_tree_.Add(mgmt_entry);
        return mgmt_entry;
    }

    const AgentRoute *rt = dynamic_cast<const AgentRoute *>(e);
    if (rt != NULL) {
        MacLearningMgmtRouteEntry mgmt_rt(rt);
        mgmt_entry = rt_tree_.Find(&mgmt_rt);
        if (mgmt_entry != NULL) {
            return mgmt_entry;
        }

        mgmt_entry = new MacLearningMgmtRouteEntry(rt);
        rt_tree_.Add(mgmt_entry);
        return mgmt_entry;
    }
    return NULL;
}

MacLearningMgmtManager::MacLearningMgmtManager(Agent *agent) :
    agent_(agent), intf_tree_(this), vrf_tree_(this), rt_tree_(this),
    request_queue_(agent_->task_scheduler()->GetTaskId(kTaskMacLearningMgmt), 0,
                   boost::bind(&MacLearningMgmtManager::RequestHandler,
                   this, _1)) {
}
