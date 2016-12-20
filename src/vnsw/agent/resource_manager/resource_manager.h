/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_manager_hpp
#define vnsw_agent_resource_manager_hpp


using namespace boost::uuids;
using namespace std;

class ResourceKey;
class ResourceType;
class ResourceBackupData;

class ResourceManager {
public:    
    typedef boost::shared_ptr<ResourceKey *> KeyPtr;
    typedef boost::shared_ptr<ResourceType *> TypePtr;
    typedef boost::shared_ptr<ResourceData *> DataPtr;
    typedef boost::shared_ptr<ResourceBackupData> ResourceBackupDataType;
    typedef boost::shared_ptr<ResourceBackupDataEncode> ResourceBackupDataEncodeType;
    typedef boost::shared_ptr<ResourceBackupDataDecode> ResourceBackupDataDecodeType;

    ResourceManager(const Agent *agent);
    ~ResourceManager();

    ResourceManager::DataPtr Allocate(ResourceManager::KeyPtr key);
    ResourceManager::DataPtr Restore(ResourceManager::KeyPtr key,
                                     ResourceManager::DataPtr data);
    void Release(ResourceManager::KeyPtr key);
    ResourceType *resource_type(uint8_t type);

    //Work queue
    bool WorkQueueProcess(ResourceBackupDataType data);
    void BackupResource(KeyPtr key, DataPtr data);
    void RestoreResource();

    //TODO Sandesh
private:    
    boost::scoped_ptr<ResourceType *>resource_type_[ResourceType::MAX];
    WorkQueue<ResourceBackupData> encode_work_queue_;
    WorkQueue<ResourceBackupData> decode_work_queue_;
    const Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(ResourceManager);
};

#endif //resource_manager
