/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <csignal>
#include <fstream>

#include <pthread.h>
#include <boost/program_options.hpp>
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "nodeinfo_types.h"
#include "base/connection_info.h"
#include <base/logging.h>
#include <base/misc_utils.h>
#include <base/contrail_ports.h>
#include <base/task.h>
#include <db/db_graph.h>
#include <ifmap/client/config_client_manager.h>
#include <ifmap/ifmap_link_table.h>
#include "ifmap/ifmap_sandesh_context.h"
#include <ifmap/ifmap_server.h>
#include <ifmap/ifmap_xmpp.h>
#include <io/event_manager.h>
#include <sandesh/sandesh.h>
#include <vnc_cfg_types.h>
#include <cmn/dns.h>
#include <bind/bind_util.h>
#include <mgr/dns_mgr.h>
#include <mgr/dns_oper.h>
#include <bind/named_config.h>
#include <cfg/dns_config.h>
#include <cfg/dns_config_parser.h>
#include <agent/agent_xmpp_init.h>
#include "cmn/dns_options.h"
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <uve/uve.h>
#include "xmpp/xmpp_sandesh.h"

namespace opt = boost::program_options;
using namespace boost::asio::ip;
using namespace std;
using process::ConnectionInfo;
using process::ConnectionStateManager;
using process::GetProcessStateCb;
using process::ProcessState;
using process::ConnectionType;
using process::ConnectionTypeName;
using process::g_process_info_constants;


uint64_t start_time;
TaskTrigger *dns_info_trigger;
Timer *dns_info_log_timer;
static Options options;

bool DnsInfoLogTimer() {
    dns_info_trigger->Set();
    return false;
}

bool DnsInfoLogger() {
    DnsUveClient::SendDnsUve(start_time);

    dns_info_log_timer->Cancel();
    dns_info_log_timer->Start(60*1000, boost::bind(&DnsInfoLogTimer),NULL);
    return true;
}

static void DnsServerGetProcessStateCb(
   const ConfigClientManager *config_client_manager,
   const std::vector<ConnectionInfo> &cinfos,
   ProcessState::type &state, std::string &message,
   std::vector<ConnectionTypeName> expected_connections) {
    GetProcessStateCb(cinfos, state, message, expected_connections);
    if (state == ProcessState::NON_FUNCTIONAL)
        return;
    if (!config_client_manager->GetEndOfRibComputed()) {
        state = ProcessState::NON_FUNCTIONAL;
        message = "IFMap Server End-Of-RIB not computed";
    }
}


bool DnsServerReEvaluatePublishCb(IFMapServer *ifmap_server,
      const ConfigClientManager *config_client_manager, std::string &message) {
    if (!config_client_manager->GetEndOfRibComputed()) {
        message = "IFMap Server End-Of-RIB not computed";
        return false;
    }

    DnsManager *dns_manager = Dns::GetDnsManager();
    if (dns_manager && !dns_manager->IsBindStatusUp()) {
        message = "Connection to named DOWN";
        return false;
    }

    message = "OK";
    return true;
}

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

static void IFMap_Initialize(IFMapServer *server, ConfigClientManager *mgr) {
    IFMapLinkTable_Init(server->database(), server->graph());
    vnc_cfg_JsonParserInit(mgr->config_json_parser());
    vnc_cfg_Server_ModuleInit(server->database(), server->graph());
    server->Initialize();
}

static bool OptionsParse(Options &options, int argc, char *argv[]) {
    try {
        options.Parse(*Dns::GetEventManager(), argc, argv);
        return true;
    } catch (boost::program_options::error &e) {
        cout << "Error " << e.what() << endl;
    } catch (...) {
        cout << "Options Parser: Caught fatal unknown exception" << endl;
    }

    return false;
}

void ReConfigSignalHandler(int signum) {
    options.ParseReConfig();
}

void InitializeSignalHandlers() {
    srand(unsigned(time(NULL)));                                                                                                  
    signal(SIGHUP, ReConfigSignalHandler);
}

