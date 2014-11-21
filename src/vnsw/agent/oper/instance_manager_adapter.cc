#include "oper/instance_manager_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"

InstanceTask* DockerInstanceAdapter::CreateStartTask(const ServiceInstance::Properties &props, bool update) {
    if (docker_cmd_.length() == 0) {
        return NULL;
    }
    std::stringstream cmd_str;
    cmd_str << "sudo " << docker_cmd_ << " create";
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " --image " << props.image_name;

    if (props.vmi_inside != boost::uuids::nil_uuid()) {
        cmd_str << " --vmi-left-id " << UuidToString(props.vmi_inside);
        if (props.ip_prefix_len_inside != -1 && !props.ip_addr_inside.empty()) {
            cmd_str << " --vmi-left-ip " << props.ip_addr_inside << "/";
            cmd_str << props.ip_prefix_len_inside;
        } else {
            cmd_str << " --vmi-left-ip 0.0.0.0/0";
        }
        if (!props.mac_addr_inside.empty()) {
            cmd_str << " --vmi-left-mac " << props.mac_addr_inside;
        } else {
            cmd_str << " --vmi-left-mac 00:00:00:00:00:00";
        }
    }
    if (props.vmi_outside != boost::uuids::nil_uuid()) {
        cmd_str << " --vmi-right-id " << UuidToString(props.vmi_outside);
        if (props.ip_prefix_len_outside != -1 && !props.ip_addr_outside.empty())  {
            cmd_str << " --vmi-right-ip " << props.ip_addr_outside << "/";
            cmd_str << props.ip_prefix_len_outside;
        } else {
            cmd_str << " --vmi-right-ip 0.0.0.0/0";
        }
        if (!props.mac_addr_outside.empty()) {
            cmd_str << " --vmi-right-mac " << props.mac_addr_outside;
        } else {
            cmd_str << " --vmi-right-mac 00:00:00:00:00:00";
        }
    }
    if (props.vmi_management != boost::uuids::nil_uuid()) {
        cmd_str << " --vmi-management-id " << UuidToString(props.vmi_management);
        if (props.ip_prefix_len_management != -1 && !props.ip_addr_management.empty())  {
            cmd_str << " --vmi-management-ip " << props.ip_addr_management << "/";
            cmd_str << props.ip_prefix_len_management;
        } else {
            cmd_str << " --vmi-management-ip 0.0.0.0/0";
        }
        if (!props.mac_addr_management.empty()) {
            cmd_str << " --vmi-management-mac " << props.mac_addr_management;
        } else {
            cmd_str << " --vmi-management-mac 00:00:00:00:00:00";
        }
    }

    if (!props.instance_data.empty()) {
        cmd_str << " --instance-data " << props.instance_data;
    }

    if (update) {
        cmd_str << " --update";
    }


    return new InstanceTask(cmd_str.str(), Start, agent_->event_manager());
}

InstanceTask* DockerInstanceAdapter::CreateStopTask(const ServiceInstance::Properties &props) {
    if (docker_cmd_.length() == 0) {
        return NULL;
    }

    std::stringstream cmd_str;
    cmd_str << "sudo " << docker_cmd_ << " destroy";
    cmd_str << " " << UuidToString(props.instance_id);

    if (props.vmi_inside != boost::uuids::nil_uuid()) {
        cmd_str << " --vmi-left-id " << UuidToString(props.vmi_inside);
    }
    if (props.vmi_outside != boost::uuids::nil_uuid()) {
        cmd_str << " --vmi-right-id " << UuidToString(props.vmi_outside);
    }
    if (props.vmi_management != boost::uuids::nil_uuid()) {
        cmd_str << " --vmi-management-id " << UuidToString(props.vmi_management);
    }
    return new InstanceTask(cmd_str.str(), Stop, agent_->event_manager());
}

bool DockerInstanceAdapter::isApplicable(const ServiceInstance::Properties &props) {
    return (props.virtualization_type == ServiceInstance::VRouterInstance
            && props.vrouter_instance_type == ServiceInstance::Docker);
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

    if (props.service_type == ServiceInstance::LoadBalancer) {
        cmd_str << " --cfg-file " << loadbalancer_config_path_ <<
            props.pool_id << "/etc/haproxy/haproxy.cfg";
        cmd_str << " --pool-id " << props.pool_id;
        cmd_str << " --gw-ip " << props.gw_ip;
    }

    if (update) {
        cmd_str << " --update";
    }

    return new InstanceTask(cmd_str.str(), Start, agent_->event_manager());
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
        cmd_str << " --cfg-file " << loadbalancer_config_path_ <<
            props.pool_id << "/etc/haproxy/haproxy.cfg";
        cmd_str << " --pool-id " << props.pool_id;
    }

    return new InstanceTask(cmd_str.str(), Stop, agent_->event_manager());
}

bool NetNSInstanceAdapter::isApplicable(const ServiceInstance::Properties &props) {
    return (props.virtualization_type == ServiceInstance::NetworkNamespace);
}