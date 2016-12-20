#ifndef agent_resource_mgr_h
#define agent_resource_mgr_h
#include <base/timer.h>
#include <agent.h>
#include <map>
#include <fstream>
#include <iostream>
#include <base/timer.h>
struct  ResourceData {
    virtual ~ResourceData() { } 
};

struct ResourceKey  {
  uint32_t key;
};

struct ResourceReq  {
    std::auto_ptr<ResourceKey>resourcekey;
    std::auto_ptr<ResourceData> resourcedata;
};

class ResourceManager {
public:
  typedef std::map<ResourceKey *, ResourceData *> ResourceMap;
  static const uint32_t kResourceSyncTimeout = 1000;
  ResourceManager(const std::string &file, Agent *agent):file_name_(file), 
    agent_(agent)  {
    timer_ = TimerManager::CreateTimer(*(agent->event_manager()->io_service()),
                                       "sync resource to file");
    timer_->Start(kResourceSyncTimeout,
                  boost::bind(&ResourceManager::TimerExpiry, this));
  }
  ResourceManager(){};
  virtual ~ResourceManager() {
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
  }
  virtual void AddResourceData(const ResourceReq &req) = 0;
  virtual void ReadResourceData(ResourceMap &resource_map) = 0;
  virtual void DeleteResourceData(const ResourceReq &req) = 0;
  virtual void ModifyResourceData(const ResourceReq &req) = 0;
  void SetChanged(bool changed) {changed_ = changed;}
  bool Changed() const {return changed_;}
  virtual void WriteToFile() = 0;
  virtual void ReadFromFile() = 0; 
  void SaveResourceDataToFile(uint8_t *buffer) {
    std::ofstream output(file_name_.c_str(), std::ofstream::binary);
    output.seekp(0);
    output.write((char *)buffer, buff_size_);
    output.close();
  }

  void ReadResourceDataFromFile(const std::string &file) {
    std::ifstream input (file.c_str(), std::ofstream::binary);
    input.seekg(0, input.end);
    std::streampos size = input.tellg();
    input.seekg(0, input.beg);
    read_buf_.reset(new uint8_t [size]);
    input.read((char *)read_buf_.get(), size);
    buff_size_ = uint32_t(size);
    input.close();
  }

  void CopyFileFromTempFile(const std::string &outfile) {
     std::string copy ("cp "  + file_name_ + "  " + outfile);
     system(copy.c_str());
  }
  bool TimerExpiry() {
    if (changed_) {
       WriteToFile(); 
       changed_ = false;
    }
    return true;
  }

protected:
  bool changed_;
  std::string file_name_;
  uint32_t buff_size_;
  tbb::mutex mutex_;
  std::auto_ptr<uint8_t>read_buf_;
  Timer *timer_;
  Agent *agent_;
};

#endif
