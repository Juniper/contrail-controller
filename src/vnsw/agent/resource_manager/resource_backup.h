/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_backup_hpp
#define vnsw_agent_resource_backup_hpp


using namespace boost::uuids;
using namespace std;

class ResourceManager;

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
                             ResourceManager::DataPtr data);
    virtual ~ResourceBackupDataEncode();

private:    
    ResourceManager::KeyPtr key_;
    ResourceManager::DataPtr data_;
    DISALLOW_COPY_AND_ASSIGN(ResourceBackupDataEncode);
};

class ResourceBackupDataDecode : public ResourceBackupData {
public:    
    ResourceBackupDataDecode();
    virtual ~ResourceBackupDataDecode();

private:    
    //TODO Sandesh data will go here
    DISALLOW_COPY_AND_ASSIGN(ResourceBackupData);
};

#endif
