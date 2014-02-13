/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include "base/cpuinfo.h"
#include "boost/python.hpp"
#include "base/logging.h"
#include "base/contrail_ports.h"
#include "base/task.h"
#include "base/task_trigger.h"
#include "base/timer.h"
#include "io/event_manager.h"
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include "gendb_if.h"
#include "viz_collector.h"
#include "viz_sandesh.h"
#include "ruleeng.h"
#include "viz_types.h"
#include "analytics_cpuinfo_types.h"
#include "generator.h"
#include "Thrift.h"
#include <base/misc_utils.h>
#include <analytics/buildinfo.h>
#include <discovery/client/discovery_client.h>
#include "boost/python.hpp"

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;

static TaskTrigger *collector_info_trigger;
static Timer *collector_info_log_timer;
static EventManager evm;

bool CollectorInfoLogTimer() {
    collector_info_trigger->Set();
    return false;
}

bool CollectorVersion(string &version) {
    return MiscUtils::GetBuildInfo(MiscUtils::Analytics, BuildInfo, version);
}

bool CollectorCPULogger(const string & hostname) {

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
    state.set_collector_cpu_share(cpu_load_info.get_cpu_share());
    state.set_collector_mem_virt(cpu_load_info.get_meminfo().get_virt());
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

    return true;
}

bool CollectorSummaryLogger(Collector *collector, const string & hostname,
        OpServerProxy * osp) {
    CollectorState state;
    static bool first = true, build_info_set = false;

    state.set_name(hostname);
    if (first) {
        vector<string> ip_list;
        ip_list.push_back(Collector::GetSelfIp());
        state.set_self_ip_list(ip_list);
        vector<string> list;
        MiscUtils::GetCoreFileList(Collector::GetProgramName(), list);
        if (list.size()) {
            state.set_core_files_list(list);
        }
        first = false;
    }
    if (!build_info_set) {
        string build_info_str;
        build_info_set = CollectorVersion(build_info_str);
        state.set_build_info(build_info_str);
    }

    std::vector<GeneratorSummaryInfo> infos;
    collector->GetGeneratorSummaryInfo(infos);

    state.set_generator_infos(infos);

    // Get socket stats
    TcpServerSocketStats rx_stats;
    collector->GetRxSocketStats(rx_stats);
    state.set_rx_socket_stats(rx_stats);
    TcpServerSocketStats tx_stats;
    collector->GetTxSocketStats(tx_stats);
    state.set_tx_socket_stats(tx_stats);

    CollectorInfo::Send(state);
    return true;
}

bool CollectorInfoLogger(VizSandeshContext &ctx) {
    VizCollector *analytics = ctx.Analytics();

    CollectorCPULogger(analytics->name());
    CollectorSummaryLogger(analytics->GetCollector(), analytics->name(),
            analytics->GetOsp());

    vector<ModuleServerState> sinfos;    
    analytics->GetCollector()->GetGeneratorSandeshStatsInfo(sinfos);

    for (uint i =0 ; i< sinfos.size(); i++) {
        SandeshModuleServerTrace::Send(sinfos[i]);
    }

    vector<SandeshMessageStat> sminfos;
    analytics->GetCollector()->GetSandeshStats(sminfos);

    for (uint i =0 ; i< sminfos.size(); i++) {
        SandeshMessageTrace::Send(sminfos[i]);
    }

    collector_info_log_timer->Cancel();
    collector_info_log_timer->Start(60*1000, boost::bind(&CollectorInfoLogTimer),
                               NULL);
    return true;
}

// Trigger graceful shutdown of collector process.
//
// IO (evm) is shutdown first. Afterwards, main() resumes, shutting down rest of the
// objects, and eventually exit()s.
void CollectorShutdown() {
    static bool shutdown_;

    if (shutdown_) return;
    shutdown_ = true;

    // Shutdown event manager first to stop all IO activities.
    evm.Shutdown();
}

static void terminate(int param) {
    CollectorShutdown();
}

static void ShutdownDiscoveryClient(DiscoveryServiceClient *client) {
    if (client) {
        client->Shutdown();
        delete client;
    }
}

