/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_resource_vxlan_id.h>

using namespace OVSDB;

OvsdbResourceVxLanId::OvsdbResourceVxLanId(OvsdbResourceVxLanIdTable *table,
                                           KSyncEntry *entry)
    : table_(table), entry_(entry), resource_id_(0), vxlan_id_(0),
      active_vxlan_id_(0) {
}

OvsdbResourceVxLanId::~OvsdbResourceVxLanId() {
    ReleaseVxLanId(true);
    ReleaseVxLanId(false);
}

bool OvsdbResourceVxLanId::AcquireVxLanId(uint32_t vxlan_id) {
    if (vxlan_id_ == vxlan_id) {
        return (resource_id_ == 0);
    }

    if (vxlan_id_ != 0) {
        if (resource_id_ != 0 || active_vxlan_id_ != 0) {
            ReleaseVxLanId(false);
        } else {
            active_vxlan_id_ = vxlan_id_;
        }
    }
    vxlan_id_ = vxlan_id;

    if (vxlan_id_ == 0) {
        return true;
    }

    if (vxlan_id_ == active_vxlan_id_) {
        active_vxlan_id_ = 0;
        resource_id_ = 0;
        return true;
    }

    return table_->AcquireVxLanId(this, vxlan_id);
}

void OvsdbResourceVxLanId::ReleaseVxLanId(bool active) {
    if (active) {
        if (active_vxlan_id_ != 0) {
            table_->ReleaseVxLanId(this, active_vxlan_id_, 0);
            active_vxlan_id_ = 0;
        }
    } else {
        if (vxlan_id_ != 0) {
            table_->ReleaseVxLanId(this, vxlan_id_, resource_id_);
            vxlan_id_ = 0;
            resource_id_ = 0;
        }
    }
}

void OvsdbResourceVxLanId::set_active_vxlan_id(uint32_t vxlan_id) {
    if (vxlan_id_ == vxlan_id) {
        assert(resource_id_ == 0);
        // release previous active vxlan id
        ReleaseVxLanId(true);
        return;
    }
    if (active_vxlan_id_ == vxlan_id) {
        // if it is same as active vxlan id return from here
        return;
    }
    assert(vxlan_id == 0);
    // release previous active vxlan id
    ReleaseVxLanId(true);
}

uint32_t OvsdbResourceVxLanId::VxLanId() const {
    return ((resource_id_ == 0) ? vxlan_id_ : 0);
}

uint32_t OvsdbResourceVxLanId::active_vxlan_id() const {
    if (active_vxlan_id_ == 0) {
        return VxLanId();
    }
    return active_vxlan_id_;
}

OvsdbResourceVxLanIdTable::OvsdbResourceVxLanIdTable() {
}

OvsdbResourceVxLanIdTable::~OvsdbResourceVxLanIdTable() {
}

bool OvsdbResourceVxLanIdTable::AcquireVxLanId(OvsdbResourceVxLanId *entry,
                                               uint32_t vxlan_id) {
    std::map<uint32_t, ResourceEntry*>::iterator tbl_it =
        vxlan_table_.find(vxlan_id);
    ResourceEntry *res_entry;
    if (tbl_it != vxlan_table_.end()) {
        res_entry = tbl_it->second;
    } else {
        res_entry = new ResourceEntry();
        vxlan_table_[vxlan_id] = res_entry;
    }

    entry->resource_id_ = res_entry->resource_id_count_;
    res_entry->resource_id_count_++;
    if (entry->resource_id_ == 0) {
        // first entry in the list
        res_entry->active_entry = entry;
        return true;
    } else {
        res_entry->pending_list.insert(entry);
    }
    return false;
}

void OvsdbResourceVxLanIdTable::ReleaseVxLanId(OvsdbResourceVxLanId *entry,
                                               uint32_t vxlan_id,
                                               uint32_t resource_id) {
    std::map<uint32_t, ResourceEntry*>::iterator tbl_it =
        vxlan_table_.find(vxlan_id);
    assert(tbl_it != vxlan_table_.end());
    ResourceEntry *res_entry = tbl_it->second;

    if (resource_id == 0) {
        assert(res_entry->active_entry == entry);
        ResourcePendingList::iterator it = res_entry->pending_list.begin();
        if (it == res_entry->pending_list.end()) {
            vxlan_table_.erase(tbl_it);
            delete res_entry;
        } else {
            (*it)->resource_id_ = 0;
            res_entry->active_entry = (*it);
            KSyncEntry *ovs_entry = (*it)->entry_;
            // only trigger for active entry
            if (ovs_entry->IsActive() &&
                ovs_entry->GetObject() &&
                (ovs_entry->GetObject()->delete_scheduled() == false)) {
                ovs_entry->GetObject()->Change(ovs_entry);
            }
            res_entry->pending_list.erase(it);
            if (res_entry->pending_list.empty()) {
                res_entry->resource_id_count_ = 1;
            }
        }
    } else {
        OvsdbResourceVxLanId key(NULL, entry->entry_);
        key.resource_id_ = resource_id;
        ResourcePendingList::iterator it = res_entry->pending_list.find(&key);
        assert(it != res_entry->pending_list.end());
        res_entry->pending_list.erase(it);
    }
}

