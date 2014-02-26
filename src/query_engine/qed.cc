/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fstream>
#include <iostream>
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
using namespace boost::asio;
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

    CpuLoadInfo cpu_load_info;
    CpuLoadData::FillCpuInfo(cpu_load_info, false);

    ModuleCpuState state;
    state.set_name(hostname);

    ModuleCpuInfo cinfo;
    cinfo.set_module_id(Sandesh::module());
    cinfo.set_instance_id(Sandesh::instance_id());
    cinfo.set_cpu_info(cpu_load_info);
    vector<ModuleCpuInfo> cciv;
    cciv.push_back(cinfo);

    // At some point, the following attributes will be deprecated
    // in favor of AnalyticsCpuState
    state.set_module_cpu_info(cciv);
    state.set_queryengine_cpu_share(cpu_load_info.get_cpu_share());
    state.set_queryengine_mem_virt(cpu_load_info.get_meminfo().get_virt());

    ModuleCpuStateTrace::Send(state);

    AnalyticsCpuState  astate;
    astate.set_name(hostname);

    ProcessCpuInfo ainfo;
    ainfo.set_module_id(Sandesh::module());
    ainfo.set_inst_id(Sandesh::instance_id());
    ainfo.set_cpu_share(cpu_load_info.get_cpu_share());
    ainfo.set_mem_virt(cpu_load_info.get_meminfo().get_virt());
    vector<ProcessCpuInfo> aciv;
    aciv.push_back(ainfo);
    astate.set_cpu_info(aciv);
    AnalyticsCpuStateTrace::Send(astate);

    qe_info_log_timer->Cancel();
    qe_info_log_timer->Start(60*1000, boost::bind(&QEInfoLogTimer),
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

static void WaitForIdle() {
    static const int kTimeout = 15;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    for (int i = 0; i < (kTimeout * 1000); i++) {
        if (scheduler->IsEmpty()) {
            break;
        }
        usleep(1000);
    }    
}

uint64_t QueryEngine::anal_ttl = 0;

// Check whether a string is empty, used as std::remove_if predicate.
struct IsEmpty {
    bool operator()(const std::string &s) { return s.empty(); }
};

#define VAR_MAP_STR(var, val)                                      \
    do {                                                           \
        if (var_map.count(val) && var_map[val].as<string>() != "") \
            var = var_map[val].as<string>();                       \
    } while (false)

#define VAR_MAP_INT(var, val)                                      \
    do {                                                           \
        if (var_map.count(val) && var_map[val].as<int>() != 0)     \
            var = var_map[val].as<int>();                          \
    } while (false)
#define VAR_MAP_ULONG(var, val)                                          \
    do {                                                                 \
        if (var_map.count(val) && var_map[val].as<unsigned long>() != 0) \
            var = var_map[val].as<unsigned long>();                      \
    } while (false)

static int qed_main(int argc, char *argv[]) {
    error_code error;

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("127.0.0.1:9160");
    vector<string> collector_server_list;
    collector_server_list.push_back("127.0.0.1:8086");
    string discovery_server;
    uint16_t discovery_port = ContrailPorts::DiscoveryServerPort;
    string redis_ip = "127.0.0.1";
    uint16_t redis_port = ContrailPorts::RedisQueryPort;
    string hostname(boost::asio::ip::host_name(error));
    uint16_t http_server_port = ContrailPorts::HttpPortQueryEngine;
    bool log_local = false;
    string log_level = "SYS_DEBUG";
    string log_category = "";
    string log_file = "<stdout>";
    string config_file = "/etc/contrail/query-engine.conf";
    int max_slice = 100;
    int max_tasks = 16;
    uint64_t start_time = 0;
    int analytics_data_ttl = g_viz_constants.AnalyticsTTL;

    EventManager evm;
    pevm = &evm;
    while (gdbhelper==0) {
        usleep(1000);
    }
    // Command line only options.
    opt::options_description generic("Generic options");
    generic.add_options()
        ("conf-file", opt::value<string>()->default_value(config_file),
             "Configuration file")
        ("help", "help message")
        ("version", "Display version information")
    ;

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
        ("DEFAULTS.analytics-data-ttl",
           opt::value<int>()->default_value(analytics_data_ttl),
           "global TTL(hours) for analytics data")
        ("DEFAULTS.cassandra-server",
           opt::value<vector<string> >()->default_value(cassandra_server_list,
               "127.0.0.1:9160"),
           "cassandra server list")
        ("DEFAULTS.collector-server",
             opt::value<vector<string> >()->multitoken(
                 )->default_value(
                 vector<string>(1,"127.0.0.1:8086"), "127.0.0.1:8086"),
          "IP address:port of sandesh collectors")
        ("DEFAULTS.http-server-port",
            opt::value<uint16_t>()->default_value(http_server_port),
            "Sandesh HTTP listener port")
        ("DEFAULTS.max-slice",
            opt::value<int>()->default_value(max_slice),
            "Max number of rows in chunk slice")
        ("DEFAULTS.max-tasks",
            opt::value<int>()->default_value(max_tasks),
            "Max number of tasks used for a query")
        ("DEFAULTS.start-time",
            opt::value<uint64_t>()->default_value(start_time),
            "Lowest start time for queries")

        ("DISCOVERY.server",
           opt::value<string>()->default_value(discovery_server),
           "IP address of Discovery Server")
        ("DISCOVERY.port", opt::value<uint16_t>()->default_value(discovery_port),
           "Port of Discovery Server")

        ("REDIS.ip", opt::value<string>()->default_value(redis_ip),
           "redis server ip")
        ("REDIS.port", opt::value<uint16_t>()->default_value(redis_port),
           "redis server port")

        ("LOG.category", opt::value<string>()->default_value(log_category),
            "Category filter for local logging of sandesh messages")
        ("LOG.file", opt::value<string>()->default_value(log_file),
            "Filename for the logs to be written to")
        ("LOG.level", opt::value<string>()->default_value(log_level),
            "Severity level for local logging of sandesh messages")
        ("LOG.local", opt::bool_switch(&log_local),
             "Enable local logging of sandesh messages")

         ;

    opt::options_description config_file_options;
    config_file_options.add(config);

    opt::options_description cmdline_options("Allowed options");
    cmdline_options.add(generic).add(config);

    opt::variables_map var_map;

    // Process options off command line first.
    opt::store(opt::parse_command_line(argc, argv, cmdline_options), var_map);

    GetOptValue<string>(var_map, config_file, "conf-file", "");
    std::ifstream config_file_in;
    config_file_in.open(config_file.c_str());
    if (config_file_in.good()) {
        opt::store(opt::parse_config_file(config_file_in, config_file_options),
                   var_map);
    }
    config_file_in.close();

    opt::notify(var_map);

    if (var_map.count("help")) {
        std::cout << cmdline_options << std::endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info;
        QedVersion(build_info);
        std::cout << build_info << std::endl;
        exit(0);
    }

    GetOptValue<uint16_t>(var_map, discovery_port, "DISCOVERY.port", 0);
    GetOptValue<string>(var_map, discovery_server, "DISCOVERY.server", "");
    GetOptValue<uint16_t>(var_map, http_server_port, "DEFAULTS.http-server-port", 0);
    GetOptValue<int>(var_map, analytics_data_ttl, "DEFAULTS.analytics-data-ttl", 0);
    GetOptValue<int>(var_map, max_slice, "DEFAULTS.max-slice", 0);
    GetOptValue<int>(var_map, max_tasks, "DEFAULTS.max-tasks", 0);
    GetOptValue<uint64_t>(var_map, start_time, "DEFAULTS.start-time", 0);
    GetOptValue<string>(var_map, log_category, "LOG.category", "");
    GetOptValue<string>(var_map, log_file, "LOG.file", "");
    GetOptValue<string>(var_map, log_level, "LOG.level", "");
    GetOptValue<string>(var_map, redis_ip, "REDIS.ip", "");
    GetOptValue<uint16_t>(var_map, redis_port, "REDIS.port", 0);

    // Retrieve cassandra server list. Remove empty strings from it.
    if (var_map.count("DEFAULTS.cassandra-server")) {
        cassandra_server_list =
            var_map["DEFAULTS.cassandra-server"].as< vector<string> >();
        cassandra_server_list.erase(std::remove_if(
            cassandra_server_list.begin(), cassandra_server_list.end(),
                IsEmpty()),
            cassandra_server_list.end());

        if (cassandra_server_list.empty()) {
            cassandra_server_list.push_back("127.0.0.1:9160");
        }
    }

    if (log_file == "<stdout>") {
        LoggingInit();
    } else {
        LoggingInit(log_file);
    }
    QueryEngine::anal_ttl = analytics_data_ttl;

    string cassandra_server = cassandra_server_list[0];

    DiscoveryServiceClient *ds_client = NULL;
    ip::tcp::endpoint dss_ep;
    Sandesh::CollectorSubFn csf = 0;
    Module::type module = Module::QUERY_ENGINE;
    string module_name = g_vns_constants.ModuleNames.find(module)->second;

    if (!discovery_server.empty()) {
        dss_ep.address(boost::asio::ip::address::from_string(discovery_server,
                                                             error));
        if (error) {
            LOG(ERROR, __func__ << ": Invalid discovery-server: " << discovery_server);
        } else {
            dss_ep.port(discovery_port);
            string subscriber_name =
                g_vns_constants.ModuleNames.find(Module::QUERY_ENGINE)->second;
            ds_client = new DiscoveryServiceClient(&evm, dss_ep, 
                                                   subscriber_name);
            ds_client->Init();
            csf = boost::bind(&DiscoveryServiceClient::Subscribe, 
                              ds_client, _1, _2, _3);
        }
    }

    LOG(INFO, "http-server-port " << http_server_port);
    LOG(INFO, "Endpoint " << dss_ep);

    // Retrieve cassandra server list. Remove empty strings from it.
    if (var_map.count("DEFAULTS.collector-server")) {
        collector_server_list =
            var_map["DEFAULTS.collector-server"].as< vector<string> >();
        collector_server_list.erase(std::remove_if(
            collector_server_list.begin(), collector_server_list.end(),
                IsEmpty()),
            collector_server_list.end());

        if (collector_server_list.empty()) {
            collector_server_list.push_back("127.0.0.1:8086");
        }
    }
    // LOG(INFO, "Collectors " << collector_server_list);
    LOG(INFO, "Max-tasks " << max_tasks);
    LOG(INFO, "Max-slice " << max_slice);

    // Initialize Sandesh
    NodeType::type node_type = 
        g_vns_constants.Module2NodeType.find(module)->second;
    Sandesh::InitGenerator(
            module_name,
            hostname, 
            g_vns_constants.NodeTypeNames.find(node_type)->second,
            g_vns_constants.INSTANCE_ID_DEFAULT,
            &evm,
            http_server_port,
            csf,
            collector_server_list,
            NULL);
    Sandesh::SetLoggingParams(log_local, log_category, log_level, false);

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
            redis_ip,
            redis_port,
            max_tasks,
            max_slice);
    } else if (start_time) {
        qe = new QueryEngine(&evm,
            cassandra_ip,
            cassandra_port,
            redis_ip,
            redis_port,
            max_tasks,
            max_slice,
            start_time);
    } else {
        qe = new QueryEngine(&evm,
            cassandra_ip,
            cassandra_port,
            redis_ip,
            redis_port,
            max_tasks,
            max_slice);
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

    if (ds_client) {
        ds_client->Shutdown();
        delete ds_client;
    }
    qe_info_log_timer->Cancel();
    TimerManager::DeleteTimer(qe_info_log_timer);
    WaitForIdle();
    delete qe;
    Sandesh::Uninit();
    WaitForIdle();
    return 0;
}

int main(int argc, char *argv[]) {
    try {
        return qed_main(argc, argv);
    } catch (boost::program_options::error &e) {
        LOG(ERROR, "Error " << e.what());
        std::cout << "Error " << e.what() << std::endl;
    } catch (...) {
        LOG(ERROR, "Options Parser: Caught fatal unknown exception");
        std::cout << "Options Parser: Caught fatal unknown exception" << std::endl;
    }

    return(-1);
}
