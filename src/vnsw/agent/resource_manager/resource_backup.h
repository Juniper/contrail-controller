/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_backup_hpp
#define vnsw_agent_resource_backup_hpp


#include "resource_manager/resource_manager.h"
#include "resource_manager/sandesh_map.h"

class Timer;
class ResourceSandeshMaps;
class ResourceManager;
class ResourceTable;
class ResourceData;

class ResourceBackupReq {
public:
    enum Op {
        ADD = 1,
        DELETE,
    };

    ResourceBackupReq(ResourceManager::KeyPtr key,
                             ResourceManager::DataPtr data,
                             Op op);
    virtual ~ResourceBackupReq();
    void Process();
    ResourceManager::KeyPtr key() {return key_;}
    ResourceManager::DataPtr data() {return data_;}


private:
    ResourceManager::KeyPtr key_;
    ResourceManager::DataPtr data_;
    Op op_;
    DISALLOW_COPY_AND_ASSIGN(ResourceBackupReq);
};
//Backup manager is to Process the Resource data and store
//it in to a file using Sandesh encoding.
class ResourceBackupManager {
public:
    typedef boost::shared_ptr<ResourceBackupReq> ResourceBackupReqPtr;
    ResourceBackupManager(ResourceManager *mgr);
    virtual ~ResourceBackupManager();

    void Init();
    ResourceSandeshMaps& sandesh_maps();
    bool SaveResourceDataToFile(const std::string &file_name,
                                const uint8_t *buffer,
                                uint32_t size);
    uint32_t ReadResourceDataFromFile(const std::string &file_name,
                                      uint8_t **buf);

    Agent *agent() {return agent_;}
    ResourceManager *resource_manager() {return resource_manager_;}
    bool WorkQueueBackUpProcess(ResourceBackupReqPtr backup_data);
    void BackupResource(ResourceManager::KeyPtr key,
                        ResourceManager::DataPtr data, 
                        ResourceBackupReq::Op op);

private:
    ResourceManager *resource_manager_;
    Agent *agent_;
    ResourceSandeshMaps sandesh_maps_;
    // Work queue to backup the data.
    WorkQueue<ResourceBackupReqPtr> backup_work_queue_;
    DISALLOW_COPY_AND_ASSIGN(ResourceBackupManager);
};

#endif
