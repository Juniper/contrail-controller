/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_manager_hpp
#define vnsw_agent_resource_manager_hpp

#include <resource_manager/resource_cmn.h>
#include <resource_manager/resource_type.h>

using namespace boost::uuids;
using namespace std;

class Resource;
class ResourceKey;
class ResourceData;
class ResourceType;
class ResourceBackupData;
class ResourceBackupDataDecode;
class ResourceBackupDataEncode;
class ResourceBackupManager;

class ResourceManager {
public:    
    typedef boost::shared_ptr<ResourceKey> KeyPtr;
    typedef boost::shared_ptr<ResourceType> TypePtr;
    typedef boost::shared_ptr<ResourceData> DataPtr;
    typedef boost::shared_ptr<ResourceBackupData> ResourceBackupDataType;
    typedef boost::shared_ptr<ResourceBackupDataEncode> ResourceBackupDataEncodeType;
    typedef boost::shared_ptr<ResourceBackupDataDecode> ResourceBackupDataDecodeType;

    ResourceManager(Agent *agent);
    ~ResourceManager();

    void Init();

    void ReserveIndex(Resource::Type, uint32_t index);
    void ReleaseIndex(Resource::Type, uint32_t index);

    ResourceManager::DataPtr Allocate(ResourceManager::KeyPtr key);
    void EnqueueRestore(ResourceManager::KeyPtr key,
                        ResourceManager::DataPtr data);
    void RestoreResource(ResourceManager::KeyPtr key,
                         ResourceManager::DataPtr data);
    void Release(ResourceManager::KeyPtr key);
    void Release(Resource::Type, uint32_t index);
    ResourceType *resource_type(uint8_t type);
    ResourceBackupManager *backup_mgr() {return backup_mgr_.get();}
    void FlushStale();

    //Work queue
    bool WorkQueueProcess(ResourceBackupDataType data);
    void BackupResource(KeyPtr key, DataPtr data, bool del);
    void RestoreResource();

    Agent *agent() {return agent_;}
    //TODO Sandesh
private:    
    Agent *agent_;
    boost::scoped_ptr<ResourceType>resource_type_[Resource::MAX];
    WorkQueue<ResourceBackupDataType> decode_work_queue_;
    WorkQueue<ResourceBackupDataType> encode_work_queue_;
    boost::scoped_ptr<ResourceBackupManager> backup_mgr_;
    DISALLOW_COPY_AND_ASSIGN(ResourceManager);
};

#endif //resource_manager
