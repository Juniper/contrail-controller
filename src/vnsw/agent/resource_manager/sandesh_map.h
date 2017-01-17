/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_sandesh_map_hpp
#define vnsw_agent_resource_sandesh_map_hpp

class InterfaceIndexResource;
class VrfMplsResource;
class RouteMplsResource;
class Timer;
class ResourceManager;
class ResourceBackupManager;
class SandeshResourceTable {
public:
    static const uint32_t kSandeshMetaDataSize = 1000;
    static const uint8_t KFallBackCount = 6;
    SandeshResourceTable(ResourceBackupManager *manager, const std::string &name);
    virtual ~SandeshResourceTable();

    virtual bool WriteToFile() = 0;
    virtual void ReadFromFile() = 0;
    virtual void RestoreResource() = 0;
    bool TimerExpiry();
    void StartTimer();
    ResourceBackupManager *backup_manager() {
        return backup_manager_;
    }
    Agent *agent() {return agent_;}
    void TriggerBackup();
    bool UpdateRequired();
    void EnqueueRestore(ResourceManager::KeyPtr key,
                        ResourceManager:: DataPtr data);
    const std::string FilePath(const std::string &file_name);
protected:
    std::string backup_dir_;

private:
    ResourceBackupManager *backup_manager_;
    Agent *agent_;
    std::string name_;
    Timer *timer_;
    uint32_t backup_idle_timeout_;
    uint64_t last_update_time_;
    uint8_t fall_back_count_;
    DISALLOW_COPY_AND_ASSIGN(SandeshResourceTable);
};

class VrfMplsSandeshResourceTable : public SandeshResourceTable {
public:
    static const uint32_t kVrfMplsRecordSize = 20;
    typedef std::map<uint32_t, VrfMplsResource> Map;
    typedef Map::iterator MapIter;

    VrfMplsSandeshResourceTable(ResourceBackupManager *manager);
    virtual ~VrfMplsSandeshResourceTable();

    bool WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    Map map_;
    const std::string vrf_file_name_str_;
};

class InterfaceMplsSandeshResourceTable : public SandeshResourceTable {
public:
    static const uint32_t KInterfaceMplsRecordSize = 30;
    typedef std::map<uint32_t, InterfaceIndexResource> Map;
    typedef Map::iterator MapIter;

    InterfaceMplsSandeshResourceTable(ResourceBackupManager *manager);
    virtual ~InterfaceMplsSandeshResourceTable();

    bool WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    Map map_;
    const std::string interface_file_name_str_;
};

class RouteMplsSandeshResourceTable : public SandeshResourceTable {
public:
    static const uint32_t KRouteMplsRecordSize = 24;
    typedef std::map<uint32_t, RouteMplsResource> Map;
    typedef Map::iterator MapIter;

    RouteMplsSandeshResourceTable(ResourceBackupManager *manager);
    virtual ~RouteMplsSandeshResourceTable();

    bool WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    Map map_;
    const std::string route_file_name_str_;
};

class ResourceSandeshMaps {
public:
    typedef pair<uint32_t, VrfMplsResource> VrfMplsResourcePair;
    typedef pair<uint32_t, InterfaceIndexResource> InterfaceMplsResourcePair;
    typedef pair<uint32_t, RouteMplsResource> RouteMplsResourcePair;
    ResourceSandeshMaps(ResourceBackupManager *manager);
    virtual ~ResourceSandeshMaps();
    void ReadFromFile();
    void RestoreResource();
    void EndOfBackup();
    void AddInterfaceMplsResourceEntry(uint32_t index,
                                       InterfaceIndexResource data );
    void DeleteInterfaceMplsResourceEntry(uint32_t index);
    void AddVrfMplsResourceEntry(uint32_t index,
                                 VrfMplsResource data);
    void DeleteVrfMplsResourceEntry(uint32_t index);
    void AddRouteMplsResourceEntry(uint32_t index,
                                   RouteMplsResource data);
    void DeleteRouteMplsResourceEntry(uint32_t index);
    InterfaceMplsSandeshResourceTable& InterfaceMplsResourceTable() {
       return interface_mpls_index_map_;
    }

    VrfMplsSandeshResourceTable& VrfMplsResourceTable() {
        return vrf_mpls_index_map_;
    }

    RouteMplsSandeshResourceTable& RouteMplsResourceTable() {
        return route_mpls_index_map_;
    }

private:
    ResourceBackupManager *backup_manager_;
    Agent *agent_;
    InterfaceMplsSandeshResourceTable interface_mpls_index_map_;
    VrfMplsSandeshResourceTable vrf_mpls_index_map_;
    RouteMplsSandeshResourceTable route_mpls_index_map_;
    DISALLOW_COPY_AND_ASSIGN(ResourceSandeshMaps);
};
#endif
