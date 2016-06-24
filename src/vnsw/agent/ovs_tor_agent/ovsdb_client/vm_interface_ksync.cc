/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <vm_interface_ksync.h>

#include <oper/vn.h>
#include <oper/interface.h>
#include <oper/vm_interface.h>
#include <oper/vrf.h>
#include <ovsdb_types.h>
#include <ovsdb_route_data.h>
#include <ovsdb_route_peer.h>

using OVSDB::OvsdbDBEntry;
using OVSDB::VMInterfaceKSyncEntry;
using OVSDB::VMInterfaceKSyncObject;

VMInterfaceKSyncEntry::VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
        const VmInterface *entry) : OvsdbDBEntry(table_),
    uuid_(entry->GetUuid()) {
}

VMInterfaceKSyncEntry::VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
        const VMInterfaceKSyncEntry *key) : OvsdbDBEntry(table),
    uuid_(key->uuid_) {
}

VMInterfaceKSyncEntry::VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
        boost::uuids::uuid uuid) : OvsdbDBEntry(table), uuid_(uuid) {
}

void VMInterfaceKSyncEntry::AddMsg(struct ovsdb_idl_txn *txn) {
}

void VMInterfaceKSyncEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
}

void VMInterfaceKSyncEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
}

bool VMInterfaceKSyncEntry::Sync(DBEntry *db_entry) {
    VmInterface *entry = static_cast<VmInterface *>(db_entry);
    bool ret = false;

    std::string vn_name;
    if (entry->vn()) {
        vn_name = UuidToString(entry->vn()->GetUuid());
    }

    SecurityGroupList sg_list;
    entry->CopySgIdList(&sg_list);
    if (sg_list != sg_list_) {
        // Enqueue a RESYNC on the route with new VN-Name and SG-ID
        const VrfEntry *vrf = entry->vrf();
        const VnEntry *vn = entry->vn();
        const VxLanId *vxlan = NULL;
        if (vn)
            vxlan = vn->vxlan_id();
        if (vrf != NULL && vn != NULL && vxlan != NULL &&
            !table_->client_idl()->IsDeleted() &&
            table_->client_idl()->route_peer() != NULL) {
            EvpnAgentRouteTable *evpn_table = static_cast<EvpnAgentRouteTable *>
                (vrf->GetEvpnRouteTable());

            // SG-ID changed, update OVSDB Route for VMI with new SG-ID
            sg_list_ = sg_list;
            OvsdbRouteResyncData *data = new OvsdbRouteResyncData(sg_list);
            evpn_table->ResyncVmRoute(table_->client_idl()->route_peer(),
                                      vrf->GetName(), entry->vm_mac(), IpAddress(),
                                      vxlan->vxlan_id(), data);
            ret = true;
        }
    }

    if (vn_name_ != vn_name) {
        vn_name_ = vn_name;
        ret = true;
    }
    return ret;
}

bool VMInterfaceKSyncEntry::IsLess(const KSyncEntry &entry) const {
    const VMInterfaceKSyncEntry &intf_entry =
        static_cast<const VMInterfaceKSyncEntry&>(entry);
    return uuid_ < intf_entry.uuid_;
}

KSyncEntry *VMInterfaceKSyncEntry::UnresolvedReference() {
    if (vn_name_.empty()) {
        return table_->client_idl()->ksync_obj_manager()->default_defer_entry();
    }
    return NULL;
}

const std::string &VMInterfaceKSyncEntry::vn_name() const {
    return vn_name_;
}

VMInterfaceKSyncObject::VMInterfaceKSyncObject(OvsdbClientIdl *idl, DBTable *table) :
    OvsdbDBObject(idl, table, false) {
}

VMInterfaceKSyncObject::~VMInterfaceKSyncObject() {
}

KSyncEntry *VMInterfaceKSyncObject::Alloc(const KSyncEntry *key, uint32_t index) {
    const VMInterfaceKSyncEntry *k_entry =
        static_cast<const VMInterfaceKSyncEntry *>(key);
    VMInterfaceKSyncEntry *entry = new VMInterfaceKSyncEntry(this, k_entry);
    return entry;
}

KSyncEntry *VMInterfaceKSyncObject::DBToKSyncEntry(const DBEntry* db_entry) {
    const VmInterface *entry = static_cast<const VmInterface *>(db_entry);
    VMInterfaceKSyncEntry *key = new VMInterfaceKSyncEntry(this, entry);
    return static_cast<KSyncEntry *>(key);
}

KSyncDBObject::DBFilterResp VMInterfaceKSyncObject::OvsdbDBEntryFilter(
        const DBEntry *entry, const OvsdbDBEntry *ovsdb_entry) {
    const Interface *intf = static_cast<const Interface *>(entry);
    // only accept vm interfaces
    if (intf->type() != Interface::VM_INTERFACE) {
        return DBFilterIgnore;
    }
    return DBFilterAccept;
}

