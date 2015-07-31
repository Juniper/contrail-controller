#ifndef __SRC_OPER_NETNS_INSTANCE_ADAPTER_H
#define __SRC_OPER_NETNS_INSTANCE_ADAPTER_H
#include "oper/instance_manager_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"

class NetNSInstanceAdapter : public InstanceManagerAdapter {
 public:
    NetNSInstanceAdapter(const std::string &netns_cmd,
                         const std::string &loadbalancer_config_path,
                         Agent *agent)
                         : netns_cmd_(netns_cmd),
                          loadbalancer_config_path_(loadbalancer_config_path),
                          agent_(agent)
                          {}

    InstanceTask* CreateStartTask(const ServiceInstance::Properties &props,
        bool update);
    InstanceTask* CreateStopTask(const ServiceInstance::Properties &props);
    bool isApplicable(const ServiceInstance::Properties &props);
    void set_cmd(const std::string &netns_cmd) { netns_cmd_ = netns_cmd;}
 private:
    std::string netns_cmd_;
    std::string loadbalancer_config_path_;
    Agent *agent_;
};

#endif
