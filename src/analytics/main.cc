/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include <boost/foreach.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <malloc.h>
#include "analytics/options.h"
#include "analytics/viz_constants.h"
#include "boost/python.hpp"
#include "base/logging.h"
#include "base/contrail_ports.h"
#include "base/task.h"
#include "base/task_trigger.h"
#include "base/timer.h"
#include "base/connection_info.h"
#include "base/misc_utils.h"
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
#include "nodeinfo_types.h"
#include "analytics_types.h"
#include "generator.h"
#include <base/misc_utils.h>
#include <analytics/buildinfo.h>
#include "boost/python.hpp"

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;
using process::ConnectionStateManager;
using process::GetProcessStateCb;
using process::ConnectionType;
using process::ConnectionTypeName;
using process::g_process_info_constants;

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

bool CollectorSummaryLogger(Collector *collector, const string & hostname,
        OpServerProxy * osp) {
    CollectorState state;
    static bool first = true, build_info_set = false;

    state.set_name(hostname);
    if (first) {
        vector<string> ip_list;
        ip_list.push_back(Collector::GetSelfIp());
        state.set_self_ip_list(ip_list);
        first = false;
    }
    if (!build_info_set) {
        string build_info_str;
        build_info_set = CollectorVersion(build_info_str);
        state.set_build_info(build_info_str);
    }

    std::vector<GeneratorSummaryInfo> infos;
    collector->GetGeneratorSummaryInfo(&infos);

    state.set_generator_infos(infos);

    // Get socket stats
    SocketIOStats rx_stats;
    collector->GetRxSocketStats(&rx_stats);
    state.set_rx_socket_stats(rx_stats);
    SocketIOStats tx_stats;
    collector->GetTxSocketStats(&tx_stats);
    state.set_tx_socket_stats(tx_stats);

    CollectorInfo::Send(state);
    return true;
}

bool CollectorInfoLogger(VizSandeshContext &ctx) {
    VizCollector *analytics = ctx.Analytics();

    CollectorSummaryLogger(analytics->GetCollector(), analytics->name(),
            analytics->GetOsp());
    analytics->SendDbStatistics();
    analytics->SendProtobufCollectorStatistics();

    vector<ModuleServerState> sinfos;
    analytics->GetCollector()->GetGeneratorUVEInfo(sinfos);
    for (uint i = 0; i < sinfos.size(); i++) {
        SandeshModuleServerTrace::Send(sinfos[i]);
    }

    analytics->SendGeneratorStatistics();

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
    // Shutdown can result in a malloc-detected error
    // Taking a stack trace during this error can result in
    // the process not terminating correctly
    // using mallopt in this way ensures that we get a core,
    // but we don't print a stack trace
    mallopt(M_CHECK_ACTION, 2);
    CollectorShutdown();
}

