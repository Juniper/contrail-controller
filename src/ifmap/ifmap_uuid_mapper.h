/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_UUID_MAPPER_H__
#define __IFMAP_UUID_MAPPER_H__

#include <boost/bind.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "db/db_table.h"

#include <map>
#include <set>
#include <string>

class DB;
class IFMapNode;
class IFMapServer;
class IFMapServerTable;

// Maintains a mapping of [uuid, node]
class IFMapUuidMapper {
public:
    typedef std::map<std::string, IFMapNode *> UuidNodeMap;
    typedef UuidNodeMap::size_type Sz_t;

    std::string Add(uint64_t ms_long, uint64_t ls_long, IFMapNode *node);
    void Delete(const std::string &uuid_str);
    IFMapNode *Find(const std::string &uuid_str);
    bool Exists(const std::string &uuid_str);
    void PrintAllMappedEntries();
    Sz_t Size() { return uuid_node_map_.size(); }

private:
    friend class ShowIFMapUuidToNodeMapping;

    void SetUuid(uint64_t ms_long, uint64_t ls_long, boost::uuids::uuid &uu_id);
    std::string UuidToString(const boost::uuids::uuid &id);

    UuidNodeMap uuid_node_map_;
};

class IFMapVmUuidMapper {
public:
    // Store [vm-uuid, vr-name] from the vm-reg request
    // ADD: vm-reg-request, DELETE: vm-node add/xmpp-not-ready
    typedef std::map<std::string, std::string> PendingVmRegMap;
    // Store [vm-node, vm-uuid]. Used to clean-up uuid_mapper_'s 'vm-uuid'
    // entry when the vm-node becomes InFeasible. The objects would be gone by
    // then and the uuid would not be available from the node.
    // ADD: config vm-node add, DELETE: config vm-node delete
    typedef std::map<IFMapNode *, std::string> NodeUuidMap;

    explicit IFMapVmUuidMapper(DB *db, IFMapServer *server);
    ~IFMapVmUuidMapper();

    void Initialize();
    void Shutdown();
    IFMapUuidMapper::Sz_t UuidMapperCount() {
        return uuid_mapper_.Size();
    }

    DBTable::ListenerId vm_listener_id() { return vm_lid_; }
    DB *db() { return db_; }
    IFMapServer *server() { return ifmap_server_; }
    bool is_registered() { return registered; }

    void VmNodeProcess(DBTablePartBase *partition, DBEntryBase *entry);
    IFMapNode *GetVmNodeByUuid(const std::string &vm_uuid);
    bool VmNodeExists(const std::string &vm_uuid);
    void PrintAllUuidMapperEntries();

    void ProcessVmRegAsPending(std::string vm_uuid, std::string vr_name,
                               bool subscribe);
    bool PendingVmRegExists(const std::string &vm_uuid, std::string *vr_name);
    PendingVmRegMap::size_type PendingVmRegCount() {
        return pending_vmreg_map_.size();
    }
    void CleanupPendingVmRegEntry(const std::string &vm_uuid) {
        pending_vmreg_map_.erase(vm_uuid);
    }
    PendingVrVmRegMap::size_type PendingVrVmRegCount() {
        return pending_vrvm_reg_map_.size();
    }
    void PrintAllPendingVmRegEntries();

    bool NodeToUuid(IFMapNode *vm_node, std::string *vm_uuid);
    bool NodeProcessed(IFMapNode *node);
    NodeUuidMap::size_type NodeUuidMapCount() {
        return node_uuid_map_.size();
    }
    void PrintAllNodeUuidMappedEntries();
    void CleanupPendingVmRegMaps(const std::string &vr_name);

private:
    friend class IFMapVmUuidMapperTest;
    friend class ShowIFMapPendingVmReg;
    friend class ShowIFMapNodeToUuidMapping;
    friend class ShowIFMapUuidToNodeMapping;

    DB *db_;
    IFMapServer *ifmap_server_;
    IFMapServerTable *vm_table_;
    DBTable::ListenerId vm_lid_;
    bool registered;
    // ADD: config vm-node add, DELETE: config vm-node delete
    IFMapUuidMapper uuid_mapper_;
    NodeUuidMap node_uuid_map_;
    PendingVmRegMap pending_vmreg_map_;
    PendingVrVmRegMap pending_vrvm_reg_map_;

    // TODO: consider moving the common parts inside IFMapNode
    bool IsFeasible(IFMapNode *node);
    void ErasePendingVrVmRegMapEntry(const std::string &vr_name,
                                     const std::string &vm_uuid);
};

#endif /* __IFMAP_UUID_MAPPER_H__ */
