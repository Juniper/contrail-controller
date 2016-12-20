#ifndef agent_mpls_resource_mgr_h
#define agent_mpls_resource_mgr_h
#include <map>
#include <string>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_mgr.h"
#include "resource_mgr/mpls_resource_data_types.h"

/*This Structure Needs to be modified based on actual Data*/
struct  MplsData : public ResourceData {
    uint32_t mpls_label; 
    std::string data;
};

struct MplsKey : public ResourceKey {

};

class MplsResourceManager : public ResourceManager {
public:
    static const std::string mpls_file_; 
    static const std::string mpls_tmp_file_; 
    static const uint32_t KMplsMaxResourceSize = 1000;
    MplsResourceManager(const std::string &, Agent *agent);
    ~MplsResourceManager() { }
    MplsResourceManager() { }
    virtual void AddResourceData(const ResourceReq& );
    virtual void ReadResourceData(ResourceMap &rmap);
    virtual void DeleteResourceData(const ResourceReq& );
    virtual void ModifyResourceData(const ResourceReq&);
    virtual void WriteToFile();
    virtual void ReadFromFile();
private:
    // Mplsresourcekey sandesh Genrated structure
    // MplsResourceData sandesh Genrated Structure
    // Resource Key can be defined based on requirement.
    std::map<uint32_t, MplsResourceData> mpls_data_map_;
};
#endif
