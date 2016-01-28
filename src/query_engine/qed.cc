/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fstream>
#include <boost/asio/ip/host_name.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include "base/logging.h"
#include "base/cpuinfo.h"
#include "base/task.h"
#include "base/task_trigger.h"
#include "base/timer.h"
#include "base/connection_info.h"
#include "io/event_manager.h"
#include "QEOpServerProxy.h"
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include "analytics_types.h"
#include "query_engine/options.h"
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
using namespace std;
using process::ConnectionStateManager;
using process::GetProcessStateCb;
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

    ModuleCpuStateTrace::Send(state);

    SendCpuInfoStat<AnalyticsCpuStateTrace, AnalyticsCpuState>(hostname,
        cpu_load_info);

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

static bool OptionsParse(Options &options, EventManager &evm,
                         int argc, char *argv[]) {
    try {
        options.Parse(evm, argc, argv);
        return true;
    } catch (boost::program_options::error &e) {
        cout << "Error " << e.what() << endl;
    } catch (...) {
        cout << "Options Parser: Caught fatal unknown exception" << endl;
    }

    return false;
}

static void ShutdownQe(DiscoveryServiceClient *ds_client,
    Timer *qe_info_log_timer) {
    if (ds_client) {
        ds_client->Shutdown();
        delete ds_client;
    }
    if (qe_info_log_timer) {
        qe_info_log_timer->Cancel();
        TimerManager::DeleteTimer(qe_info_log_timer);
    }
    WaitForIdle();
    Sandesh::Uninit();
    ConnectionStateManager<NodeStatusUVE, NodeStatus>::
        GetInstance()->Shutdown();
    WaitForIdle();
}

