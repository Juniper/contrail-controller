#include "oper/netns_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"
#include "agent.h"
#include "init/agent_param.h"

InstanceTask* NetNSInstanceAdapter::CreateStartTask(
        const ServiceInstance::Properties &props, bool update) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return NULL;
    }
    cmd_str << netns_cmd_ << " create";

    const ServiceInstance::InterfaceData *intf_inside =
            props.GetIntfByType("left");
    const ServiceInstance::InterfaceData *intf_outside =
            props.GetIntfByType("right");

    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(intf_inside->vmi_uuid);
    cmd_str << " " << UuidToString(intf_outside->vmi_uuid);

    if (intf_inside->ip_prefix_len != -1)  {
        cmd_str << " --vmi-left-ip " << intf_inside->ip_addr << "/";
        cmd_str << intf_inside->ip_prefix_len;
    } else {
        cmd_str << " --vmi-left-ip 0.0.0.0/0";
    }
    cmd_str << " --vmi-right-ip " << intf_outside->ip_addr << "/";
    cmd_str << intf_outside->ip_prefix_len;

    if (!intf_inside->mac_addr.empty()) {
        cmd_str << " --vmi-left-mac " << intf_inside->mac_addr;
    } else {
        cmd_str << " --vmi-left-mac 00:00:00:00:00:00";
    }
    cmd_str << " --vmi-right-mac " << intf_outside->mac_addr;
    cmd_str << " --gw-ip " << props.gw_ip;

    if (props.service_type == ServiceInstance::LoadBalancer) {
        cmd_str << " --cfg-file " << loadbalancer_config_path_ <<
            props.pool_id << "/conf.json";
        cmd_str << " --pool-id " << props.pool_id;
        if (!agent_->params()->si_lb_keystone_auth_conf_path().empty()) {
            cmd_str << " --keystone-auth-cfg-file " <<
                agent_->params()->si_lb_keystone_auth_conf_path();
        }
    }

    if (update) {
        cmd_str << " --update";
    }

    return new InstanceTaskExecvp(cmd_str.str(), START,
                                  agent_->event_manager());
}

InstanceTask* NetNSInstanceAdapter::CreateStopTask(
        const ServiceInstance::Properties &props) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return NULL;
    }
    cmd_str << netns_cmd_ << " destroy";

    const ServiceInstance::InterfaceData *intf_inside =
            props.GetIntfByType("left");
    const ServiceInstance::InterfaceData *intf_outside =
            props.GetIntfByType("right");

    if (props.instance_id.is_nil() ||
        intf_outside == NULL || intf_outside->vmi_uuid.is_nil()) {
        return NULL;
    }

    if (props.interfaces.size() == 2 &&
        (intf_inside == NULL || intf_inside->vmi_uuid.is_nil())) {
        return NULL;
    }

    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(intf_inside->vmi_uuid);
    cmd_str << " " << UuidToString(intf_outside->vmi_uuid);
    if (props.service_type == ServiceInstance::LoadBalancer) {
        cmd_str << " --cfg-file " << loadbalancer_config_path_ <<
            props.pool_id << "/conf.json";
        cmd_str << " --pool-id " << props.pool_id;
    }

    return new InstanceTaskExecvp(cmd_str.str(), STOP, agent_->event_manager());
}

bool NetNSInstanceAdapter::isApplicable(const ServiceInstance::Properties &props) {
    return (props.virtualization_type == ServiceInstance::NetworkNamespace);
}
