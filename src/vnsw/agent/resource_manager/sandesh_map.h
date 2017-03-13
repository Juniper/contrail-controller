/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_resource_sandesh_map_hpp
#define vnsw_agent_resource_sandesh_map_hpp
#include "resource_manager/resource_manager_types.h"

class Timer;
class ResourceManager;
class ResourceBackupManager;
// Backup Resource Table maintains all the Sandesh data to
// resource index in a map. Timer is maintained per Backup Resource table.
// will be used to trigger Write to file based on idle time out logic.
// Trigger will be intiated Only when we don't see any frequent Changes
// in the Data modifications with in the idle time out period otherwise
// Write to file will happens upon fallback.
class BackUpResourceTable {
public:
    static const uint8_t  kFallBackCount = 6;
    BackUpResourceTable(ResourceBackupManager *manager,
                        const std::string &name,
                        const std::string& file_name);
    virtual ~BackUpResourceTable();

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
    const std::string & backup_dir() {return backup_dir_;}
    static const std::string FindFile(const std::string &root,
                                      const std::string & file_ext);
    static bool CalculateHashSum(const std::string &file_name,
                                 uint32_t *hashsum);
    const std::string& file_name_str() {return file_name_str_;}
    const std::string& file_name_prefix() {return file_name_prefix_;}
protected:
    template <typename T1, typename T2>
    bool WriteMapToFile(T1* sandesh_data, const T2& index_map);
    template <typename T>
    void ReadMapFromFile(T* sandesh_data, const std::string &root);
    std::string backup_dir_;

private:
    ResourceBackupManager *backup_manager_;
    Agent *agent_;
    std::string name_;
    Timer *timer_;
    uint32_t backup_idle_timeout_;
    uint64_t last_modified_time_;
    uint8_t fall_back_count_;
    std::string file_name_str_;
    std::string file_name_prefix_;
    DISALLOW_COPY_AND_ASSIGN(BackUpResourceTable);
};

// Vrf backup resource table to maintains Sandesh encoded data VrfMpls info
class VrfMplsBackUpResourceTable : public BackUpResourceTable {
public:
    typedef std::map<uint32_t, VrfMplsResource> Map;
    typedef Map::iterator MapIter;
    VrfMplsBackUpResourceTable(ResourceBackupManager *manager);
    virtual ~VrfMplsBackUpResourceTable();

    bool WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    Map& map() {return map_;}
private:
    Map map_;
};

// Vlan backup resource table to maintains Sandesh encoded data VlanMpls info
class VlanMplsBackUpResourceTable : public BackUpResourceTable {
public:
    typedef std::map<uint32_t, VlanMplsResource> Map;
    typedef Map::iterator MapIter;
    VlanMplsBackUpResourceTable(ResourceBackupManager *manager);
    virtual ~VlanMplsBackUpResourceTable();

    bool WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    Map& map() {return map_;}
private:
    Map map_;
};

// Interface backup resource table to maintains sandesh encode data for
// interfaceMpls info
class InterfaceMplsBackUpResourceTable : public BackUpResourceTable {
public:
    typedef std::map<uint32_t, InterfaceIndexResource> Map;
    typedef Map::iterator MapIter;
    InterfaceMplsBackUpResourceTable(ResourceBackupManager *manager);
    virtual ~InterfaceMplsBackUpResourceTable();

    bool WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    Map& map() {return map_;}
private:
    Map map_;
};

// Route backup resource table to maintains sandesh encoded data for route info
class RouteMplsBackUpResourceTable : public BackUpResourceTable {
public:
    typedef std::map<uint32_t, RouteMplsResource> Map;
    typedef Map::iterator MapIter;
    RouteMplsBackUpResourceTable(ResourceBackupManager *manager);
    virtual ~RouteMplsBackUpResourceTable();

    bool WriteToFile();
    void ReadFromFile();
    void RestoreResource();
    Map& map() {return map_;}
private:
    Map map_;
};
// Maintians all the Sandesh encoded structures
class ResourceSandeshMaps {
public:
    typedef pair<uint32_t, VrfMplsResource> VrfMplsResourcePair;
    typedef pair<uint32_t, VlanMplsResource> VlanMplsResourcePair;
    typedef pair<uint32_t, InterfaceIndexResource>
        InterfaceMplsResourcePair;
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
    void AddVlanMplsResourceEntry(uint32_t index,
                                 VlanMplsResource data);
    void DeleteVlanMplsResourceEntry(uint32_t index);
    void AddRouteMplsResourceEntry(uint32_t index,
                                   RouteMplsResource data);
    void DeleteRouteMplsResourceEntry(uint32_t index);
    InterfaceMplsBackUpResourceTable& interface_mpls_index_table() {
       return interface_mpls_index_table_;
    }

    VrfMplsBackUpResourceTable& vrf_mpls_index_table() {
        return vrf_mpls_index_table_;
    }

    VlanMplsBackUpResourceTable& vlan_mpls_index_table() {
        return vlan_mpls_index_table_;
    }

    RouteMplsBackUpResourceTable& route_mpls_index_table() {
        return route_mpls_index_table_;
    }

private:
    ResourceBackupManager *backup_manager_;
    Agent *agent_;
    InterfaceMplsBackUpResourceTable interface_mpls_index_table_;
    VrfMplsBackUpResourceTable vrf_mpls_index_table_;
    VlanMplsBackUpResourceTable vlan_mpls_index_table_;
    RouteMplsBackUpResourceTable route_mpls_index_table_;
    DISALLOW_COPY_AND_ASSIGN(ResourceSandeshMaps);
};
#endif
