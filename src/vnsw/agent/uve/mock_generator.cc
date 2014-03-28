//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <sys/types.h>
#include <unistd.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

#include <base/task.h>
#include <base/logging.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/common/vns_constants.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/flow_types.h>

namespace opt = boost::program_options;

class MockGenerator {
public:
    MockGenerator(std::string &hostname, std::string &module_name,
                  std::string &node_type_name,
                  std::string &instance_id,
                  int http_server_port, int start_vn, int end_vn, int other_vn,
                  int num_vns, int vm_iterations,
                  std::vector<std::string> &collectors,
                  std::vector<uint32_t> &ip_vns,
                  int ip_start_index, int num_flows_per_vm,
                  EventManager *evm) :
        hostname_(hostname),
        module_name_(module_name),
        node_type_name_(node_type_name),
        instance_id_(instance_id),
        http_server_port_(http_server_port),
        start_vn_(start_vn),
        end_vn_(end_vn),
        other_vn_(other_vn),
        num_vns_(num_vns),
        vm_iterations_(vm_iterations),
        collectors_(collectors),
        ip_vns_(ip_vns),
        ip_start_index_(ip_start_index),
        num_flows_per_vm_(num_flows_per_vm),
        rgen_(std::time(0)),
        u_rgen_(&rgen_),
        evm_(evm) {
    }

    bool Run() {
        // Initialize Sandesh
        Sandesh::CollectorSubFn csf = 0;
        Sandesh::InitGenerator(module_name_, hostname_,
            node_type_name_, instance_id_, evm_, http_server_port_,
            csf, collectors_, NULL);
        // Enqueue send flow task
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        SendFlowTask *ftask(new SendFlowTask(this,
            scheduler->GetTaskId("mockgen::SendFlowTask"), -1));
        scheduler->Enqueue(ftask);
        return true; 
    }

private:

    class SendFlowTask : public Task {
    public:
        SendFlowTask(MockGenerator *mock_generator,
            int task_id, int task_instance) :
            Task(task_id, task_instance),
            mgen_(mock_generator) {
        }
 
        bool Run() {
            // Populate flows if not done
            if (mgen_->flows_.empty()) {
                int other_vn = mgen_->other_vn_;
                for (int vn = mgen_->start_vn_; vn < mgen_->end_vn_; vn++) {
                    for (int nvm = 0; nvm < mgen_->vm_iterations_; nvm++) {
                        for (int nflow = 0; nflow < mgen_->num_flows_per_vm_;
                             nflow++) {
                            uint64_t init_packets(mgen_->dFlowPktsPerSec(
                                mgen_->rgen_));
                            uint64_t init_bytes(init_packets * 
                                mgen_->dBytesPerPacket(mgen_->rgen_));
                            uint32_t sourceip(mgen_->ip_vns_[vn] + 
                                mgen_->ip_start_index_ + nvm);
                            uint32_t destip(mgen_->ip_vns_[other_vn] +
                                mgen_->ip_start_index_ + nvm);
                            FlowDataIpv4 flow_data;
                            boost::uuids::uuid flowuuid(mgen_->u_rgen_());
                            flow_data.set_flowuuid(to_string(flowuuid));
                            flow_data.set_direction_ing(mgen_->dDirection(
                                mgen_->rgen_));
                            std::string sourcevn(mgen_->kVnPrefix +
                                integerToString(vn));
                            flow_data.set_sourcevn(sourcevn);
                            std::string destvn(mgen_->kVnPrefix +
                                integerToString(other_vn));
                            flow_data.set_destvn(destvn);
                            flow_data.set_sourceip(sourceip);
                            flow_data.set_destip(destip);
                            flow_data.set_sport(mgen_->dPort(mgen_->rgen_));
                            flow_data.set_dport(mgen_->dPort(mgen_->rgen_));
                            flow_data.set_protocol(mgen_->kProtocols[
                                mgen_->dProtocols(mgen_->rgen_)]);
                            flow_data.set_setup_time(UTCTimestampUsec());
                            flow_data.set_packets(init_packets);
                            flow_data.set_bytes(init_bytes);
                            flow_data.set_diff_packets(init_packets);
                            flow_data.set_diff_bytes(init_bytes);
                            mgen_->flows_.push_back(flow_data);
                        }
                    }
                    other_vn = (other_vn + 1) % mgen_->num_vns_;
                }
            } 
            // Send the flows periodically
            int lflow_cnt = 0;
            for (std::vector<FlowDataIpv4>::iterator it = mgen_->flows_.begin() +
                 mgen_->flow_counter_; it != mgen_->flows_.end(); ++it) {
                FlowDataIpv4 &flow_data(*it);
                uint64_t new_packets(mgen_->dFlowPktsPerSec(mgen_->rgen_));
                uint64_t new_bytes(new_packets *
                    mgen_->dBytesPerPacket(mgen_->rgen_));
                uint64_t old_packets(flow_data.get_packets());
                uint64_t old_bytes(flow_data.get_bytes());
                flow_data.set_packets(old_packets + new_packets);
                flow_data.set_bytes(old_bytes + new_bytes);
                flow_data.set_diff_packets(new_packets);
                flow_data.set_diff_bytes(new_bytes);
                FLOW_DATA_IPV4_OBJECT_SEND(flow_data);
                lflow_cnt++;
                mgen_->flow_counter_++;
                if (lflow_cnt == mgen_->kNumFlowsInIteration) {
                    return false;
                }
            }
            // Completed iteration, reset flow counter
            mgen_->flow_counter_ = 0;
            return false;
        }