// Shutdown various objects used in the collector.
static void ShutdownServers(VizCollector *viz_collector) {

    Sandesh::Uninit();

    viz_collector->Shutdown();

    if (collector_info_log_timer) {
        TimerManager::DeleteTimer(collector_info_log_timer);
        delete collector_info_trigger;
        collector_info_log_timer = NULL;
    }

    ConnectionStateManager::
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
    NodeType::type node_type =
        g_vns_constants.Module2NodeType.find(module)->second;
    std::string instance_id(g_vns_constants.INSTANCE_ID_DEFAULT);
    std::string log_property_file = options.log_property_file();
    if (log_property_file.size()) {
        LoggingInit(log_property_file);
    } else {
        LoggingInit(options.log_file(), options.log_file_size(),
                    options.log_files_count(), options.use_syslog(),
                    options.syslog_facility(), module_id,
                    SandeshLevelTolog4Level(
                        Sandesh::StringToLevel(options.log_level())));
    }
    vector<string> cassandra_servers(options.cassandra_server_list());
    for (vector<string>::const_iterator it = cassandra_servers.begin();
         it != cassandra_servers.end(); it++) {
        string cassandra_server(*it);
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> sep(":");
        tokenizer tokens(cassandra_server, sep);
        tokenizer::iterator tit = tokens.begin();
        string cassandra_ip(*tit);
        options.add_cassandra_ip(cassandra_ip);
        ++tit;
        string port(*tit);
        int cassandra_port;
        stringToInteger(port, cassandra_port);
        options.add_cassandra_port(cassandra_port);
    }

    /*
     * the option is enable_db_messages_keyword_writes, but the variable
     * passed along is options.disable_db_messages_keyword_writes
     * so we need to update it in the options.cassandra_options_
     */
    options.disable_db_messages_keyword_writes();

    LOG(INFO, "COLLECTOR LISTEN PORT: " << options.collector_port());
    LOG(INFO, "COLLECTOR REDIS UVE PORT: " << options.redis_port());
    ostringstream css;
    copy(cassandra_servers.begin(), cassandra_servers.end(),
         ostream_iterator<string>(css, " "));
    LOG(INFO, "COLLECTOR CASSANDRA SERVERS: " << css.str());
    LOG(INFO, "COLLECTOR ZOOKEEPER SERVERS: " <<
        options.zookeeper_server_list());
    LOG(INFO, "COLLECTOR SYSLOG LISTEN PORT: " << options.syslog_port());
    LOG(INFO, "COLLECTOR SFLOW LISTEN PORT: " << options.sflow_port());
    LOG(INFO, "COLLECTOR IPFIX LISTEN PORT: " << options.ipfix_port());
    uint16_t protobuf_port(0);
    bool protobuf_server_enabled =
        options.collector_protobuf_port(&protobuf_port);
    if (protobuf_server_enabled) {
        LOG(INFO, "COLLECTOR PROTOBUF LISTEN PORT: " << protobuf_port);
    }
    uint16_t structured_syslog_port(0);
    bool structured_syslog_server_enabled =
        options.collector_structured_syslog_port(&structured_syslog_port);
    vector<string> structured_syslog_fwd;
    if (structured_syslog_server_enabled) {
        LOG(INFO, "COLLECTOR STRUCTURED SYSLOG LISTEN PORT: " << structured_syslog_port);
        structured_syslog_fwd = options.collector_structured_syslog_forward_destination();
    }
    string kstr("");
    vector<string> kbl = options.kafka_broker_list();
    for (vector<string>::const_iterator st = kbl.begin();
            st != kbl.end(); st++) {
        if (st != kbl.begin()) {
            kstr += string(",");
        }
        kstr += *st;
    }
    std::map<std::string, std::string> aggconf;
    vector<string> upl = options.uve_proxy_list();
    for (vector<string>::const_iterator st = upl.begin();
            st != upl.end(); st++) {
        size_t spos = st->find(':');
        string key = st->substr(0, spos);
        string val = st->substr(spos+1, string::npos);
        aggconf[key] = val;
    }

    LOG(INFO, "KAFKA BROKERS: " << kstr);
    std::string hostname;
    boost::system::error_code error;
    if (options.dup()) {
        hostname = boost::asio::ip::host_name(error) + "dup";
    } else {
        hostname = boost::asio::ip::host_name(error);
    }
    // Determine if the number of connections is expected:
    // 1. Collector client
    // 2. Redis From
    // 3. Redis To
    // 4. Database global
    // 5. Kafka Pub
    // 6. Database protobuf if enabled

    std::vector<ConnectionTypeName> expected_connections; 
    expected_connections = boost::assign::list_of
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::COLLECTOR)->second, ""))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::REDIS_UVE)->second, "To"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::REDIS_UVE)->second, "From"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DATABASE)->second,
                             hostname+":Global"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::KAFKA_PUB)->second, kstr));

    ConnectionStateManager::
        GetInstance()->Init(*a_evm->io_service(),
            hostname, module_id, instance_id,
            boost::bind(&GetProcessStateCb, _1, _2, _3,
            expected_connections), "ObjectCollectorInfo");

    LOG(INFO, "COLLECTOR analytics_data_ttl: " << options.analytics_data_ttl());
    LOG(INFO, "COLLECTOR analytics_flow_ttl: " << options.analytics_flow_ttl());
    LOG(INFO, "COLLECTOR analytics_statistics_ttl: " <<
            options.analytics_statistics_ttl());
    LOG(INFO, "COLLECTOR analytics_config_audit_ttl: " <<
            options.analytics_config_audit_ttl());
    TtlMap ttl_map;
    ttl_map.insert(std::make_pair(TtlType::FLOWDATA_TTL,
                options.analytics_flow_ttl()));
    ttl_map.insert(std::make_pair(TtlType::STATSDATA_TTL,
                options.analytics_statistics_ttl()));
    ttl_map.insert(std::make_pair(TtlType::CONFIGAUDIT_TTL,
                options.analytics_config_audit_ttl()));
    ttl_map.insert(std::make_pair(TtlType::GLOBAL_TTL,
                options.analytics_data_ttl()));
    options.set_ttl_map(ttl_map);

    std::string zookeeper_server_list(options.zookeeper_server_list());
    bool use_zookeeper = !zookeeper_server_list.empty();

    ConfigDBConnection::ApiServerList api_server_list;
    BOOST_FOREACH(const std::string &api_server, options.api_server_list()) {
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> sep(":");
        tokenizer tokens(api_server, sep);
        tokenizer::iterator tit = tokens.begin();
        string api_server_ip(*tit);
        int api_server_port;
        stringToInteger(*++tit, api_server_port);
        api_server_list.push_back(std::make_pair(api_server_ip,
                                                 api_server_port));
    }
    VncApiConfig api_config;
    api_config.api_use_ssl = options.api_server_use_ssl();
    api_config.ks_srv_ip = options.auth_host();
    api_config.ks_srv_port = options.auth_port();
    api_config.ks_protocol = options.auth_protocol();
    api_config.ks_user = options.auth_user();
    api_config.ks_password = options.auth_passwd();
    api_config.ks_tenant = options.auth_tenant();
    api_config.ks_keyfile = options.keystone_keyfile();
    api_config.ks_certfile = options.keystone_certfile();
    api_config.ks_cafile = options.keystone_cafile();

    VizCollector analytics(a_evm,
            options.collector_port(),
            protobuf_server_enabled,
            protobuf_port,
            structured_syslog_server_enabled,
            structured_syslog_port,
            structured_syslog_fwd,
            string("127.0.0.1"),
            options.redis_port(),
            options.redis_password(),
            aggconf,
            kstr,
            options.syslog_port(),
            options.sflow_port(),
            options.ipfix_port(),
            options.partitions(),
            options.dup(),
            options.kafka_prefix(),
            options.get_cassandra_options(),
            zookeeper_server_list,
            use_zookeeper,
            options.get_db_write_options(),
            options.sandesh_config(),
            api_server_list,
            api_config);
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

    unsigned short coll_port = analytics.GetCollector()->GetPort();
    VizSandeshContext vsc(&analytics);
    Sandesh::set_send_rate_limit(options.sandesh_send_rate_limit());
    bool success(Sandesh::InitCollector(
            module_id,
            analytics.name(),
            g_vns_constants.NodeTypeNames.find(node_type)->second,
            instance_id,
            a_evm, "127.0.0.1", coll_port,
            options.http_server_port(), &vsc, options.sandesh_config()));
    if (!success) {
        LOG(ERROR, "SANDESH: Initialization FAILED ... exiting");
        ShutdownServers(&analytics);
        delete a_evm;
        exit(1);
    }

    Sandesh::DisableFlowCollection(options.disable_flow_collection());
    Sandesh::SetLoggingParams(options.log_local(), options.log_category(),
                              options.log_level());

    // XXX Disable logging -- for test purposes only
    if (options.log_disable()) {
        SetLoggingDisabled(true);
    }

    // Get local ip address
    Collector::SetSelfIp(options.host_ip());

    collector_info_trigger =
        new TaskTrigger(boost::bind(&CollectorInfoLogger, vsc),
                    TaskScheduler::GetInstance()->GetTaskId("vizd::Stats"), 0);
    collector_info_log_timer = TimerManager::CreateTimer(*a_evm->io_service(),
        "Collector Info log timer",
        TaskScheduler::GetInstance()->GetTaskId("vizd::Stats"), 0);
    collector_info_log_timer->Start(5*1000, boost::bind(&CollectorInfoLogTimer), NULL);
    signal(SIGTERM, terminate);
    a_evm->Run();

    ShutdownServers(&analytics);

    delete a_evm;

    return 0;
}
