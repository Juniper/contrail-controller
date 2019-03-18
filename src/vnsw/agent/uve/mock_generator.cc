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
#include <io/event_manager.h>
#include <base/address.h>
#include <base/task.h>
#include <base/logging.h>
#include <base/timer.h>
#include <sandesh/common/vns_constants.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/flow_types.h>
#include <ksync/ksync_types.h>

namespace opt = boost::program_options;

typedef std::map<SessionIpPortProtocol, SessionAggInfo> SessionAggMap;

class MockGenerator {
public:
    const static int kNumVRouterErrorMessagesPerSec;
    const static int kNumSessionSamplesPerSec;
    const static int kNumSessionSamplesInMessage;

    MockGenerator(std::string &hostname, std::string &module_name,
                  std::string &node_type_name,
                  std::string &instance_id,
                  int http_server_port, int start_vn, int end_vn, int other_vn,
                  int num_vns, int vm_iterations,
                  std::vector<std::string> &collectors,
                  std::vector<uint32_t> &ip_vns,
                  int ip_start_index,
                  int num_vrouter_error_messages_per_sec,
                  int num_sessions_per_vm,
                  int num_session_samples_per_sec,
                  int num_session_samples_in_message,
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
        num_session_per_vm_(num_sessions_per_vm),
        num_session_samples_per_sec_(num_session_samples_per_sec),
        num_session_samples_in_message_(num_session_samples_in_message),
        num_vrouter_error_messages_per_sec_(num_vrouter_error_messages_per_sec),
        rgen_(std::time(0)),
        u_rgen_(&rgen_),
        evm_(evm) {
    }

    bool Run() {
        // Initialize Sandesh
        Sandesh::InitGenerator(module_name_, hostname_,
            node_type_name_, instance_id_, evm_, http_server_port_,
            collectors_, NULL);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        if (num_session_samples_per_sec_) {
            SendSessionTask *stask(new SendSessionTask(this,
                scheduler->GetTaskId("mockgen::SendSessionTask"), -1));
            scheduler->Enqueue(stask);
        }
        if (num_vrouter_error_messages_per_sec_) {
            SendMessageTask *mtask(new SendMessageTask(this,
                scheduler->GetTaskId("mockgen::SendMessageTask"), -1));
            scheduler->Enqueue(mtask);
        }
        return true;
    }

private:

    class SendMessageTask : public Task {
    public:
        SendMessageTask(MockGenerator *mock_generator,
            int task_id, int task_instance) :
            Task(task_id, task_instance),
            mgen_(mock_generator) {
        }

        void SendVRouterError() {
            std::string str1("VRouter operation failed. Error <");
            uint32_t error(2);
            std::string str2(":");
            std::string error_msg("Entry not pressent");
            std::string str3(">. Object <");
            std::string obj_str("Flow: 333333 with Source IP: "
                "not present >. Object < Flow: 333333 with Source IP: "
                "10.0.0.6 Source Port: 3333 Destination IP: 10.0.0.10 "
                "Destination Port: 13333 Protocol 6");
            std::string str4(">. Operation <");
            std::string state_str("Change");
            std::string str5(">. Message number :");
            uint64_t msg_no(418940931);
            V_ROUTER_ERROR_LOG("", SandeshLevel::SYS_DEBUG, str1, error,
                str2, error_msg, str3, obj_str, str4, state_str, str5,
                msg_no);
        }

        bool Run() {
            uint64_t diff_time(0);
            for (int i = 0; i < mgen_->num_vrouter_error_messages_per_sec_;
                i++) {
                uint64_t stime(UTCTimestampUsec());
                SendVRouterError();
                diff_time += UTCTimestampUsec() - stime;
                if (diff_time >= 1000000) {
                    LOG(ERROR, "Sent: " << i + 1 << " in " <<
                        diff_time/1000000 << " seconds, NOT sending at " <<
                        mgen_->num_vrouter_error_messages_per_sec_ << " rate");
                    return false;
                }
            }
            usleep(1000000 - diff_time);
            return false;
        }

        std::string Description() const {
            return "SendMessageTask";
        }

    private:
        MockGenerator *mgen_;
    };


    class SendSessionTask : public Task {
    public:
        SendSessionTask(MockGenerator *mock_generator,
            int task_id, int task_instance) :
            Task(task_id, task_instance),
            mgen_(mock_generator) {
        }

        bool Run() {
            if (mgen_->sessions_.empty()) {
                int other_vn = mgen_->other_vn_;
                for (int vn = mgen_->start_vn_; vn < mgen_->end_vn_; vn++) {
                    for (int nvm = 0; nvm < mgen_->vm_iterations_; nvm++) {
                        for (int nsession = 0; nsession < mgen_->num_session_per_vm_;
                                nsession++) {
                            SessionEndpoint end_point;
                            end_point.set_vmi(to_string(mgen_->u_rgen_()));
                            end_point.set_vn(mgen_->kVnPrefix +
                                integerToString(vn));
                            end_point.set_remote_vn(mgen_->kVnPrefix +
                                integerToString(other_vn));
                            end_point.set_is_client_session(mgen_->dClientSession(
                                mgen_->rgen_));
                            end_point.set_deployment(
                                mgen_->kDeployment[mgen_->dTagIdx(mgen_->rgen_)]);
                            end_point.set_tier(mgen_->kTier[mgen_->dTagIdx(
                                mgen_->rgen_)]);
                            end_point.set_application(
                                mgen_->kApplication[mgen_->dTagIdx(mgen_->rgen_)]);
                            end_point.set_site(mgen_->kSite[mgen_->dTagIdx(
                                mgen_->rgen_)]);
                            end_point.set_remote_deployment(
                                mgen_->kDeployment[mgen_->dTagIdx(mgen_->rgen_)]);
                            end_point.set_remote_tier(
                                mgen_->kTier[mgen_->dTagIdx(mgen_->rgen_)]);
                            end_point.set_remote_application(
                                mgen_->kApplication[mgen_->dTagIdx(mgen_->rgen_)]);
                            end_point.set_remote_site(mgen_->kSite[mgen_->dTagIdx(
                                mgen_->rgen_)]);
                            end_point.set_is_si(mgen_->dClientSession(mgen_->rgen_));
                            std::vector<std::string> labels;
                            std::vector<std::string> remote_labels;
                            int nlabels(mgen_->dLabels(mgen_->rgen_));
                            int nremote_labels(mgen_->dLabels(mgen_->rgen_));
                            for (int i = 0; i < nlabels + 1; i++) {
                                labels.push_back(
                                    mgen_->kLabels[mgen_->dLabels(mgen_->rgen_)]);
                            }
                            for (int i = 0; i < nremote_labels + 1; i++) {
                                remote_labels.push_back(
                                    mgen_->kLabels[mgen_->dLabels(mgen_->rgen_)]);
                            }
                            end_point.set_labels(
                                std::set<string>(labels.begin(),labels.end()));
                            end_point.set_remote_labels(
                                std::set<string>(remote_labels.begin(),remote_labels.end()));
                            SessionAggMap sess_agg_map;
                            int nsport = mgen_->dNPorts(mgen_->rgen_);
                            for (int i = 0; i < nsport; i++) {
                                IpAddress ipaddr(Ip4Address(mgen_->ip_vns_[vn] +
                                    mgen_->ip_start_index_ + nvm));
                                uint16_t protoIdx(mgen_->dProtocols(mgen_->rgen_));
                                uint16_t port = mgen_->kPorts[protoIdx];
                                uint16_t proto = mgen_->kProtocols[protoIdx];
                                SessionIpPortProtocol sess_ip_port_proto;
                                sess_ip_port_proto.set_local_ip(ipaddr);
                                sess_ip_port_proto.set_service_port(port);
                                sess_ip_port_proto.set_protocol(proto);
                                SessionAggInfo place_holder;
                                sess_agg_map[sess_ip_port_proto];
                            }
                            end_point.set_sess_agg_info(sess_agg_map);
                            mgen_->sessions_.push_back(end_point);
                        }
                    }
                other_vn = (other_vn + 1) % mgen_->num_vns_;
                }
            }

            int lsession_cnt = 0;
            int last_lsession_cnt = 0;
            uint64_t diff_time = 0;
            std::vector<SessionEndpoint>::iterator begin(mgen_->sessions_.begin() +
                mgen_->session_counter_);
            for (std::vector<SessionEndpoint>::iterator it = begin;
                it != mgen_->sessions_.end(); ++it) {
                bool sent_message(false);
                uint64_t stime = UTCTimestampUsec();
                SessionEndpoint &end_point(*it);
                SessionAggMap sess_agg_info_map;
                for (SessionAggMap::const_iterator it2
                    = end_point.get_sess_agg_info().begin();
                    it2 != end_point.get_sess_agg_info().end(); ++it2) {
                    SessionAggInfo sess_agg_info;
                    std::map<SessionIpPort, SessionInfo> session_map;
                    int ncport = mgen_->dNPorts(mgen_->rgen_);
                    for (int i = 0; i < ncport; i++) {
                        uint16_t cport(mgen_->dPort(mgen_->rgen_));
                        int nips = mgen_->dIps(mgen_->rgen_);
                        for (int j = 0; j < nips; j++) {
                            int other_vn;
                            stringToInteger(end_point.get_remote_vn()
                                .substr(MockGenerator::kVnPrefix.length(),
                                    std::string::npos), other_vn);
                            IpAddress ipaddr(Ip4Address(mgen_->ip_vns_[other_vn]
                                + mgen_->ip_start_index_ + j));
                            SessionIpPort sess_ip_port;
                            sess_ip_port.set_port(cport);
                            sess_ip_port.set_ip(ipaddr);
                            std::map<SessionIpPort, SessionInfo>::iterator iter
                                = session_map.find(sess_ip_port);
                            if (iter != session_map.end()) {
                                continue;
                            }
                            SessionInfo session_val;
                            SessionFlowInfo forward_flow_info;
                            SessionFlowInfo reverse_flow_info;
                            uint64_t forward_pkts(mgen_->dFlowPktsPerSec(
                                mgen_->rgen_));
                            uint64_t reverse_pkts(mgen_->dFlowPktsPerSec(
                                mgen_->rgen_));
                            // Send once in every 5 message as a logged message
                            if (j % 5 !=0 ) {
                                forward_flow_info.set_sampled_pkts(forward_pkts);
                                forward_flow_info.set_sampled_bytes(forward_pkts *
                                    mgen_->dBytesPerPacket(mgen_->rgen_));
                                reverse_flow_info.set_sampled_pkts(reverse_pkts);
                                reverse_flow_info.set_sampled_bytes(reverse_pkts *
                                    mgen_->dBytesPerPacket(mgen_->rgen_));
                                sess_agg_info.set_sampled_forward_pkts(
                                    sess_agg_info.get_sampled_forward_pkts() +
                                    forward_pkts);
                                sess_agg_info.set_sampled_forward_bytes(
                                    sess_agg_info.get_sampled_forward_bytes() +
                                    forward_flow_info.get_sampled_bytes());
                                sess_agg_info.set_sampled_reverse_pkts(
                                    sess_agg_info.get_sampled_reverse_pkts() +
                                    reverse_pkts);
                                sess_agg_info.set_sampled_reverse_bytes(
                                    sess_agg_info.get_sampled_reverse_bytes() +
                                    reverse_flow_info.get_sampled_bytes());
                            } else {
                                forward_flow_info.set_logged_pkts(forward_pkts);
                                forward_flow_info.set_logged_bytes(forward_pkts *
                                    mgen_->dBytesPerPacket(mgen_->rgen_));
                                reverse_flow_info.set_logged_pkts(reverse_pkts);
                                reverse_flow_info.set_logged_bytes(reverse_pkts *
                                    mgen_->dBytesPerPacket(mgen_->rgen_));
                                sess_agg_info.set_logged_forward_pkts(
                                    sess_agg_info.get_logged_forward_pkts() +
                                    forward_pkts);
                                sess_agg_info.set_logged_forward_bytes(
                                    sess_agg_info.get_logged_forward_bytes() +
                                    forward_flow_info.get_logged_bytes());
                                sess_agg_info.set_logged_reverse_pkts(
                                    sess_agg_info.get_logged_reverse_pkts() +
                                    reverse_pkts);
                                sess_agg_info.set_logged_reverse_bytes(
                                    sess_agg_info.get_logged_reverse_bytes() +
                                    reverse_flow_info.get_logged_bytes());
                            }
                            session_val.set_forward_flow_info(forward_flow_info);
                            session_val.set_reverse_flow_info(reverse_flow_info);
                            session_map[sess_ip_port] = session_val;
                        }
                    }
                    sess_agg_info.set_sessionMap(session_map);
                    sess_agg_info_map[it2->first] = sess_agg_info;
                }
                end_point.set_sess_agg_info(sess_agg_info_map);
                lsession_cnt++;
                mgen_->session_counter_++;
                SESSION_ENDPOINT_OBJECT_LOG("", SandeshLevel::SYS_NOTICE,
                    std::vector<SessionEndpoint>(begin+last_lsession_cnt, it + 1));
                sent_message = true;
                last_lsession_cnt = lsession_cnt;
                if (lsession_cnt == mgen_->num_session_samples_per_sec_) {
                    if (!sent_message) {
                        SESSION_ENDPOINT_OBJECT_LOG("", SandeshLevel::SYS_NOTICE,
                            std::vector<SessionEndpoint>(begin+last_lsession_cnt, it + 1));
                    }
                    diff_time += UTCTimestampUsec() - stime;
                    usleep(1000000 - diff_time);
                    return false;
                }
                diff_time += UTCTimestampUsec() - stime;
                if (diff_time >= 1000000) {
                    if (lsession_cnt < mgen_->num_session_samples_per_sec_) {
                        LOG(ERROR, "Sent: " << lsession_cnt << " in " <<
                            diff_time/1000000 << " seconds, NOT sending at " <<
                            mgen_->num_session_samples_per_sec_ << " rate");
                        return false;
                    }
                }

            }
            mgen_->session_counter_ = 0;
            return false;
        }

        std::string Description() const {
            return "SendSessionTask";
        }
    private:
        MockGenerator *mgen_;
    };

    const static std::string kVnPrefix;
    const static std::string kVmPrefix;
    const static int kBytesPerPacket = 1024;
    const static int kOtherVnPktsPerSec = 1000;
    const static int kUveMsgIntvlInSec = 10;
    const static int kFlowMsgIntvlInSec = 1;
    const static int kFlowPktsPerSec = 100;
    const static int kMaxIps = 64;
    const static int kMaxPorts = 5;

    const static boost::random::uniform_int_distribution<>
        dBytesPerPacket;
    const static boost::random::uniform_int_distribution<>
        dOtherVnPktsPerSec;
    const static boost::random::uniform_int_distribution<>
        dFlowPktsPerSec;
    const static boost::random::uniform_int_distribution<>
        dDirection;
    const static boost::random::uniform_int_distribution<>
        dClientSession;
    const static boost::random::uniform_int_distribution<>
        dPort;
    const static boost::random::uniform_int_distribution<>
        dIps;
    const static boost::random::uniform_int_distribution<>
        dNPorts;
    const static boost::random::uniform_int_distribution<>
        dLabels;
    const static std::vector<int> kProtocols;
    const static boost::random::uniform_int_distribution<>
        dProtocols;
    const static boost::random::uniform_int_distribution<>
        dTagIdx;
    const static std::vector<string> kLabels;
    const static std::vector<std::string> kDeployment;
    const static std::vector<std::string> kTier;
    const static std::vector<std::string> kSite;
    const static std::vector<std::string> kApplication;
    const static std::vector<int> kPorts;
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
    const int num_session_per_vm_;
    const int num_session_samples_per_sec_;
    const int num_session_samples_in_message_;
    const int num_vrouter_error_messages_per_sec_;
    std::vector<SessionEndpoint> sessions_;
    static int session_counter_;
    boost::random::mt19937 rgen_;
    boost::uuids::random_generator u_rgen_;
    EventManager *evm_;

    friend class SendMessageTask;
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
    MockGenerator::dClientSession(0, 1);
const boost::random::uniform_int_distribution<>
    MockGenerator::dPort(0, 65535);
const std::vector<int> MockGenerator::kProtocols = boost::assign::list_of
    (6)(17)(1);
const boost::random::uniform_int_distribution<>
    MockGenerator::dProtocols(0, MockGenerator::kProtocols.size() - 1);
const std::vector<int> MockGenerator::kPorts = boost::assign::list_of
    (443)(8080)(22);
const std::vector<std::string> MockGenerator::kDeployment = boost::assign::list_of
    ("Dep1")("Dep2")("Dep3")("Dep4");
const std::vector<std::string> MockGenerator::kTier = boost::assign::list_of
    ("Tier1")("Tier2")("Tier3")("Tier4");
const std::vector<std::string> MockGenerator::kApplication = boost::assign::list_of
    ("App1")("App2")("App3")("App4");
const std::vector<std::string> MockGenerator::kSite = boost::assign::list_of
    ("Site1")("Site2")("Site3")("Site4");
const std::vector<std::string> MockGenerator::kLabels = boost::assign::list_of
    ("Label1")("Label2")("Label3")("Label4")("Label5");
const boost::random::uniform_int_distribution<>
    MockGenerator::dTagIdx(0, MockGenerator::kDeployment.size() - 1);
const boost::random::uniform_int_distribution<>
    MockGenerator::dIps(1, MockGenerator::kMaxIps);
const boost::random::uniform_int_distribution<>
    MockGenerator::dNPorts(1, MockGenerator::kMaxPorts);
const boost::random::uniform_int_distribution<>
    MockGenerator::dLabels(0, MockGenerator::kLabels.size() - 1);

int MockGenerator::session_counter_(0);
const int MockGenerator::kNumVRouterErrorMessagesPerSec(50);
const int MockGenerator::kNumSessionSamplesPerSec(0);
const int MockGenerator::kNumSessionSamplesInMessage(0);

int main(int argc, char *argv[]) {
    bool log_local(false), use_syslog(false), log_flow(false);
    std::string log_category;
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
        ("num_sessions_per_instance", opt::value<int>()->default_value(10),
         "Number of sessions per instance")
        ("start_ip_address",
         opt::value<std::string>()->default_value("1.0.0.1"),
         "Start IP address to be used for instances")
        ("http_server_port", opt::value<int>()->default_value(-1),
         "HTTP server port")
        ("generator_id", opt::value<int>()->default_value(0),
         "Generator Id")
        ("num_generators", opt::value<int>()->default_value(1),
         "Number of generators")
        ("num_vrouter_errors_per_second", opt::value<int>()->default_value(
            MockGenerator::kNumVRouterErrorMessagesPerSec),
         "Number of VRouterErrror messages to send in one second")
        ("num_session_samples_per_second", opt::value<int>()->default_value(
            MockGenerator::kNumSessionSamplesPerSec),
         "Number of session messages to send in one second")
        ("num_session_samples_in_message", opt::value<int>()->default_value(
            MockGenerator::kNumSessionSamplesInMessage),
         "Number of session samples to send in one message")
        ("log_property_file", opt::value<std::string>()->default_value(""),
            "log4cplus property file name")
        ("log_files_count", opt::value<int>()->default_value(10),
            "Maximum log file roll over index")
        ("log_file_size",
            opt::value<long>()->default_value(10*1024*1024),
            "Maximum size of the log file")
        ("log_category",
            opt::value<std::string>()->default_value(log_category),
            "Category filter for local logging of sandesh messages")
        ("log_file", opt::value<std::string>()->default_value("<stdout>"),
            "Filename for the logs to be written to")
        ("log_level", opt::value<std::string>()->default_value("SYS_NOTICE"),
            "Severity level for local logging of sandesh messages")
        ("log_local", opt::bool_switch(&log_local),
            "Enable local logging of sandesh messages")
        ("use_syslog", opt::bool_switch(&use_syslog),
            "Enable logging to syslog")
        ("syslog_facility", opt::value<std::string>()->default_value(
            "LOG_LOCAL0"), "Syslog facility to receive log lines")
        ("log_flow", opt::bool_switch(&log_flow),
            "Enable local logging of flow sandesh messages")
        ("slo_destination", opt::value<std::vector<std::string> >()->multitoken(
            )->default_value(std::vector<std::string>(1, "collector"),
                             "collector"),
            "List of destinations. valid values are collector, file, syslog")
        ("sampled_destination", opt::value<std::vector<std::string> >()->multitoken(
            )->default_value(std::vector<std::string>(1, "collector"),
                             "collector"),
            "List of destinations. valid values are collector, file, syslog");

    opt::variables_map var_map;
    opt::store(opt::parse_command_line(argc, argv, desc), var_map);
    opt::notify(var_map);

    if (var_map.count("help")) {
        std::cout << desc << std::endl;
        exit(0);
    }

    Module::type module(Module::VROUTER_AGENT);
    std::string moduleid(g_vns_constants.ModuleNames.find(module)->second);
    std::string log_property_file(
        var_map["log_property_file"].as<std::string>());
    if (log_property_file.size()) {
        LoggingInit(log_property_file);
    } else {
        LoggingInit(var_map["log_file"].as<std::string>(),
                    var_map["log_file_size"].as<long>(),
                    var_map["log_files_count"].as<int>(),
                    use_syslog,
                    var_map["syslog_facility"].as<std::string>(),
                    moduleid,
                    SandeshLevelTolog4Level(Sandesh::StringToLevel(
                        var_map["log_level"].as<std::string>())));
    }
    Sandesh::SetLoggingParams(log_local,
        var_map["log_category"].as<std::string>(),
        var_map["log_level"].as<std::string>(), false, log_flow);

    std::vector<std::string> slo_destination(
        var_map["slo_destination"].as<std::vector<std::string> >());
    std::vector<std::string> sample_destination(
        var_map["sampled_destination"].as<std::vector<std::string> >());
    Sandesh::set_logger_appender(var_map["log_file"].as<std::string>(),
                                     var_map["log_file_size"].as<long>(),
                                     var_map["log_files_count"].as<int>(),
                                     var_map["syslog_facility"].as<std::string>(),
                                     slo_destination,
                                     moduleid, false);

    Sandesh::set_logger_appender(var_map["log_file"].as<std::string>(),
                                     var_map["log_file_size"].as<long>(),
                                     var_map["log_files_count"].as<int>(),
                                     var_map["syslog_facility"].as<std::string>(),
                                     sample_destination,
                                     moduleid, true);

    Sandesh::set_send_to_collector_flags(sample_destination, slo_destination);

    int gen_id(var_map["generator_id"].as<int>());
    int ngens(var_map["num_generators"].as<int>());
    int pid(getpid());
    int num_instances(var_map["num_instances_per_generator"].as<int>());
    int num_networks(var_map["num_networks"].as<int>());
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
    int num_sessions_per_instance(var_map["num_sessions_per_instance"].as<int>());
    int num_session_samples_per_sec(
        var_map["num_session_samples_per_second"].as<int>());
    int num_session_samples_in_message(
        var_map["num_session_samples_in_message"].as<int>());
    int num_vrouter_error_messages_per_sec(
        var_map["num_vrouter_errors_per_second"].as<int>());
    std::string instance_id(integerToString(gen_id));
    MockGenerator mock_generator(hostname, moduleid, node_type_name,
        instance_id, http_server_port, start_vn, end_vn, other_vn,
        num_networks, instance_iterations, collectors, ip_vns, start_ip_index,
        num_vrouter_error_messages_per_sec,
        num_sessions_per_instance, num_session_samples_per_sec,
        num_session_samples_in_message, &evm);
    mock_generator.Run();
    evm.Run();
    return 0;
}