int
main(int argc, char *argv[]) {
    EventManager evm;
    pevm = &evm;
    Options options;

    // Increase max number of threads available by a factor of 4
    TaskScheduler::SetThreadAmpFactor( 
        QEOpServerProxy::nThreadCountMultFactor);

    if (!OptionsParse(options, evm, argc, argv)) {
        exit(-1);
    }

    while (gdbhelper==0) {
        usleep(1000);
    }

    Module::type module = Module::QUERY_ENGINE;
    string module_name = g_vns_constants.ModuleNames.find(module)->second;

    std::string log_property_file = options.log_property_file();
    if (log_property_file.size()) {
        LoggingInit(log_property_file);
    }
    else {
        LoggingInit(options.log_file(), options.log_file_size(),
                    options.log_files_count(), options.use_syslog(),
                    options.syslog_facility(), module_name,
                    SandeshLevelTolog4Level(
                        Sandesh::StringToLevel(options.log_level())));
    }
    error_code error;
    DiscoveryServiceClient *ds_client = NULL;
    ip::tcp::endpoint dss_ep;
    Sandesh::CollectorSubFn csf = 0;

    if (DiscoveryServiceClient::ParseDiscoveryServerConfig(
        options.discovery_server(), options.discovery_port(), &dss_ep)) {

            string subscriber_name =
                g_vns_constants.ModuleNames.find(Module::QUERY_ENGINE)->second;
            ds_client = new DiscoveryServiceClient(&evm, dss_ep, 
                                                   subscriber_name);
            ds_client->Init();
            csf = boost::bind(&DiscoveryServiceClient::Subscribe, 
                              ds_client, _1, _2, _3);
    } else {
        LOG(ERROR, "Invalid Discovery Server hostname or ip " <<
                    options.discovery_server());
    }

    int max_tasks = options.max_tasks();
    // Tune max_tasks 
    if (max_tasks == 0)
    {
        // no command line option was specified to tune the max # of tasks
        max_tasks = QEOpServerProxy::nMaxChunks;

        if (max_tasks*2 > TaskScheduler::GetThreadCount())
        {
            // avoid creating too many tasks for one query
            max_tasks = TaskScheduler::GetThreadCount()/2;
            // make sure atleast we have one task
            max_tasks = (max_tasks > 1) ? max_tasks : 1;
        }
    }

    LOG(INFO, "http-server-port " << options.http_server_port());
    LOG(INFO, "Endpoint " << dss_ep);
    LOG(INFO, "Max-tasks " << max_tasks);
    LOG(INFO, "Max-slice " << options.max_slice());
    BOOST_FOREACH(std::string collector_ip, options.collector_server_list()) {
        LOG(INFO, "Collectors  " << collector_ip);
    }

    // Initialize Sandesh
    NodeType::type node_type = 
        g_vns_constants.Module2NodeType.find(module)->second;
    std::string instance_id(g_vns_constants.INSTANCE_ID_DEFAULT);
    // Determine if the number of connections is expected:
    // 1. Collector client
    // 2. Redis
    // 3. Cassandra
    // 4. Discovery (if collector list not configured)
    int num_expected_connections = 4;
    bool use_collector_list = false;
    if (options.collectors_configured() || !csf) {
        num_expected_connections = 3;
        use_collector_list = true;
    }
    ConnectionStateManager<NodeStatusUVE, NodeStatus>::
        GetInstance()->Init(*evm.io_service(),
            options.hostname(), module_name,
            instance_id,
            boost::bind(&GetProcessStateCb, _1, _2, _3,
            num_expected_connections));
    Sandesh::set_send_rate_limit(options.sandesh_send_rate_limit());
    bool success;
    // subscribe to the collector service with discovery only if the
    // collector list is not configured.
    if (use_collector_list) {
        std::vector<std::string> collectors(options.collector_server_list());
        if (!collectors.size()) {
            collectors = options.default_collector_server_list();
        }
        success = Sandesh::InitGenerator(module_name, options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    instance_id, &evm, options.http_server_port(), 0,
                    collectors, NULL);
    } else {
        const std::vector<std::string> collectors;
        success = Sandesh::InitGenerator(module_name, options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    instance_id, &evm, options.http_server_port(), csf,
                    collectors, NULL);
    }
    if (!success) {
        LOG(ERROR, "SANDESH: Initialization FAILED ... exiting");
        ShutdownQe(ds_client, NULL);
        exit(1);
    }

    Sandesh::SetLoggingParams(options.log_local(), options.log_category(),
                              options.log_level());

    // XXX Disable logging -- for test purposes only
    if (options.log_disable()) {
        SetLoggingDisabled(true);
    }

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
    ostringstream css;
    copy(cassandra_servers.begin(), cassandra_servers.end(),
         ostream_iterator<string>(css, " "));
    LOG(INFO, "Cassandra Servers: " << css.str());

    boost::scoped_ptr<QueryEngine> qe;

    //Get Platform info
    //cql not supported in precise, centos 6.4 6.5
    bool use_cql = MiscUtils::IsCqlSupported();

    if (cassandra_ports.size() == 1 && cassandra_ports[0] == 0) {
        qe.reset(new QueryEngine(&evm,
            options.redis_server(),
            options.redis_port(),
            options.redis_password(),
            max_tasks,
            options.max_slice(),
            options.cassandra_user(),
            options.cassandra_password()));
    } else {
        qe.reset(new QueryEngine(&evm,
            cassandra_ips,
            cassandra_ports,
            options.redis_server(),
            options.redis_port(),
            options.redis_password(),
            max_tasks,
            options.max_slice(),
            options.cassandra_user(),
            options.cassandra_password(),
            use_cql));
    }

    CpuLoadData::Init();
    qe_info_trigger =
        new TaskTrigger(boost::bind(&QEInfoLogger, options.hostname()),
                    TaskScheduler::GetInstance()->GetTaskId("qe::Stats"), 0);
    qe_info_log_timer = TimerManager::CreateTimer(*evm.io_service(),
                                                    "Collector Info log timer");
    qe_info_log_timer->Start(5*1000, boost::bind(&QEInfoLogTimer), NULL);    
    signal(SIGTERM, terminate_qe);
    evm.Run();

    ShutdownQe(ds_client, qe_info_log_timer);
    return 0;
}
