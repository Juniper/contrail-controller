#include "resource_mgr.h"
#include "mpls_resource_mgr.h"

const std::string MplsResourceManager::mpls_file_ = "/tmp/mpls.txt";
const std::string MplsResourceManager::mpls_tmp_file_ = "/tmp/mpls_tmp.txt";

void MplsResourceDataMap::Process(SandeshContext *context) {

}

MplsResourceManager::MplsResourceManager(
        const std::string& mpls_file, Agent *agent) : 
    ResourceManager(mpls_file, agent) {

}

void MplsResourceManager::AddResourceData(const ResourceReq &req) {
    MplsResourceData mplsdata;
    MplsData *rdata = static_cast< MplsData *>(req.resourcedata.get());
    MplsKey *rkey = static_cast <MplsKey *> (req.resourcekey.get()); 
    mplsdata.set_mpls_label(rdata->mpls_label);
    mplsdata.set_data(rdata->data);
    mpls_data_map_.insert(std::pair<uint32_t, MplsResourceData>(rkey->key, mplsdata));
    SetChanged(true);
    
}

void MplsResourceManager::DeleteResourceData(const ResourceReq &req) {
   MplsKey *rkey = static_cast <MplsKey *> (req.resourcekey.get()); 
   std::map<uint32_t, MplsResourceData>::iterator it = 
       mpls_data_map_.find(rkey->key);
   if (it != mpls_data_map_.end()) {
       mpls_data_map_.erase(it);
   }
   SetChanged(true);
}

void MplsResourceManager::ModifyResourceData(const ResourceReq& req) {
   MplsKey *rkey = static_cast <MplsKey *> (req.resourcekey.get()); 
   std::map<uint32_t, MplsResourceData>::iterator it = 
       mpls_data_map_.find(rkey->key);
   if (it != mpls_data_map_.end()) {
       MplsData *rdata = static_cast< MplsData *>(req.resourcedata.get());
       it->second.set_mpls_label(rdata->mpls_label);
       it->second.set_data(rdata->data);
   }
    SetChanged(true);
}

void MplsResourceManager::ReadResourceData(ResourceMap& rmap) {
    
    // clear the data before reading the Sandesh structure.
    // Marker needs to be used in future if required
    mpls_data_map_.clear();    
    ReadFromFile();
    std::map<uint32_t, MplsResourceData>::iterator it = mpls_data_map_.begin();
    while (it != mpls_data_map_.end()) {
        MplsKey * rkey = new MplsKey();
        MplsData *rdata = new MplsData();
        rkey->key = it->first;
        rdata->mpls_label = it->second.get_mpls_label();
        rdata->data = it->second.get_data(); 
        rmap.insert(std::pair<ResourceKey *, ResourceData *>(rkey, rdata));
        ++it;
    }
}

void MplsResourceManager::WriteToFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    MplsResourceDataMap rmap;
    rmap.set_mpls_map(mpls_data_map_);
    // this code needs to be checked for giving correct size
    uint32_t size = rmap.ToString().size();
    uint8_t *buf = new uint8_t[size];
    int error = 0;
    buff_size_ = rmap.WriteBinary(buf, size, &error);
    SaveResourceDataToFile(buf);
    CopyFileFromTempFile(mpls_file_);
    delete [] buf;
}

void MplsResourceManager::ReadFromFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    int error = 0;
    ReadResourceDataFromFile(mpls_file_);
    MplsResourceDataMap rmap;
    rmap.ReadBinary(read_buf_.get(), buff_size_, &error);
    mpls_data_map_ = rmap.get_mpls_map();
}

