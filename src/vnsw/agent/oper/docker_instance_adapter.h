#ifndef __SRC_OPER_DOCKER_INSTANCE_ADAPTER_H
#define __SRC_OPER_DOCKER_INSTANCE_ADAPTER_H
#include "oper/instance_manager_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"

class DockerInstanceAdapter : public InstanceManagerAdapter {
 public:
    DockerInstanceAdapter(const std::string &docker_cmd,Agent *agent)
                        : docker_cmd_(docker_cmd), agent_(agent) {}

    InstanceTask* CreateStartTask(const ServiceInstance::Properties &props,
        bool update);
    InstanceTask* CreateStopTask(const ServiceInstance::Properties &props);
    bool isApplicable(const ServiceInstance::Properties &props);

 private:
    std::string docker_cmd_;
    Agent *agent_;
};

#endif
