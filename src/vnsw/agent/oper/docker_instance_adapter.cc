#include "oper/docker_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"


static void generate_cmd_for_intf_create(
        const ServiceInstance::Properties &props, const std::string& type,
        std::stringstream& cmd_str) {
    const ServiceInstance::InterfaceData *intf_data =
            props.GetIntfByType(type);
    if (intf_data == NULL)
        return;

    cmd_str << " --vmi-" << type << "-id " << UuidToString(
                   intf_data->vmi_uuid);

    if (intf_data->ip_prefix_len != -1 && !intf_data->ip_addr.empty()) {
        cmd_str << " --vmi-" << type << "-ip " << intf_data->ip_addr << "/";
        cmd_str << intf_data->ip_prefix_len;
    } else {
        cmd_str << " --vmi-" << type << "-ip 0.0.0.0/0";
    }
    if (!intf_data->mac_addr.empty()) {
        cmd_str << " --vmi-" << type << "-mac " << intf_data->mac_addr;
    } else {
        cmd_str << " --vmi-" << type << "-mac 00:00:00:00:00:00";
    }
}

static void generate_cmd_for_intf_delete(
        const ServiceInstance::Properties &props, const std::string& type,
        std::stringstream& cmd_str) {
    const ServiceInstance::InterfaceData *intf_data =
            props.GetIntfByType(type);

    if (intf_data == NULL)
        return;

    cmd_str << " --vmi-" << type << "-id " << UuidToString(
                   intf_data->vmi_uuid);
}

InstanceTask* DockerInstanceAdapter::CreateStartTask(
        const ServiceInstance::Properties &props, bool update) {
    if (docker_cmd_.length() == 0) {
        return NULL;
    }
    std::stringstream cmd_str;
    cmd_str << "sudo " << docker_cmd_ << " create";
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " --image " << props.image_name;

    generate_cmd_for_intf_create(props, "left", cmd_str);
    generate_cmd_for_intf_create(props, "right", cmd_str);
    generate_cmd_for_intf_create(props, "management", cmd_str);

    if (!props.instance_data.empty()) {
        cmd_str << " --instance-data " << props.instance_data;
    }

    if (update) {
        cmd_str << " --update";
    }


    return new InstanceTaskExecvp(cmd_str.str(), START,
                                  agent_->event_manager());
}

InstanceTask* DockerInstanceAdapter::CreateStopTask(
        const ServiceInstance::Properties &props) {
    if (docker_cmd_.length() == 0) {
        return NULL;
    }

    std::stringstream cmd_str;
    cmd_str << "sudo " << docker_cmd_ << " destroy";
    cmd_str << " " << UuidToString(props.instance_id);

    generate_cmd_for_intf_delete(props, "left", cmd_str);
    generate_cmd_for_intf_delete(props, "right", cmd_str);
    generate_cmd_for_intf_delete(props, "management", cmd_str);

    return new InstanceTaskExecvp(cmd_str.str(), STOP, agent_->event_manager());
}

bool DockerInstanceAdapter::isApplicable(
        const ServiceInstance::Properties &props) {
    return (props.virtualization_type == ServiceInstance::VRouterInstance
            && props.vrouter_instance_type == ServiceInstance::Docker);
}
