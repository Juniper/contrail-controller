/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_manager_hpp
#define vnsw_agent_resource_manager_hpp

#include <resource_manager/resource_cmn.h>
#include <resource_manager/resource_table.h>


class Resource;
class ResourceKey;
class ResourceData;
class ResourceTable;
class ResourceBackupReq;
class ResourceRestoreReq;
class ResourceBackupManager;

class ResourceManager {
public:    
    typedef boost::shared_ptr<ResourceKey> KeyPtr;
    typedef boost::shared_ptr<ResourceTable> TypePtr;
    typedef boost::shared_ptr<ResourceData> DataPtr;
    typedef boost::shared_ptr<ResourceBackupReq> ResourceBackupReqType;
    typedef boost::shared_ptr<ResourceRestoreReq> ResourceRestoreReqType;

    ResourceManager(Agent *agent);
    ~ResourceManager();

    void Init();

    void ReserveIndex(Resource::Type, uint32_t index);
    void ReleaseIndex(Resource::Type, uint32_t index);

    ResourceManager::DataPtr Allocate(KeyPtr key);
    void EnqueueRestore(KeyPtr key, DataPtr data);
    void RestoreResource(KeyPtr key, DataPtr data);
    void Release(KeyPtr key);
    void Release(Resource::Type, uint32_t index);
    ResourceTable *resource_table(uint8_t type);
    ResourceBackupManager *backup_mgr() {return backup_mgr_.get();}
    void Audit();

    //Work queue
    bool WorkQueueBackUpProcess(ResourceBackupReqType backup_data);
    bool WorkQueueRestoreProcess(ResourceRestoreReqType restore_data);
    void BackupResource(KeyPtr key, DataPtr data, bool del);
    void RestoreResource();

    Agent *agent() {return agent_;}
    //TODO Sandesh
private:    
    Agent *agent_;
    boost::scoped_ptr<ResourceTable>resource_table_[Resource::MAX];
    WorkQueue<ResourceRestoreReqType> restore_work_queue_;
    WorkQueue<ResourceBackupReqType> backup_work_queue_;
    boost::scoped_ptr<ResourceBackupManager> backup_mgr_;
    DISALLOW_COPY_AND_ASSIGN(ResourceManager);
};

#endif //resource_manager
