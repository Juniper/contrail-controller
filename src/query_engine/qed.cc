/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fstream>
#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include "base/logging.h"
#include "base/cpuinfo.h"
#include "base/task.h"
#include "base/task_trigger.h"
#include "base/timer.h"
#include "io/event_manager.h"
#include "QEOpServerProxy.h"
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include "analytics_cpuinfo_types.h"
#include "query.h"
#include <base/misc_utils.h>
#include <query_engine/buildinfo.h>
#include <sandesh/sandesh_http.h>
using std::auto_ptr;
using std::string;
using std::vector;
using std::map;
using std::vector;
using boost::assign::list_of;
using boost::system::error_code;
namespace opt = boost::program_options;
// This is to force qed to wait for a gdbattach
// before proceeding.
// It will make it easier to debug qed during systest
volatile int gdbhelper = 1;

TaskTrigger *qe_info_trigger;
Timer *qe_info_log_timer;

bool QEInfoLogTimer() {
    qe_info_trigger->Set();
    return false;
}

bool QEInfoLogger(const string &hostname) {

    ModuleCpuState state;
    state.set_name(hostname);

    ModuleCpuInfo cinfo;
    cinfo.set_module_id(
            g_vns_constants.ModuleNames.find(Module::QUERY_ENGINE)->second);

    CpuLoadInfo cpu_load_info;
    CpuLoadData::FillCpuInfo(cpu_load_info, false);
    cinfo.set_cpu_info(cpu_load_info);

    vector<ModuleCpuInfo> cciv;
    cciv.push_back(cinfo);
    state.set_module_cpu_info(cciv);
    state.set_queryengine_cpu_share(cpu_load_info.get_cpu_share());
    state.set_queryengine_mem_virt(cpu_load_info.get_meminfo().get_virt());

    ModuleCpuStateTrace::Send(state);

    qe_info_log_timer->Cancel();
    qe_info_log_timer->Start(30*1000, boost::bind(&QEInfoLogTimer),
                               NULL);
    return true;
}

bool QedVersion(std::string &version) {
    return MiscUtils::GetBuildInfo(MiscUtils::Analytics, BuildInfo, version);
}

#include <csignal>

static EventManager * pevm = NULL;
static void terminate_qe (int param)
{
  pevm->Shutdown();
}