    private:
        MockGenerator *mgen_;
    };

    const static std::string kVnPrefix;
    const static std::string kVmPrefix;
    const static int kBytesPerPacket = 1024;
    const static int kOtherVnPktsPerSec = 1000;
    const static int kUveMsgIntvlInSec = 10; 
    const static int kNumFlowsInIteration = 145 * 10;
    const static int kFlowMsgIntvlInSec = 1;
    const static int kFlowPktsPerSec = 100;

    const static boost::random::uniform_int_distribution<> 
        dBytesPerPacket;
    const static boost::random::uniform_int_distribution<>
        dOtherVnPktsPerSec;
    const static boost::random::uniform_int_distribution<>
        dFlowPktsPerSec;
    const static boost::random::uniform_int_distribution<>
        dDirection;
    const static boost::random::uniform_int_distribution<>
        dPort;
    const static std::vector<int> kProtocols;
    const static boost::random::uniform_int_distribution<>
        dProtocols;
    
    const std::string hostname_;
    const std::string module_name_;
    const std::string node_type_name_;
    const std::string instance_id_;
    const int http_server_port_;
    const int start_vn_;
    const int end_vn_;
    const int other_vn_;
    const int num_vns_;
    const int vm_iterations_;
    const std::vector<std::string> collectors_;
    const std::vector<uint32_t> ip_vns_;
    const int ip_start_index_;
    const int num_flows_per_vm_;
    std::vector<FlowDataIpv4> flows_;
    static int flow_counter_;
    boost::random::mt19937 rgen_;
    boost::uuids::random_generator u_rgen_;
    EventManager *evm_;

    friend class SendFlowTask;
};

const std::string MockGenerator::kVnPrefix("default-domain:mock-gen-test:vn");
const std::string MockGenerator::kVmPrefix("vm");
const boost::random::uniform_int_distribution<> 
    MockGenerator::dBytesPerPacket(1, MockGenerator::kBytesPerPacket); 
const boost::random::uniform_int_distribution<>
    MockGenerator::dOtherVnPktsPerSec(1, MockGenerator::kOtherVnPktsPerSec);
const boost::random::uniform_int_distribution<>
    MockGenerator::dFlowPktsPerSec(1, MockGenerator::kFlowPktsPerSec);
const boost::random::uniform_int_distribution<>
    MockGenerator::dDirection(0, 1);
const boost::random::uniform_int_distribution<>
    MockGenerator::dPort(0, 65535);
const std::vector<int> MockGenerator::kProtocols = boost::assign::list_of
    (6)(17)(1);
const boost::random::uniform_int_distribution<>
    MockGenerator::dProtocols(0, MockGenerator::kProtocols.size() - 1);
int MockGenerator::flow_counter_(0);

