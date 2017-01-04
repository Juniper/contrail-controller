#include "resource_manager.h"
#include "mpls_resource_manager.h"

const std::string MplsResourceManager::mpls_file_ = "/tmp/mpls.txt";
const std::string MplsResourceManager::mpls_tmp_file_ = "/tmp/mpls_tmp.txt";

void MplsResourceDataMap::Process(SandeshContext *context) {

}

MplsResourceManager::MplsResourceManager(
        const std::string& mpls_file, const std::string &mpls_tmp_file, 
        Agent *agent) :
    ResourceManager(mpls_file, mpls_tmp_file, agent) {

}

void MplsResourceManager::AddResourceData(const ResourceReq &req) {
    MplsResourceData mplsdata;
    MplsData *rdata = static_cast< MplsData *>(req.resourcedata.get());
    MplsKey *rkey = static_cast <MplsKey *> (req.resourcekey.get());
    mplsdata.set_mpls_label(rdata->mpls_label);
    mplsdata.set_data(rdata->data);
    mpls_data_map_.insert(std::pair<uint32_t, MplsResourceData>(
                rkey->mpls_resource_key, mplsdata));
    SetChanged(true);
}

void MplsResourceManager::DeleteResourceData(const ResourceReq &req) {
   MplsKey *rkey = static_cast <MplsKey *> (req.resourcekey.get());
   std::map<uint32_t, MplsResourceData>::iterator it =
       mpls_data_map_.find(rkey->mpls_resource_key);
   if (it != mpls_data_map_.end()) {
       mpls_data_map_.erase(it);
   }
   SetChanged(true);
}

void MplsResourceManager::ModifyResourceData(const ResourceReq& req) {
   MplsKey *rkey = static_cast <MplsKey *> (req.resourcekey.get());
   std::map<uint32_t, MplsResourceData>::iterator it =
       mpls_data_map_.find(rkey->mpls_resource_key);
   if (it != mpls_data_map_.end()) {
       MplsData *rdata = static_cast< MplsData *>(req.resourcedata.get());
       it->second.set_mpls_label(rdata->mpls_label);
       it->second.set_data(rdata->data);
       SetChanged(true);
   }
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
        rkey->mpls_resource_key = it->first;
        rdata->mpls_label = it->second.get_mpls_label();
        rdata->data = it->second.get_data();
        rmap.insert(std::pair<ResourceKey *, ResourceData *>(rkey, rdata));
        ++it;
    }
}

void MplsResourceManager::EnCode() {
    MplsResourceDataMap rmap;
    rmap.set_mpls_map(mpls_data_map_);
    // this code needs to be checked for giving correct size
    uint32_t size = rmap.ToString().size();
    write_buf_.reset(new uint8_t [size]);
    int error = 0;
    write_buff_size_ = rmap.WriteBinary(write_buf_.get(), size, &error);
}

void MplsResourceManager::DeCode() {
    int error = 0;
    MplsResourceDataMap rmap;
    rmap.ReadBinary(read_buf_.get(), read_buff_size_, &error);
    mpls_data_map_ = rmap.get_mpls_map();
}