int main(int argc, char *argv[]) {
    // Initialize the task scheduler
    int num_threads_to_tbb = TaskScheduler::GetDefaultThreadCount() +
        ConfigClientManager::GetNumWorkers();
    TaskScheduler::Initialize(num_threads_to_tbb);

    // Create DB table and event manager
    Dns::Init();

    // Process options from command-line and configuration file.
    if (!OptionsParse(options, argc, argv)) {
        exit(-1);
    }

    InitializeSignalHandlers();

    Dns::SetProgramName(argv[0]);
    Module::type module = Module::DNS;
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

    string build_info_str;
    Dns::GetVersion(build_info_str);
    MiscUtils::LogVersionInfo(build_info_str, Category::DNSAGENT);

    if (options.collectors_configured()) {
        Dns::SetCollector(options.collector_server_list()[0]);
    }
    Dns::SetHttpPort(options.http_server_port());
    Dns::SetDnsPort(options.dns_server_port());

    boost::system::error_code ec;
    string hostname = host_name(ec);
    Dns::SetHostName(hostname);
    Sandesh::set_send_rate_limit(options.sandesh_send_rate_limit());
    {
        NodeType::type node_type =
            g_vns_constants.Module2NodeType.find(module)->second;
        bool success(Sandesh::InitGenerator(
                    module_name,
                    options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    g_vns_constants.INSTANCE_ID_DEFAULT,
                    Dns::GetEventManager(),
                    options.http_server_port(),
                    options.randomized_collector_server_list(),
                    NULL,
                    Sandesh::DerivedStats(),
                    options.sandesh_config()));
        if (!success) {
            LOG(ERROR, "SANDESH: Initialization FAILED ... exiting");
            Sandesh::Uninit();
            exit(1);
        }
    }
    Sandesh::SetLoggingParams(options.log_local(), options.log_category(),
                              options.log_level());

    // XXX Disable logging -- for test purposes only
    if (options.log_disable()) {
        SetLoggingDisabled(true);
    }

    // DNS::SetTestMode(options.test_mode());
    ConnectionStateManager::GetInstance();

    DB config_db(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"));
    DBGraph config_graph;
    IFMapServer ifmap_server(&config_db, &config_graph,
                             Dns::GetEventManager()->io_service());

    ConfigClientManager *config_client_manager =
        new ConfigClientManager(Dns::GetEventManager(), &ifmap_server,
            options.hostname(), module_name, options.configdb_options());
    IFMap_Initialize(&ifmap_server, config_client_manager);


    DnsManager dns_manager;
    Dns::SetDnsManager(&dns_manager);
    dns_manager.Initialize(&config_db, &config_graph,
                           options.named_config_dir(),
                           options.named_config_file(),
                           options.named_log_file(),
                           options.rndc_config_file(),
                           options.rndc_secret(),
                           options.named_max_cache_size(),
                           options.named_max_retransmissions(),
                           options.named_retransmission_interval());
    DnsConfigParser parser(&config_db);
    parser.Parse(FileRead(options.config_file()));

    if (!DnsAgentXmppManager::Init(options.xmpp_auth_enabled(),
                                   options.xmpp_server_cert(),
                                   options.xmpp_server_key(),
                                   options.xmpp_ca_cert())) {
        LOG(ERROR, "Address already in use " << ContrailPorts::DnsXmpp());
        exit(1);
    }

    XmppSandeshContext xmpp_sandesh_context;
    xmpp_sandesh_context.xmpp_server = Dns::GetXmppServer();
    Sandesh::set_module_context("XMPP", &xmpp_sandesh_context);
    IFMapSandeshContext ifmap_sandesh_context(&ifmap_server);
    Sandesh::set_module_context("IFMap", &ifmap_sandesh_context);

    start_time = UTCTimestampUsec();
    dns_info_trigger =
            new TaskTrigger(boost::bind(&DnsInfoLogger),
                    TaskScheduler::GetInstance()->GetTaskId("dns::Config"), 0);

    dns_info_log_timer =
        TimerManager::CreateTimer(*(Dns::GetEventManager()->io_service()),
                                                   "Dns Info log timer");
    dns_info_log_timer->Start(60*1000, boost::bind(&DnsInfoLogTimer), NULL);
    Dns::SetSelfIp(options.host_ip());
    ifmap_server.set_config_manager(config_client_manager);

    //
    // Determine if the number of connections is as expected.
    // 1. Cassandra Server
    // 2. AMQP Server
    //
    std::vector<ConnectionTypeName> expected_connections;
    expected_connections = boost::assign::list_of
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::COLLECTOR)->second, "Collector"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DATABASE)->second, "Cassandra"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DATABASE)->second, "RabbitMQ"));

    ConnectionStateManager::GetInstance()->Init(
        *(Dns::GetEventManager()->io_service()), options.hostname(),
        module_name, g_vns_constants.INSTANCE_ID_DEFAULT,
        boost::bind(&DnsServerGetProcessStateCb, config_client_manager, _1, _2, _3,
                    expected_connections), "ObjectDns");

    dns_manager.set_config_manager(config_client_manager);
    config_client_manager->Initialize();

    Dns::GetEventManager()->Run();

    return 0;
}
