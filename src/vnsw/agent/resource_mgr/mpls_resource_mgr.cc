#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "resource_mgr.h"
#include "mpls_resource_mgr.h"

const std::string MplsResourceManager::mpls_file_ = "/tmp/mpls.bin";

void MplsResourceDataList::Process(SandeshContext* context) {

}

MplsResourceManager::MplsResourceManager(
        const std::string& mpls_file) : ResourceManager(mpls_file){
}

void MplsResourceManager::AddResourceData(const ResourceData& req) {
    MplsResourceData mplsdata;
    MplsData data = static_cast<const MplsData&>(req);
    mplsdata.set_key(data.key);
    mplsdata.set_mpls_label(data.mpls_label);
    mplsdata.set_data(data.data);
    mpls_data_list_.push_back(mplsdata);
    AddKeytoIndexMaping(data.key, mpls_data_list_.size()-1); 
    SetChanged(true);
    
}

void MplsResourceManager::DeleteResourceData(const ResourceData& req) {
   int index = FindKeyToIndex((static_cast<const MplsData&>(req)).key);
   if (index == -1)
      return;
   // swap last element to current index pop the last element.
   uint32_t lastelement = mpls_data_list_.size()-1;
   mpls_data_list_[index].set_key(mpls_data_list_[lastelement].get_key());
   mpls_data_list_[index].set_mpls_label(mpls_data_list_[lastelement].get_mpls_label());
   mpls_data_list_[index].set_data(mpls_data_list_[lastelement].get_data());
   DeleteKey((static_cast<const MplsData&>(req)).key);
   AddKeytoIndexMaping(mpls_data_list_[index].get_key(), index); 
   mpls_data_list_.pop_back();
   SetChanged(true);
}

void MplsResourceManager::ModifyResourceData(const ResourceData& req) {
    MplsData mplsdata = static_cast<const MplsData&>(req);
    int index = FindKeyToIndex(mplsdata.key);
    if (index == -1)
        return;
   mpls_data_list_[index].set_key(mplsdata.key);
   mpls_data_list_[index].set_mpls_label(mplsdata.mpls_label);
   mpls_data_list_[index].set_data(mplsdata.data);
   SetChanged(true);
}

void MplsResourceManager::ReadResourceData(std::vector<ResourceData*> &rdata_list, 
                                      uint32_t &marker) {
    
    // clear the data before reading the Sandesh structure.
    // Marker needs to be used in future if required
    mpls_data_list_.clear();
    ReadFromFile();
    std::vector<MplsResourceData>::iterator it = mpls_data_list_.begin();
    int index = 0;
    for (; it != mpls_data_list_.end(); ++it) {
        MplsData *mplsdata = new MplsData();
        mplsdata->key = it->get_key();
        mplsdata->mpls_label = it->get_mpls_label();
        mplsdata->data = it->get_data();
        rdata_list.push_back(mplsdata);
        AddKeytoIndexMaping(mplsdata->key, index);
        index++;
    }
}

void MplsResourceManager::WriteToFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    uint8_t *buf = new uint8_t[KMplsMaxResourceSize];
    int buf_len = sizeof(buf);
    int error = 0;
    MplsResourceDataList mpls_list;
    mpls_list.set_mpls_list(mpls_data_list_);
    buff_size_ = mpls_list.WriteBinary(buf, buf_len, &error);
    SaveResourceDataToFile(buf);
    delete [] buf;
}

void MplsResourceManager::ReadFromFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    int error = 0;
    MplsResourceDataList mpls_list;
    ReadResourceDataFromFile();
    mpls_list.ReadBinary(read_buf_.get(), buff_size_, &error);
    mpls_data_list_ = mpls_list.get_mpls_list();  
    //delete [] read_buf_;
}