int
main(int argc, char *argv[]) {
    EventManager evm;
    pevm = &evm;
    bool enable_local_logging = false;
    const string default_log_file = "<stdout>";
    opt::options_description desc("Command line options");
    while (gdbhelper==0) {
        usleep(1000);
    }
    desc.add_options()
        ("help", "help message")
        ("cassandra-server-list",
         opt::value<vector<string> >()->default_value(
                 vector<string>(), "127.0.0.1:9160"),
         "cassandra server list")
        ("redis-ip",
         opt::value<string>()->default_value("127.0.0.1"),
         "redis server ip")
        ("redis-port",
         opt::value<int>()->default_value(6379),
         "redis server port")
        ("http-server-port",
         opt::value<int>()->default_value(ContrailPorts::HttpPortQueryEngine),
         "Sandesh HTTP listener port")
        ("log-local", opt::bool_switch(&enable_local_logging),
         "Enable local logging of sandesh messages")
        ("log-level", opt::value<string>()->default_value("SYS_NOTICE"),
         "Severity level for local logging of sandesh messages")
        ("log-category", opt::value<string>()->default_value(""),
         "Category filter for local logging of sandesh messages")
        ("collectors", opt::value<vector<string> >()->multitoken(
                )->default_value(
                vector<string>(1,"127.0.0.1:8086"), "127.0.0.1:8086"),
         "IP address:port of sandesh collectors")
        ("discovery-server",
         opt::value<string>(),
         "Discovery Server IP Addr")
        ("discovery-port",
         opt::value<int>()->default_value(ContrailPorts::DiscoveryServerPort),
         "Discovery Server port")
        ("log-file", opt::value<string>()->default_value(default_log_file),
         "Filename for the logs to be written to")
        ("version", "Display version information")
        ;
    opt::variables_map var_map;
    opt::store(opt::parse_command_line(argc, argv, desc), var_map);
    opt::notify(var_map);

    if (var_map.count("help")) {
        std::cout << desc << std::endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info;
        QedVersion(build_info);
        std::cout << build_info << std::endl;
        exit(0);
    }

    if (var_map["log-file"].as<string>() == default_log_file) {
        LoggingInit();
    } else {
        LoggingInit(var_map["log-file"].as<string>());
    }

    error_code error;
    string hostname(boost::asio::ip::host_name(error));
    DiscoveryServiceClient *ds_client = NULL;
    boost::asio::ip::tcp::endpoint dss_ep;
    Sandesh::CollectorSubFn csf = 0;
    if (var_map.count("discovery-server")) {
        error_code error;
        dss_ep.address(boost::asio::ip::address::from_string(var_map["discovery-server"].as<string>(),
                       error));
        if (error) {
            LOG(ERROR, __func__ << ": Invalid discovery-server: " << 
                    var_map["discovery-server"].as<string>());
        } else {
            dss_ep.port(var_map["discovery-port"].as<int>());
            ds_client = new DiscoveryServiceClient(&evm, dss_ep);
            ds_client->Init();
            string subscriber_name = g_vns_constants.ModuleNames.find(Module::QUERY_ENGINE)->second;
            csf = boost::bind(&DiscoveryServiceClient::Subscribe, ds_client, subscriber_name, _1, _2, _3);
        }
    }

    LOG(INFO, "http-server-port " << var_map["http-server-port"].as<int>());
    LOG(INFO, "Endpoint " << dss_ep);
    LOG(INFO, "Collectors " << var_map.count("collectors"));

    // Initialize Sandesh
    Sandesh::InitGenerator(
            g_vns_constants.ModuleNames.find(Module::QUERY_ENGINE)->second,
            hostname, &evm,
            var_map["http-server-port"].as<int>(),
            csf,
            var_map["collectors"].as<vector<string> >(),
            NULL);
    Sandesh::SetLoggingParams(enable_local_logging,
                              var_map["log-category"].as<string>(),
                              var_map["log-level"].as<string>(),
                              false);

    vector<string> cassandra_server_list(
            var_map["cassandra-server-list"].as<vector<string> >());
    string cassandra_server;
    if (cassandra_server_list.empty()) {
        cassandra_server = "127.0.0.1:9160";
    } else {
        cassandra_server = cassandra_server_list[0];
    }
    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep(":");
    tokenizer tokens(cassandra_server, sep);
    tokenizer::iterator it = tokens.begin();
    std::string cassandra_ip(*it);
    ++it;
    std::string port(*it);
    int cassandra_port;
    stringToInteger(port, cassandra_port);

    QueryEngine *qe;
    if (cassandra_port == 0) {
        qe = new QueryEngine(&evm,
            var_map["redis-ip"].as<string>(),
            var_map["redis-port"].as<int>());
    } else { 
        qe = new QueryEngine(&evm,
            cassandra_ip,
            cassandra_port,
            var_map["redis-ip"].as<string>(),
            var_map["redis-port"].as<int>());
    }
    (void) qe;

    CpuLoadData::Init();
    qe_info_trigger =
        new TaskTrigger(boost::bind(&QEInfoLogger, hostname),
                    TaskScheduler::GetInstance()->GetTaskId("qe::Stats"), 0);
    qe_info_log_timer = TimerManager::CreateTimer(*evm.io_service(),
                                                    "Collector Info log timer");
    qe_info_log_timer->Start(5*1000, boost::bind(&QEInfoLogTimer), NULL);    
    signal(SIGTERM,terminate_qe);
    evm.Run();

#if 0 
    if (ds_client){
        ds_client->Shutdown();
        delete ds_client;
    }
#endif
    qe_info_log_timer->Cancel();
    TimerManager::DeleteTimer(qe_info_log_timer);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    while (!scheduler->IsEmpty()) {
        usleep(1000);
    }
    delete qe;
    Sandesh::Uninit();
    usleep(1000000);
    return 0;
}
