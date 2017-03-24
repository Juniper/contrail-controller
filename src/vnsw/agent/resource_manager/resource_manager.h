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
class ResourceRestoreReq;
class ResourceBackupManager;

// Resource maanager is to restore the resources.
// and retain the old labels after reading form the File.
class ResourceManager {
public:
    typedef boost::shared_ptr<ResourceKey> KeyPtr;
    typedef boost::shared_ptr<ResourceData> DataPtr;
    typedef boost::shared_ptr<ResourceRestoreReq> ResourceRestoreReqPtr;

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
    bool Audit();

    //Work queue to restore the data.
    bool WorkQueueRestoreProcess(ResourceRestoreReqPtr restore_data);
    Agent *agent() {return agent_;}
private:
    Agent *agent_;
    boost::scoped_ptr<ResourceTable>resource_table_[Resource::MAX];
    WorkQueue<ResourceRestoreReqPtr> restore_work_queue_;
    boost::scoped_ptr<ResourceBackupManager> backup_mgr_;
    DISALLOW_COPY_AND_ASSIGN(ResourceManager);
};
// Restore request is to restore the Data & Key from File.
class ResourceRestoreReq {
public:
    ResourceRestoreReq(ResourceManager::KeyPtr key,
                       ResourceManager::DataPtr data);
    virtual ~ResourceRestoreReq();
    ResourceManager::KeyPtr key() {return key_;}
    ResourceManager::DataPtr data() {return data_;}

private:
    ResourceManager::KeyPtr key_;
    ResourceManager::DataPtr data_;
    DISALLOW_COPY_AND_ASSIGN(ResourceRestoreReq);
};

#endif //resource_manager
