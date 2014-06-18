/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_uuid_mapper.h"

#include "db/db.h"
#include "db/db_table_partition.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_table.h"
#include "schema/vnc_cfg_types.h"

void IFMapUuidMapper::SetUuid(uint64_t ms_long, uint64_t ls_long,
                              boost::uuids::uuid &uu_id) {
    for (int i = 0; i < 8; i++) {
        uu_id.data[7 - i] = ms_long & 0xFF;
        ms_long = ms_long >> 8;
    }

    for (int i = 0; i < 8; i++) {
        uu_id.data[15 - i] = ls_long & 0xFF;
        ls_long = ls_long >> 8;
    }
}

std::string IFMapUuidMapper::UuidToString(const boost::uuids::uuid &id) {   
    std::stringstream uuid_str;
    uuid_str << id;
    return uuid_str.str();
}

std::string IFMapUuidMapper::Add(uint64_t ms_long, uint64_t ls_long,
                                 IFMapNode *node) {
    boost::uuids::uuid uu_id;
    SetUuid(ms_long, ls_long, uu_id);
    std::string uuid_str = UuidToString(uu_id);
    uuid_node_map_.insert(std::make_pair(uuid_str, node));
    return uuid_str;
}

void IFMapUuidMapper::Delete(const std::string &uuid_str) {
    uuid_node_map_.erase(uuid_str);
}

IFMapNode *IFMapUuidMapper::Find(const std::string &uuid_str) {
    UuidNodeMap::iterator loc = uuid_node_map_.find(uuid_str);
    if (loc != uuid_node_map_.end()) {
        return loc->second;;
    }
    return NULL;
}

bool IFMapUuidMapper::Exists(const std::string &uuid_str) {
    UuidNodeMap::iterator loc = uuid_node_map_.find(uuid_str);
    if (loc != uuid_node_map_.end()) {
        return true;
    }
    return false;
}

void IFMapUuidMapper::PrintAllMappedEntries() {
    std::cout << "Printing all UUID mapper entries - UUID : type:NODE-FQN\n";
    for (UuidNodeMap::iterator iter = uuid_node_map_.begin();
         iter != uuid_node_map_.end(); ++iter) {
        IFMapNode *node = iter->second;
        std::cout << iter->first << " : " << node->ToString() << std::endl;
    }
}

// Routines for class IFMapVmUuidMapper

IFMapVmUuidMapper::IFMapVmUuidMapper(DB *db, IFMapServer *server)
        : db_(db), ifmap_server_(server), vm_table_(NULL), registered(false) {
}

IFMapVmUuidMapper::~IFMapVmUuidMapper() {
    Shutdown();
}

void IFMapVmUuidMapper::Initialize() {
    vm_table_ = static_cast<IFMapServerTable *>(
        db_->FindTable("__ifmap__.virtual_machine.0"));
    assert(vm_table_ != NULL);
    vm_lid_ = vm_table_->Register(boost::bind(&IFMapVmUuidMapper::VmNodeProcess,
                                              this, _1, _2));
    registered = true;
}

void IFMapVmUuidMapper::Shutdown() {
    if (registered) {
        vm_table_->Unregister(vm_lid_);
        registered = false;
    }
}

