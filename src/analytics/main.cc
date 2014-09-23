/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>

#include "analytics/options.h"
#include "analytics/viz_constants.h"
#include "base/cpuinfo.h"
#include "boost/python.hpp"
#include "base/logging.h"
#include "base/contrail_ports.h"
#include "base/task.h"
#include "base/task_trigger.h"
#include "base/timer.h"
#include "base/connection_info.h"
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
#include "analytics_types.h"
#include "generator.h"
#include "Thrift.h"
#include <base/misc_utils.h>
#include <analytics/buildinfo.h>
#include <discovery/client/discovery_client.h>
#include "boost/python.hpp"

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;
using process::ConnectionStateManager;
using process::GetProcessStateCb;

static TaskTrigger *collector_info_trigger;
static Timer *collector_info_log_timer;
static EventManager * a_evm = NULL; 

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
    SocketIOStats rx_stats;
    collector->GetRxSocketStats(rx_stats);
    state.set_rx_socket_stats(rx_stats);
    SocketIOStats tx_stats;
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
    analytics->GetCollector()->GetGeneratorUVEInfo(sinfos);
    for (uint i = 0; i < sinfos.size(); i++) {
        SandeshModuleServerTrace::Send(sinfos[i]);
    }

    vector<SandeshMessageStat> sminfos;
    vector<GeneratorDbStats> gdbsinfos;
    analytics->GetCollector()->GetGeneratorStats(sminfos, gdbsinfos);
    for (uint i = 0; i < sminfos.size(); i++) {
        SandeshMessageTrace::Send(sminfos[i]);
    }
    for (uint i = 0; i < gdbsinfos.size(); i++) {
        GeneratorDbStatsUve::Send(gdbsinfos[i]);
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
    a_evm->Shutdown();
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

    Sandesh::Uninit();

    viz_collector->Shutdown();

    TimerManager::DeleteTimer(collector_info_log_timer);
    delete collector_info_trigger;

    ConnectionStateManager<NodeStatusUVE, NodeStatus>::
        GetInstance()->Shutdown();
    VizCollector::WaitForIdle();
}

static bool OptionsParse(Options &options, int argc, char *argv[]) {
    try {
        options.Parse(*a_evm, argc, argv);
        return true;
    } catch (boost::program_options::error &e) {
        cout << "Error " << e.what() << endl;
    } catch (...) {
        cout << "Options Parser: Caught fatal unknown exception" << endl;
    }

    return false;
}

// This is to force vizd to wait for a gdbattach
// before proceeding.
// It will make it easier to debug vizd during systest
volatile int gdbhelper = 1;

