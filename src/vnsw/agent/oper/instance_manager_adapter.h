#ifndef __AGENT_OPER_INSTANCE_MANAGER_ADAPTER_H__
#define __AGENT_OPER_INSTANCE_MANAGER_ADAPTER_H__

#include <string>
#include <boost/uuid/uuid.hpp>
#include "oper/service_instance.h"
#include "oper/instance_task.h"

class InstanceManagerAdapter {
    public:
    enum CmdType {
        Start = 1,
        Stop
    };

    virtual InstanceTask* CreateStartTask(const ServiceInstance::Properties &props, bool update) = 0;

    virtual InstanceTask* CreateStopTask(const ServiceInstance::Properties &props) = 0;

    virtual bool isApplicable(const ServiceInstance::Properties &props) = 0;

    virtual ~InstanceManagerAdapter() { }
};

class DockerInstanceAdapter : public InstanceManagerAdapter {
    public:
    DockerInstanceAdapter(const std::string &docker_cmd,Agent *agent)
                        : docker_cmd_(docker_cmd), agent_(agent) {}

    InstanceTask* CreateStartTask(const ServiceInstance::Properties &props, bool update);

    InstanceTask* CreateStopTask(const ServiceInstance::Properties &props);

    bool isApplicable(const ServiceInstance::Properties &props);

    private:
    std::string docker_cmd_;
    Agent *agent_;
};

class NetNSInstanceAdapter : public InstanceManagerAdapter {
    public:
    NetNSInstanceAdapter(const std::string &netns_cmd,
                         const std::string &loadbalancer_config_path,
                         Agent *agent)
                         : netns_cmd_(netns_cmd),
                          loadbalancer_config_path_(loadbalancer_config_path),
                          agent_(agent)
                          {}

    InstanceTask* CreateStartTask(const ServiceInstance::Properties &props, bool update);

    InstanceTask* CreateStopTask(const ServiceInstance::Properties &props);

    bool isApplicable(const ServiceInstance::Properties &props);

    private:
    std::string netns_cmd_;
    std::string loadbalancer_config_path_;
    Agent *agent_;
};

#endif