#include <fstream>
#include <boost/filesystem.hpp>
#include "oper/netns_instance_adapter.h"
#include "oper/service_instance.h"
#include "oper/instance_task.h"
#include "agent.h"
#include "init/agent_param.h"

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
        std::stringstream pathgen;
        boost::system::error_code error;
        boost::uuids::uuid lb_id = props.ToId();
        pathgen << loadbalancer_config_path_ << lb_id;
        boost::filesystem::path dir(pathgen.str());
        if (!boost::filesystem::exists(dir, error)) {
            boost::filesystem::create_directories(dir, error);
            if (error) {
                std::stringstream ss;
                ss << "CreateDirectory error for ";
                ss << UuidToString(lb_id) << " ";
                ss << error.message();
                LOG(ERROR, ss.str().c_str());
                return NULL;
            }
        }
        pathgen << "/haproxy.conf.new";
        const std::string &filename = pathgen.str();
        std::ofstream fs(filename.c_str());
        if (fs.fail()) {
            LOG(ERROR, "File create " << filename << ": " << strerror(errno));
            return NULL;
        }
        fs << "global\n\tdaemon\n\tuser nobody\n\tgroup nogroup\n\tlog /dev/log local0\n\tlog /dev/log local1 notice\n\ttune.ssl.default-dh-param 2048\n\tulimit-n 200000\n\tmaxconn 65000\n\tstats socket " << loadbalancer_config_path_ << lb_id << "/haproxy.sock mode 0666 level user\n\n" << "defaults\n\tlog global\n\tretries 3\n\toption redispatch\n\ttimeout connect 5000\n\ttimeout client 300000\n\ttimeout server 300000\n\nfrontend DEFAULT_FRONTEND\n\toption tcplog\n\tbind " << props.ip_addr_outside << ":80\n\tmode http\n\tdefault_backend DEFAULT_BACKEND\n\nbackend DEFAULT_BACKEND\n\tmode http\n\tbalance roundrobin\n\toption forwardfor\n";
        fs.close();
        cmd_str << " --cfg-file " << filename.c_str();
        cmd_str << props.IdToCmdLineStr();
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
        boost::uuids::uuid lb_id = props.ToId();
        cmd_str << " --cfg-file " << loadbalancer_config_path_ << lb_id
            << "/haproxy.conf";
        cmd_str << props.IdToCmdLineStr();
    }

    return new InstanceTaskExecvp("NetNS", cmd_str.str(), STOP,
                                  agent_->event_manager());
}

bool NetNSInstanceAdapter::isApplicable(const ServiceInstance::Properties &props) {
    return (props.virtualization_type == ServiceInstance::NetworkNamespace);
}