int main(int argc, char *argv[])
{
    a_evm = new EventManager();

    Options options;

    if (!OptionsParse(options, argc, argv)) {
        exit(-1);
    }

    while (gdbhelper==0) {
        usleep(1000);
    }

    Collector::SetProgramName(argv[0]);
    Module::type module = Module::COLLECTOR;
    std::string module_id(g_vns_constants.ModuleNames.find(module)->second);
    LoggingInit(options.log_file(), options.log_file_size(),
                options.log_files_count(), options.use_syslog(),
                options.syslog_facility(), module_id);

    vector<string> cassandra_servers(options.cassandra_server_list());
    vector<string> cassandra_ips;
    vector<int> cassandra_ports;
    for (vector<string>::const_iterator it = cassandra_servers.begin();
         it != cassandra_servers.end(); it++) {
        string cassandra_server(*it);
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> sep(":");
        tokenizer tokens(cassandra_server, sep);
        tokenizer::iterator tit = tokens.begin();
        string cassandra_ip(*tit);
        cassandra_ips.push_back(cassandra_ip);
        ++tit;
        string port(*tit);
        int cassandra_port;
        stringToInteger(port, cassandra_port);
        cassandra_ports.push_back(cassandra_port);
    }

    LOG(INFO, "COLLECTOR LISTEN PORT: " << options.collector_port());
    LOG(INFO, "COLLECTOR REDIS UVE PORT: " << options.redis_port());
    ostringstream css;
    copy(cassandra_servers.begin(), cassandra_servers.end(),
         ostream_iterator<string>(css, " "));
    LOG(INFO, "COLLECTOR CASSANDRA SERVERS: " << css.str());
    LOG(INFO, "COLLECTOR SYSLOG LISTEN PORT: " << options.syslog_port());
    LOG(INFO, "COLLECTOR SFLOW LISTEN PORT: " << options.sflow_port());
    uint16_t protobuf_port(0);
    bool protobuf_server_enabled =
        options.collector_protobuf_port(&protobuf_port);
    if (protobuf_server_enabled) {
        LOG(INFO, "COLLECTOR PROTOBUF LISTEN PORT: " << protobuf_port);
    }

    VizCollector analytics(a_evm,
            options.collector_port(),
            protobuf_server_enabled,
            protobuf_port,
            cassandra_ips,
            cassandra_ports,
            string("127.0.0.1"),
            options.redis_port(),
            options.syslog_port(),
            options.sflow_port(),
            options.dup(),
            options.analytics_data_ttl());

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
    NodeType::type node_type =
        g_vns_constants.Module2NodeType.find(module)->second;
    std::string instance_id(g_vns_constants.INSTANCE_ID_DEFAULT);
    Sandesh::InitCollector(
            module_id,
            analytics.name(),
            g_vns_constants.NodeTypeNames.find(node_type)->second,
            instance_id, 
            a_evm, "127.0.0.1", options.collector_port(),
            options.http_server_port(), &vsc);

    Sandesh::SetLoggingParams(options.log_local(), options.log_category(),
                              options.log_level());

    // XXX Disable logging -- for test purposes only
    if (options.log_disable()) {
        SetLoggingDisabled(true);
    }

    //Publish services to Discovery Service Servee
    DiscoveryServiceClient *ds_client = NULL;
    if (!options.discovery_server().empty()) {
        tcp::endpoint dss_ep;
        boost::system::error_code error;
        dss_ep.address(address::from_string(options.discovery_server(),
                       error));
        dss_ep.port(options.discovery_port());
        string sname = 
            g_vns_constants.ModuleNames.find(Module::COLLECTOR)->second;
        ds_client = new DiscoveryServiceClient(a_evm, dss_ep, sname);
        ds_client->Init();
        Collector::SetDiscoveryServiceClient(ds_client);

        // Get local ip address
        Collector::SetSelfIp(options.host_ip());
        stringstream pub_ss;
        pub_ss << "<" << sname << "><ip-address>" << options.host_ip() <<
                  "</ip-address><port>" << options.collector_port() <<
                  "</port></" << sname << ">";
        std::string pub_msg;
        pub_msg = pub_ss.str();
        ds_client->Publish(DiscoveryServiceClient::CollectorService, pub_msg);
    }
             
    CpuLoadData::Init();
    // Determine if the number of connections is expected:
    // 1. Collector client
    // 2. Redis From
    // 3. Redis To
    // 4. Discovery Collector Publish
    // 5. Database global
    // 6. Database protobuf if enabled
    ConnectionStateManager<NodeStatusUVE, NodeStatus>::
        GetInstance()->Init(*a_evm->io_service(),
            analytics.name(), module_id, instance_id,
            boost::bind(&GetProcessStateCb, _1, _2, _3,
            protobuf_server_enabled ? 6 : 5));
    collector_info_trigger =
        new TaskTrigger(boost::bind(&CollectorInfoLogger, vsc),
                    TaskScheduler::GetInstance()->GetTaskId("vizd::Stats"), 0);
    collector_info_log_timer = TimerManager::CreateTimer(*a_evm->io_service(),
        "Collector Info log timer",
        TaskScheduler::GetInstance()->GetTaskId("vizd::Stats"), 0);
    collector_info_log_timer->Start(5*1000, boost::bind(&CollectorInfoLogTimer), NULL);
    signal(SIGTERM, terminate);
    a_evm->Run();

    ShutdownServers(&analytics, ds_client);

    delete a_evm;

    return 0;
}
