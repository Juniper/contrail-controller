#include "oper/netns_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"
#include "agent.h"
#include "init/agent_param.h"

boost::uuids::uuid NetNSInstanceAdapter::PropertyToId
    (const ServiceInstance::Properties &props) const {
    boost::uuids::uuid lb_id = boost::uuids::nil_uuid();
    if (!props.pool_id.is_nil()) {
        lb_id = props.pool_id;
    } else if (!props.loadbalancer_id.is_nil()) {
        lb_id = props.loadbalancer_id;
    }
    return lb_id;
}

InstanceTask* NetNSInstanceAdapter::CreateStartTask(const ServiceInstance::Properties &props, bool update) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return NULL;
    }
    cmd_str << netns_cmd_ << " create";

    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(props.vmi_inside);
    cmd_str << " " << UuidToString(props.vmi_outside);

    if (props.ip_prefix_len_inside != -1)  {
        cmd_str << " --vmi-left-ip " << props.ip_addr_inside << "/";
        cmd_str << props.ip_prefix_len_inside;
    } else {
        cmd_str << " --vmi-left-ip 0.0.0.0/0";
    }
    cmd_str << " --vmi-right-ip " << props.ip_addr_outside << "/";
    cmd_str << props.ip_prefix_len_outside;

    if (!props.mac_addr_inside.empty()) {
        cmd_str << " --vmi-left-mac " << props.mac_addr_inside;
    } else {
        cmd_str << " --vmi-left-mac 00:00:00:00:00:00";
    }
    cmd_str << " --vmi-right-mac " << props.mac_addr_outside;
    cmd_str << " --gw-ip " << props.gw_ip;

    if (props.service_type == ServiceInstance::LoadBalancer) {
        boost::uuids::uuid lb_id = PropertyToId(props);
        cmd_str << " --cfg-file " << loadbalancer_config_path_ << lb_id
            << "/conf.json";
        cmd_str << " --pool-id " << props.pool_id;
        if (!agent_->params()->si_lb_keystone_auth_conf_path().empty()) {
            cmd_str << " --keystone-auth-cfg-file " <<
                agent_->params()->si_lb_keystone_auth_conf_path();
        }
    }

    if (update) {
        cmd_str << " --update";
    }

    return new InstanceTaskExecvp(cmd_str.str(), START, agent_->event_manager());
}

InstanceTask* NetNSInstanceAdapter::CreateStopTask(const ServiceInstance::Properties &props) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return NULL;
    }
    cmd_str << netns_cmd_ << " destroy";

    if (props.instance_id.is_nil() ||
        props.vmi_outside.is_nil()) {
        return NULL;
    }

    if (props.interface_count == 2 && props.vmi_inside.is_nil()) {
        return NULL;
    }

    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(props.vmi_inside);
    cmd_str << " " << UuidToString(props.vmi_outside);
    if (props.service_type == ServiceInstance::LoadBalancer) {
        boost::uuids::uuid lb_id = PropertyToId(props);
        cmd_str << " --cfg-file " << loadbalancer_config_path_ << lb_id
            << "/conf.json";
        cmd_str << " --pool-id " << props.pool_id;
    }

    return new InstanceTaskExecvp(cmd_str.str(), STOP, agent_->event_manager());
}

bool NetNSInstanceAdapter::isApplicable(const ServiceInstance::Properties &props) {
    return (props.virtualization_type == ServiceInstance::NetworkNamespace);
}
