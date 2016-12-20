#ifndef agent_mpls_resource_mgr_h
#define agent_mpls_resource_mgr_h

#include "resource_mgr.h"
#include <vector>
#include <string>
#include "mpls_resource_data_types.h"

class MplsData : public ResourceData {
public:
   uint64_t key;
   uint32_t mpls_label; 
   std::string data;
};


class MplsResourceManager : public ResourceManager {
public:
    static const std::string mpls_file_; 
    static const uint32_t KMplsMaxResourceSize = 1000;
    MplsResourceManager(const std::string &);
    ~MplsResourceManager() { }
    MplsResourceManager() { } 
    virtual void AddResourceData(const ResourceData& );
    virtual void ReadResourceData(std::vector<ResourceData*> &rdata_list, 
                                  uint32_t &marker);
    virtual void DeleteResourceData(const ResourceData& ); 
    virtual void ModifyResourceData(const ResourceData &);
    virtual void WriteToFile();
    virtual void ReadFromFile();
private:
    // Sandesh Genrated structure.
    std::vector<MplsResourceData> mpls_data_list_; 
};
#endif
