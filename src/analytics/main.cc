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
#include "discovery_client.h"
#include "boost/python.hpp"

using namespace ::apache::thrift;

using namespace std;
using boost::system::error_code;
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

    ModuleCpuState state;
    state.set_name(hostname);

    ModuleCpuInfo cinfo;
    cinfo.set_module_id(
            g_vns_constants.ModuleNames.find(Module::COLLECTOR)->second);

    CpuLoadInfo cpu_load_info;
    CpuLoadData::FillCpuInfo(cpu_load_info, false);
    cinfo.set_cpu_info(cpu_load_info);

    vector<ModuleCpuInfo> cciv;
    cciv.push_back(cinfo);
    state.set_module_cpu_info(cciv);
    state.set_collector_cpu_share(cpu_load_info.get_cpu_share());
    state.set_collector_mem_virt(cpu_load_info.get_meminfo().get_virt());

    ModuleCpuStateTrace::Send(state);

    return true;
}

void HandleGenCleanup(int count) {
    if (count)
        LOG(INFO, "Cleaned up " << count << " abandoned generators");
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

    // The generator keys must be refreshed to prove that this Vizd
    // instance owns its generators
    for (vector<GeneratorSummaryInfo>::const_iterator it = infos.begin(); 
            it != infos.end(); it++) {
        osp->RefreshGenerator(it->get_source(), it->get_module_id());
    }

    osp->GeneratorCleanup(HandleGenCleanup);

    state.set_generator_infos(infos);
    CollectorInfo::Send(state);
    return true;
}

bool CollectorInfoLogger(VizSandeshContext &ctx) {
    VizCollector *analytics = ctx.Analytics();
    CollectorState state;

    CollectorCPULogger(analytics->name());
    CollectorSummaryLogger(analytics->GetCollector(), analytics->name(),
            analytics->GetOsp());
    state.set_name(analytics->name());

    vector<ModuleServerState> sinfos;    
    analytics->GetCollector()->GetGeneratorSandeshStatsInfo(sinfos);

    for (uint i =0 ; i< sinfos.size(); i++) {
        SandeshModuleServerTrace::Send(sinfos[i]);
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

// This is to force vizd to wait for a gdbattach
// before proceeding.
// It will make it easier to debug vizd during systest
volatile int gdbhelper = 1;

int main(int argc, char *argv[])
{
    const string default_log_file = "<stdout>";
    while (gdbhelper==0) {
        usleep(1000);
    }
    opt::options_description desc("Command line options");
    desc.add_options()
        ("help", "help message")
        ("cassandra-server-list",
         opt::value<vector<string> >()->default_value(
                 std::vector<std::string>(), "127.0.0.1:9160"),
         "cassandra server list")
        ("analytics-data-ttl", opt::value<int>()->default_value(g_viz_constants.AnalyticsTTL),
            "global TTL(days) for analytics data")
        ("discovery-server", opt::value<string>(),
         "IP address of Discovery Server")
        ("discovery-port",
         opt::value<int>()->default_value(ContrailPorts::DiscoveryServerPort),
         "Port of Discovery Server")
        ("redis-ip",
         opt::value<string>()->default_value("127.0.0.1"),
         "redis server ip")
        ("redis-port",
         opt::value<int>()->default_value(ContrailPorts::RedisUvePort),
         "redis server port")
        ("listen-port",
         opt::value<int>()->default_value(ContrailPorts::CollectorPort),
         "vizd listener port")
        ("host-ip", opt::value<string>(),
         "IP address of Analytics Node")
        ("http-server-port",
            opt::value<int>()->default_value(ContrailPorts::HttpPortCollector),
            "Sandesh HTTP listener port")
        ("dup", "Internal use")
        ("gen-timeout", opt::value<int>()->default_value(80),
            "Expiration timeout for generators")
        ("log-local", "Enable local logging of sandesh messages")
        ("log-level", opt::value<string>()->default_value("SYS_DEBUG"),
            "Severity level for local logging of sandesh messages")
        ("log-category", opt::value<string>()->default_value(""),
            "Category filter for local logging of sandesh messages")
        ("log-file", opt::value<string>()->default_value(default_log_file),
         "Filename for the logs to be written to")
        ("version", "Display version information")
        ;
    opt::variables_map var_map;
    opt::store(opt::parse_command_line(argc, argv, desc), var_map);
    opt::notify(var_map);

    if (var_map.count("help")) {
        cout << desc << endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info_str;
        CollectorVersion(build_info_str);
        cout << build_info_str << endl;
        exit(0);
    }

    Collector::SetProgramName(argv[0]);
    if (var_map["log-file"].as<string>() == default_log_file) {
        LoggingInit();
    } else {
        LoggingInit(var_map["log-file"].as<string>());
    }

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

    bool dup = false;
    if (var_map.count("dup")) {
        dup = true;
    }

    LOG(INFO, "COLLECTOR LISTEN PORT: " << var_map["listen-port"].as<int>());
    LOG(INFO, "COLLECTOR REDIS SERVER: " << var_map["redis-ip"].as<string>());
    LOG(INFO, "COLLECTOR REDIS PORT: " << var_map["redis-port"].as<int>());
    LOG(INFO, "COLLECTOR CASSANDRA SERVER: " << cassandra_ip);
    LOG(INFO, "COLLECTOR CASSANDRA PORT: " << cassandra_port);

    VizCollector analytics(&evm,
            var_map["listen-port"].as<int>(),
            cassandra_ip,
            cassandra_port,
            var_map["redis-ip"].as<string>(),
            var_map["redis-port"].as<int>(),
            var_map["gen-timeout"].as<int>(),
            dup,
            var_map["analytics-data-ttl"].as<int>());

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
    Sandesh::InitCollector(
            g_vns_constants.ModuleNames.find(Module::COLLECTOR)->second,
            analytics.name(), &evm, "127.0.0.1", var_map["listen-port"].as<int>(),
            var_map["http-server-port"].as<int>(), &vsc);
    Sandesh::SetLoggingParams(var_map.count("log"),
            var_map["log-category"].as<string>(),
            var_map["log-level"].as<string>());

    //Publish services to Discovery Service Servee
    DiscoveryServiceClient *ds_client = NULL;
    if (var_map.count("discovery-server")) {
        tcp::endpoint dss_ep;
        error_code error;
        dss_ep.address(address::from_string(var_map["discovery-server"].as<string>(),
                       error));
        dss_ep.port(var_map["discovery-port"].as<int>());
        ds_client = new DiscoveryServiceClient(&evm, dss_ep);
        ds_client->Init();

        // Get local ip address
        string self_ip; 
        if (var_map.count("host-ip")) { 
            self_ip = var_map["host-ip"].as<string>();
        } else {
            tcp::resolver resolver(*evm.io_service());
            tcp::resolver::query query(boost::asio::ip::host_name(), "");
            tcp::resolver::iterator iter = resolver.resolve(query);
            self_ip = iter->endpoint().address().to_string();
        }
        Collector::SetSelfIp(self_ip);
        stringstream pub_ss;
        string sname = g_vns_constants.ModuleNames.find(Module::COLLECTOR)->second;
        pub_ss << "<" << sname << "><ip-address>" << self_ip <<
                  "</ip-address><port>" << var_map["listen-port"].as<int>() <<
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
