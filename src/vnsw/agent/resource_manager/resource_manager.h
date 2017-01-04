#ifndef agent_resource_manager_h
#define agent_resource_manager_h
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
    virtual ~ResourceKey() { } 
};

struct ResourceReq  {
    std::auto_ptr<ResourceKey>resourcekey;
    std::auto_ptr<ResourceData> resourcedata;
};

class ResourceManager {
public:
  typedef std::map<ResourceKey *, ResourceData *> ResourceMap;
  static const uint32_t kResourceSyncTimeout = 1000;
  ResourceManager(const std::string &file, const std::string &tmp_file, 
          Agent *agent):file_name_(file),file_tmp_name_(tmp_file),
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
  virtual void EnCode() = 0;
  virtual void DeCode() = 0;
  
  virtual void WriteToFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    EnCode();
    SaveResourceDataToFile(write_buf_.get());
    write_buf_.reset();
  };

  virtual void ReadFromFile() {
    tbb::mutex::scoped_lock lock(mutex_);
    ReadResourceDataFromFile();
    DeCode();
    read_buf_.reset(); 
  };

protected:
  void SaveResourceDataToFile(const uint8_t *buffer) {
    std::ofstream output;
    output.open(file_tmp_name_.c_str(), std::ofstream::binary | std::ofstream::trunc);
    if (!output.good()) {
        output.close();
        return; 
    }

    output.write((char *)buffer, write_buff_size_);
    output.flush();
    output.close();
    // Copy the File to main file
    std::string copy ("cp "  + file_tmp_name_ + "  " + file_name_);
    system(copy.c_str());
  }

  void ReadResourceDataFromFile() {
    std::ifstream input (file_name_.c_str(), std::ofstream::binary);
    if (!input.good()) {
        input.close();
        return;
    }

    input.seekg(0, input.end);
    std::streampos size = input.tellg();
    input.seekg(0, input.beg);
    read_buf_.reset(new uint8_t [size]);
    input.read((char *)read_buf_.get(), size);
    read_buff_size_ = uint32_t(size);
    input.close();
  }

  bool TimerExpiry() {
    if (changed_) {
       WriteToFile();
       changed_ = false;
    }
    return true;
  }

  bool changed_;
  std::string file_name_;
  std::string file_tmp_name_;
  tbb::mutex mutex_;
  std::auto_ptr<uint8_t>read_buf_;
  std::auto_ptr<uint8_t>write_buf_;
  uint32_t read_buff_size_;
  uint32_t write_buff_size_;
  Timer *timer_;
  Agent *agent_;
};

#endif
