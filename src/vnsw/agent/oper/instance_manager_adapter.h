#ifndef __AGENT_OPER_INSTANCE_MANAGER_ADAPTER_H__
#define __AGENT_OPER_INSTANCE_MANAGER_ADAPTER_H__

#include <string>
#include <boost/uuid/uuid.hpp>
#include "oper/service_instance.h"
#include "oper/instance_task.h"

class InstanceManagerAdapter {
 public:
    enum CmdType {
        START = 1,
        STOP
    };

    virtual ~InstanceManagerAdapter() {}

    virtual InstanceTask* CreateStartTask(
        const ServiceInstance::Properties &props, bool update) = 0;
    virtual InstanceTask* CreateStopTask(
        const ServiceInstance::Properties &props) = 0;
    virtual bool isApplicable(const ServiceInstance::Properties &props) = 0;
};

#endif
