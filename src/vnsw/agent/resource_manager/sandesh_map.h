/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_sandesh_map_hpp
#define vnsw_agent_resource_sandesh_map_hpp

using namespace boost::uuids;
using namespace std;

class InterfaceIndexResource;
class VrfMplsResource;
class RouteMplsResource;
class Timer;
class ResourceBackupManager;

class SandeshResourceType {
public:
    static const uint32_t kResourceSyncTimeout = 1000;
    SandeshResourceType(ResourceBackupManager *manager, const std::string &name);
    virtual ~SandeshResourceType();

    virtual void WriteToFile() = 0; 
    virtual void ReadFromFile() = 0; 
    virtual void RestoreResource() = 0; 
    bool TimerExpiry();
    void StartTimer();
    ResourceBackupManager *backup_manager() {
        return backup_manager_;
    }
    Agent *agent() {return agent_;}
    void TriggerBackup();

protected:
    tbb::mutex mutex_;
    std::string backup_dir_;

private:    
    ResourceBackupManager *backup_manager_;
    Agent *agent_;
    std::string name_;
    Timer *timer_;
    DISALLOW_COPY_AND_ASSIGN(SandeshResourceType);
};

class VrfMplsSandeshResourceType : public SandeshResourceType {
public:
    typedef std::map<uint32_t, VrfMplsResource> Map;
    typedef Map::iterator MapIter;

    VrfMplsSandeshResourceType(ResourceBackupManager *manager);
    virtual ~VrfMplsSandeshResourceType();

    void WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    std::map<uint32_t, VrfMplsResource> map_;
};

class InterfaceMplsSandeshResourceType : public SandeshResourceType {
public:
    typedef std::map<uint32_t, InterfaceIndexResource> Map;
    typedef Map::iterator MapIter;

    InterfaceMplsSandeshResourceType(ResourceBackupManager *manager);
    virtual ~InterfaceMplsSandeshResourceType();

    void WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    std::map<uint32_t, InterfaceIndexResource> map_;
};

class RouteMplsSandeshResourceType : public SandeshResourceType {
public:
    typedef std::map<uint32_t, RouteMplsResource> Map;
    typedef Map::iterator MapIter;

    RouteMplsSandeshResourceType(ResourceBackupManager *manager);
    virtual ~RouteMplsSandeshResourceType();

    void WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    std::map<uint32_t, RouteMplsResource> map_;
};

class ResourceSandeshMaps {
public:
    ResourceSandeshMaps(ResourceBackupManager *manager);
    virtual ~ResourceSandeshMaps();

    void ReadFromFile();
    void RestoreResource();
    void EndOfBackup();

    //TODO make these private and provide add/delete routines
    InterfaceMplsSandeshResourceType interface_mpls_index_map_;
    VrfMplsSandeshResourceType vrf_mpls_index_map_;
    RouteMplsSandeshResourceType route_mpls_index_map_;
private:
    ResourceBackupManager *backup_manager_;
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(ResourceSandeshMaps);
};
#endif