void IFMapVmUuidMapper::VmNodeProcess(DBTablePartBase *partition,
                                      DBEntryBase *entry) {
    IFMapNode *vm_node = static_cast<IFMapNode *>(entry);
    std::string tname = vm_node->table()->Typename();
    assert(tname.compare("virtual-machine") == 0);

    if (!IsFeasible(vm_node)) {
        std::string vm_uuid;
        bool val = NodeToUuid(vm_node, &vm_uuid);

        // Its possible that the add came without any properties i.e no object
        // and hence no entry in node_uuid_map_
        if (val) {
            node_uuid_map_.erase(vm_node);
            uuid_mapper_.Delete(vm_uuid);
        }
        return;
    }

    // Ignore 'change' if the 'add' has already been processed.
    if (NodeProcessed(vm_node)) {
        return;
    }

    IFMapObject *object = vm_node->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
    if (object) {
        // Insert into the uuid-node-mapper
        autogen::VirtualMachine *vm = static_cast<autogen::VirtualMachine *>
                                                        (object);
        if (vm->IsPropertySet(autogen::VirtualMachine::ID_PERMS)) {
            autogen::UuidType uuid = vm->id_perms().uuid;
            std::string vm_uuid = 
                uuid_mapper_.Add(uuid.uuid_mslong, uuid.uuid_lslong, vm_node);

            // Insert into the node-uuid-map
            node_uuid_map_.insert(make_pair(vm_node, vm_uuid));

            // Check if there were any vm-reg's for this VM whose processing we
            // had deferred since the vm-node did not exist then.
            std::string vr_name;
            bool exists = PendingVmRegExists(vm_uuid, &vr_name);
            if (exists) {
                bool subscribe = true;
                ifmap_server_->ProcessVmSubscribe(vr_name, vm_uuid, subscribe);
                pending_vmreg_map_.erase(vm_uuid);
            }
        }
    }
}

IFMapNode *IFMapVmUuidMapper::GetVmNodeByUuid(const std::string &vm_uuid) {
    return uuid_mapper_.Find(vm_uuid);
}

bool IFMapVmUuidMapper::VmNodeExists(const std::string &vm_uuid) {
    return uuid_mapper_.Exists(vm_uuid);
}

void IFMapVmUuidMapper::PrintAllUuidMapperEntries() {
    uuid_mapper_.PrintAllMappedEntries();
}

void IFMapVmUuidMapper::ProcessVmRegAsPending(std::string vm_uuid,
        std::string vr_name, bool subscribe) {
    if (subscribe) {
        pending_vmreg_map_.insert(make_pair(vm_uuid, vr_name));
    } else {
        pending_vmreg_map_.erase(vm_uuid);
    }
}

bool IFMapVmUuidMapper::PendingVmRegExists(const std::string &vm_uuid,
                                           std::string *vr_name) {
    PendingVmRegMap::iterator loc = pending_vmreg_map_.find(vm_uuid);
    if (loc != pending_vmreg_map_.end()) {
        *vr_name = loc->second;
        return true;
    }
    return false;
}

void IFMapVmUuidMapper::PrintAllPendingVmRegEntries() {
    std::cout << "Printing all pending vm-reg entries - VM-UUID : VR-FQN\n";
    for (PendingVmRegMap::iterator iter = pending_vmreg_map_.begin();
         iter != pending_vmreg_map_.end(); ++iter) {
        std::cout << iter->first << " : " << iter->second << std::endl;
    }
}

bool IFMapVmUuidMapper::IsFeasible(IFMapNode *node) {
    if (node->IsDeleted()) {
        return false;
    }
    return true;
}

bool IFMapVmUuidMapper::NodeToUuid(IFMapNode *vm_node, std::string *vm_uuid) {
    NodeUuidMap::iterator loc = node_uuid_map_.find(vm_node);
    if (loc != node_uuid_map_.end()) {
        *vm_uuid = loc->second;
        return true;
    }
    return false;
}

bool IFMapVmUuidMapper::NodeProcessed(IFMapNode *vm_node) {
    NodeUuidMap::iterator loc = node_uuid_map_.find(vm_node);
    if (loc != node_uuid_map_.end()) {
        return true;
    }
    return false;
}

void IFMapVmUuidMapper::PrintAllNodeUuidMappedEntries() {
    std::cout << "Printing all node-UUID entries - type:NODE-FQN : uuid\n";
    for (NodeUuidMap::iterator iter = node_uuid_map_.begin();
         iter != node_uuid_map_.end(); ++iter) {
        IFMapNode *node = iter->first;
        std::cout << node->ToString() << " : " << iter->second << std::endl;
    }
}

