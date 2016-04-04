#include <fstream>
#include <boost/filesystem.hpp>
#include "oper/netns_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"
#include "agent.h"
#include "init/agent_param.h"

#if 0
        cmd_str << " --cfg-file " << loadbalancer_config_path_ << "haproxy"
            << "." << lb_id << ".conf";
#endif

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
        cmd_str << props.IdToCmdLineStr();
        if (!agent_->params()->si_lb_keystone_auth_conf_path().empty()) {
            cmd_str << " --keystone-auth-cfg-file " <<
                agent_->params()->si_lb_keystone_auth_conf_path();
        }
        const std::vector<autogen::KeyValuePair> &kvps  = props.instance_kvps;
        bool haproxy_config = false;
        boost::uuids::uuid lb_id = props.ToId();
        std::vector<autogen::KeyValuePair>::const_iterator it;
        for (it = kvps.begin(); it != kvps.end(); ++it) {
            autogen::KeyValuePair kvp = *it;
            if (kvp.key == "haproxy_config") {
                haproxy_config = true;
                std::stringstream pathgen;
                pathgen << loadbalancer_config_path_ << "haproxy"
                    << "." << lb_id << ".conf";
                const std::string &filename = pathgen.str();
                std::ofstream fs(filename.c_str());
                if (fs.fail()) {
                    LOG(ERROR, "File create " << filename << ": " << strerror(errno));
                    return NULL;
                }
                fs << kvp.value;
                fs.close();
                cmd_str << " --cfg-file " << pathgen.str();
                break;
            }
        }
        if (haproxy_config == false) {
            LOG(ERROR, "haproxy_config error lb id: " << lb_id);
            return NULL;
        }
    }

    if (update) {
        cmd_str << " --update";
    }

    return new InstanceTaskExecvp("NetNS", cmd_str.str(), START,
                                  agent_->event_manager());
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
        cmd_str << props.IdToCmdLineStr();
    }

    return new InstanceTaskExecvp("NetNS", cmd_str.str(), STOP,
                                  agent_->event_manager());
}

bool NetNSInstanceAdapter::isApplicable(const ServiceInstance::Properties &props) {
    return (props.virtualization_type == ServiceInstance::NetworkNamespace);
}
