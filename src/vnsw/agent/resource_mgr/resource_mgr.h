#ifndef agent_resource_mgr_h
#define agent_resource_mgr_h
#include <base/timer.h>
#include <agent.h>
#include <map>
#include <fstream>
#include <iostream>
class ResourceData {
    public:
        virtual ~ResourceData() { } 
};

class ResourceManager {
public:
  typedef uint32_t ResourceKey;
  typedef std::map<ResourceKey, int> KeyToIndexMap;
  ResourceManager(const std::string &file):file_name_(file)  { 
  }
  ResourceManager(){};
  virtual ~ResourceManager() {}
  virtual void AddResourceData(const ResourceData &req) = 0;
  virtual void ReadResourceData(std::vector<ResourceData*> &rdata_list,
                                uint32_t &marker) = 0;
  virtual void DeleteResourceData(const ResourceData &data) = 0;
  virtual void ModifyResourceData(const ResourceData &data) = 0;
  void SetChanged(bool changed) {changed_ = changed;}
  bool Changed() const {return changed_;}
  virtual void WriteToFile() = 0;
  virtual void ReadFromFile() = 0; 
  void AddKeytoIndexMaping(ResourceKey key, int index) {
       indexmap_.insert(std::make_pair(key, index));
  }

  void DeleteKey(ResourceKey key) {
     indexmap_.erase(key);
  }
  
  int FindKeyToIndex(ResourceKey key) {
    KeyToIndexMap::const_iterator it;
    it = indexmap_.find(key);
    if (it == indexmap_.end()) {
        return -1;
    }
    return it->second;
  }

  void SaveResourceDataToFile(uint8_t *buffer) {
    std::ofstream output(file_name_.c_str(), std::ofstream::binary);
    output.seekp(0);
    output.write((char *)buffer, buff_size_);
    output.close();
  }

  void ReadResourceDataFromFile() {
    std::ifstream input (file_name_.c_str(), std::ofstream::binary);
    input.seekg(0, input.end);
    std::streampos size = input.tellg();
    input.seekg(0, input.beg);
    read_buf_.reset(new uint8_t [size]);
    input.read((char *)read_buf_.get(), size);
    buff_size_ = uint32_t(size);
    input.close();
  }

protected:
  bool changed_;
  std::string file_name_;
  KeyToIndexMap indexmap_;
  uint32_t buff_size_;
  tbb::mutex mutex_;
  std::auto_ptr<uint8_t>read_buf_;
};

#endif