// Shutdown various objects used in the collector.
static void ShutdownServers(VizCollector *viz_collector,
        DiscoveryServiceClient *client) {
    // Shutdown discovery client first
    ShutdownDiscoveryClient(client);

    viz_collector->Shutdown();

    TimerManager::DeleteTimer(collector_info_log_timer);
    delete collector_info_trigger;

    VizCollector::WaitForIdle();
    Sandesh::Uninit();
    VizCollector::WaitForIdle();
}

// Check whether a string is empty, used as std::remove_if predicate.
struct IsEmpty {
    bool operator()(const std::string &s) { return s.empty(); }
};

// This is to force vizd to wait for a gdbattach
// before proceeding.
// It will make it easier to debug vizd during systest
volatile int gdbhelper = 1;

static int analytics_main(int argc, char *argv[]) {
    boost::system::error_code error;

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("127.0.0.1:9160");
    int analytics_data_ttl = g_viz_constants.AnalyticsTTL;
    string discovery_server;
    uint16_t discovery_port = ContrailPorts::DiscoveryServerPort;
    string redis_ip = "127.0.0.1";
    uint16_t redis_port = ContrailPorts::RedisUvePort;
    uint16_t listen_port = ContrailPorts::CollectorPort;
    uint16_t syslog_port = ContrailPorts::SyslogPort;
    string hostname(host_name(error));
    string hostip = GetHostIp(evm.io_service(), hostname);
    uint16_t http_server_port = ContrailPorts::HttpPortCollector;
    bool dup = false;
    bool log_local = true;
    string log_level = "SYS_DEBUG";
    string log_category = "";
    string log_file = "<stdout>";
    string config_file = "/etc/contrail/collector.conf";

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
           "global TTL(days) for analytics data")
        ("DEFAULTS.cassandra-server",
           opt::value<vector<string> >()->default_value(cassandra_server_list,
               "127.0.0.1:9160"),
           "cassandra server list")
        ("DEFAULTS.dup", opt::bool_switch(&dup), "Internal use")
        ("DEFAULTS.hostip", opt::value<string>()->default_value(hostip),
           "IP address of Analytics Node")
        ("DEFAULTS.listen-port", opt::value<uint16_t>()->default_value(listen_port),
           "Collector listener port")
        ("DEFAULTS.http-server-port",
            opt::value<uint16_t>()->default_value(http_server_port),
            "Sandesh HTTP listener port")

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
        ("LOG.listen-port", opt::value<uint16_t>()->default_value(syslog_port),
           "Syslog listener port")
         ;

    opt::options_description config_file_options;
    config_file_options.add(config);

    opt::options_description cmdline_options("Allowed options");
    cmdline_options.add(generic).add(config);

    opt::variables_map var_map;
 
    // Process options off command line first.
    opt::store(opt::parse_command_line(argc, argv, cmdline_options), var_map);
    GetOptValue<string>(var_map, config_file, "conf-file", "");
 
    ifstream config_file_in;
    config_file_in.open(config_file.c_str());
    if (config_file_in.good()) {
        opt::store(opt::parse_config_file(config_file_in, config_file_options),
                   var_map);
    }
    config_file_in.close();
 
    opt::notify(var_map);

    if (var_map.count("help")) {
        cout << cmdline_options << endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info_str;
        CollectorVersion(build_info_str);
        cout << build_info_str << endl;
        exit(0);
    }

    GetOptValue<string>(var_map, hostip, "DEFAULTS.hostip", "");
    GetOptValue<uint16_t>(var_map, http_server_port, "DEFAULTS.http-server-port", 0);
    GetOptValue<uint16_t>(var_map, discovery_port, "DISCOVERY.port", 0);
    GetOptValue<string>(var_map, discovery_server, "DISCOVERY.server", "");
    GetOptValue<string>(var_map, log_category, "LOG.category", "");
    GetOptValue<string>(var_map, log_file, "LOG.file", "");
    GetOptValue<string>(var_map, log_level, "LOG.level", "");
    GetOptValue<uint16_t>(var_map, syslog_port, "LOG.listen-port", 0);
    GetOptValue<int>(var_map, analytics_data_ttl, "DEFAULTS.analytics-data-ttl", 0);
    GetOptValue<string>(var_map, redis_ip, "REDIS.ip", "");
    GetOptValue<uint16_t>(var_map, redis_port, "REDIS.port", 0);
    GetOptValue<uint16_t>(var_map, listen_port, "DEFAULTS.listen-port", 0);

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

    Collector::SetProgramName(argv[0]);
    if (log_file == "<stdout>") {
        LoggingInit();
    } else {
        LoggingInit(log_file);
    }

    string cassandra_server = cassandra_server_list[0];

    typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
    boost::char_separator<char> sep(":");
    tokenizer tokens(cassandra_server, sep);
    tokenizer::iterator it = tokens.begin();
    std::string cassandra_ip(*it);
    ++it;
    std::string port(*it);
    int cassandra_port;
    stringToInteger(port, cassandra_port);

    LOG(INFO, "COLLECTOR LISTEN PORT: " << listen_port);
    LOG(INFO, "COLLECTOR REDIS SERVER: " << redis_ip);
    LOG(INFO, "COLLECTOR REDIS PORT: " << redis_port);
    LOG(INFO, "COLLECTOR LOG LISTEN PORT: " << syslog_port);
    LOG(INFO, "COLLECTOR CASSANDRA SERVER: " << cassandra_ip);
    LOG(INFO, "COLLECTOR CASSANDRA PORT: " << cassandra_port);

    VizCollector analytics(&evm,
            listen_port,
            cassandra_ip,
            cassandra_port,
            redis_ip,
            redis_port,
            syslog_port,
            dup,
            analytics_data_ttl);