int main(int argc, char *argv[]) {
    opt::options_description desc("Command line options");
    desc.add_options()
        ("help", "help message")
        ("collectors", opt::value<std::vector<std::string> >()->multitoken(
            )->default_value(std::vector<std::string>(1, "127.0.0.1:8086"),
                             "127.0.0.1:8086"),
         "List of Collectors addresses in ip:port format")
        ("num_instances_per_generator", opt::value<int>()->default_value(10),
         "Number of instances (virtual machines) per generator")
        ("num_networks", opt::value<int>()->default_value(100),
         "Number of virtual networks")
        ("num_flows_per_instance", opt::value<int>()->default_value(10),
         "Number of flows per instance")
        ("start_ip_address",
         opt::value<std::string>()->default_value("1.0.0.1"),
         "Start IP address to be used for instances")
        ("http_server_port", opt::value<int>()->default_value(-1),
         "HTTP server port")
        ("generator_id", opt::value<int>()->default_value(0),
         "Generator Id")
        ("num_generators", opt::value<int>()->default_value(1),
         "Number of generators");

    opt::variables_map var_map;
    opt::store(opt::parse_command_line(argc, argv, desc), var_map);
    opt::notify(var_map);

    if (var_map.count("help")) {
        std::cout << desc << std::endl;
        exit(0);
    }

    LoggingInit();

    int gen_id(var_map["generator_id"].as<int>());
    int ngens(var_map["num_generators"].as<int>());
    int pid(getpid());
    int num_instances(var_map["num_instances_per_generator"].as<int>());
    int num_networks(var_map["num_networks"].as<int>());
    Module::type module(Module::VROUTER_AGENT);
    std::string moduleid(g_vns_constants.ModuleNames.find(module)->second);
    NodeType::type node_type(
        g_vns_constants.Module2NodeType.find(module)->second);
    std::string node_type_name(
        g_vns_constants.NodeTypeNames.find(node_type)->second);
    int http_server_port(var_map["http_server_port"].as<int>());
    std::vector<std::string> collectors(
        var_map["collectors"].as<std::vector<std::string> >());

    boost::system::error_code ec;
    std::string hostname(boost::asio::ip::host_name(ec));
    if (ec) {
        LOG(ERROR, "Hostname FAILED: " << ec);
        exit(1);
    }
    hostname += "-" + integerToString(pid);
    int gen_factor = num_networks / num_instances;
    if (gen_factor == 0) {
        LOG(ERROR, "Number of virtual networks(" << num_networks << ") should "
            "be greater than number of instances per generator(" << 
            num_instances << ")");
        exit(1);
    }
    int start_vn((gen_id % gen_factor) * num_instances);
    int end_vn(((gen_id % gen_factor) + 1) * num_instances);
    int other_vn_adj(num_networks / 2);
    int other_vn;
    if (gen_id >= other_vn_adj) {
        other_vn = gen_id - other_vn_adj;
    } else {
        other_vn = gen_id + other_vn_adj;
    }
    int instance_iterations((num_instances + num_networks - 1) / num_networks); 
    int num_ips_per_vn(((ngens * num_instances) + num_networks - 1) / 
            num_networks);
    std::string start_ip(var_map["start_ip_address"].as<std::string>());
    boost::asio::ip::address_v4 start_ip_address(
        boost::asio::ip::address_v4::from_string(start_ip.c_str(), ec));
    if (ec) {
        LOG(ERROR, "IP Address (" << start_ip << ") FAILED: " << ec);
        exit(1);
    }
    std::vector<uint32_t> ip_vns;
    for (int num = 0; num < num_networks; num++) {
        ip_vns.push_back(start_ip_address.to_ulong() +  
                         num_ips_per_vn * num);
    }
    int start_ip_index(gen_id * num_instances / num_networks);

    EventManager evm;
    int num_flows_per_instance(var_map["num_flows_per_instance"].as<int>());
    std::string instance_id(integerToString(gen_id));
    MockGenerator mock_generator(hostname, moduleid, node_type_name,
        instance_id, http_server_port, start_vn, end_vn, other_vn,
        num_networks, instance_iterations, collectors, ip_vns, start_ip_index,
        num_flows_per_instance, &evm);
    mock_generator.Run();
    evm.Run();
    return 0;
}
