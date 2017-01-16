/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_backup_hpp
#define vnsw_agent_resource_backup_hpp


using namespace boost::uuids;
using namespace std;

#include "resource_manager/resource_manager.h"
#include "resource_manager/sandesh_map.h"

class Timer;
class ResourceSandeshMaps;

class ResourceBackupManager {
public:    
    typedef std::map<ResourceKey *, ResourceData *> ResourceMap;

    ResourceBackupManager(ResourceManager *mgr);
    virtual ~ResourceBackupManager();

    void Init();
    void ReadFromFile();
    ResourceSandeshMaps& sandesh_maps();
    void SaveResourceDataToFile(const std::string &file_name,
                                const uint8_t *buffer,
                                uint32_t size);
    uint32_t ReadResourceDataFromFile(const std::string &file_name, uint8_t **buf);
    void Audit();

    Agent *agent() {return agent_;}
    ResourceManager *resource_manager() {return resource_manager_;}

private:    
    ResourceManager *resource_manager_;
    Agent *agent_;
    ResourceSandeshMaps sandesh_maps_;
    DISALLOW_COPY_AND_ASSIGN(ResourceBackupManager);
};

class ResourceBackupData {
public:    
    ResourceBackupData();
    virtual ~ResourceBackupData();

private:    
    DISALLOW_COPY_AND_ASSIGN(ResourceBackupData);
};

class ResourceBackupDataEncode : public ResourceBackupData {
public:    
    ResourceBackupDataEncode(ResourceManager::KeyPtr key,
                             ResourceManager::DataPtr data,
                             bool del);
    virtual ~ResourceBackupDataEncode();
    void Process();
    ResourceManager::KeyPtr key() {return key_;}
    ResourceManager::DataPtr data() {return data_;}


private:    
    ResourceManager::KeyPtr key_;
    ResourceManager::DataPtr data_;
    bool del_;
    DISALLOW_COPY_AND_ASSIGN(ResourceBackupDataEncode);
};

class ResourceBackupDataDecode : public ResourceBackupData {
public:    
    ResourceBackupDataDecode(ResourceManager::KeyPtr key,
                             ResourceManager::DataPtr data);
    virtual ~ResourceBackupDataDecode();
    ResourceManager::KeyPtr key() {return key_;}
    ResourceManager::DataPtr data() {return data_;}

private:    
    ResourceManager::KeyPtr key_;
    ResourceManager::DataPtr data_;
    DISALLOW_COPY_AND_ASSIGN(ResourceBackupDataDecode);
};

#endif