#if 0
    // initialize python/c++ API
    Py_InitializeEx(0);
    // insert the patch where scripts are placed
    // temporary it is env variable RULEENGPATH
    char *rpath = getenv("RULEENGPATH");
    if (rpath != NULL) {
        PyObject* sysPath = PySys_GetObject((char*)"path");
        PyList_Insert(sysPath, 0, PyString_FromString(rpath));
    }
#endif

    analytics.Init();

    VizSandeshContext vsc(&analytics);
    Module::type module = Module::COLLECTOR;
    NodeType::type node_type =
        g_vns_constants.Module2NodeType.find(module)->second;
    Sandesh::InitCollector(
            g_vns_constants.ModuleNames.find(module)->second,
            analytics.name(),
            g_vns_constants.NodeTypeNames.find(node_type)->second,
            g_vns_constants.INSTANCE_ID_DEFAULT, 
            &evm, "127.0.0.1",
            listen_port,
            http_server_port,
            &vsc);
    Sandesh::SetLoggingParams(log_local, log_category, log_level);

    //Publish services to Discovery Service Servee
    DiscoveryServiceClient *ds_client = NULL;
    if (!discovery_server.empty()) {
        tcp::endpoint dss_ep;
        dss_ep.address(address::from_string(discovery_server, error));
        dss_ep.port(discovery_port);
        string sname = 
            g_vns_constants.ModuleNames.find(Module::COLLECTOR)->second;
        ds_client = new DiscoveryServiceClient(&evm, dss_ep, sname);
        ds_client->Init();

        // Get local ip address
        Collector::SetSelfIp(hostip);
        stringstream pub_ss;
        pub_ss << "<" << sname << "><ip-address>" << hostip <<
                  "</ip-address><port>" << listen_port <<
                  "</port></" << sname << ">";
        std::string pub_msg;
        pub_msg = pub_ss.str();
        ds_client->Publish(DiscoveryServiceClient::CollectorService, pub_msg);
    }
             
    CpuLoadData::Init();
    collector_info_trigger =
        new TaskTrigger(boost::bind(&CollectorInfoLogger, vsc),
                    TaskScheduler::GetInstance()->GetTaskId("vizd::Stats"), 0);
    collector_info_log_timer = TimerManager::CreateTimer(*evm.io_service(),
                                                    "Collector Info log timer");
    collector_info_log_timer->Start(5*1000, boost::bind(&CollectorInfoLogTimer), NULL);
    signal(SIGTERM, terminate);
    evm.Run();

    ShutdownServers(&analytics, ds_client);

    return 0;
}

int main(int argc, char *argv[]) {
    try {
        return analytics_main(argc, argv);
    } catch (boost::program_options::error &e) {
        LOG(ERROR, "Error " << e.what());
        cout << "Error " << e.what();
    } catch (...) {
        LOG(ERROR, "Options Parser: Caught fatal unknown exception");
        cout << "Options Parser: Caught fatal unknown exception";
    }

    return(-1);
}
