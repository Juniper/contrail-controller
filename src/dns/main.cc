/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include <pthread.h>
#include <boost/program_options.hpp>
#include <base/logging.h>
#include <base/misc_utils.h>
#include <base/contrail_ports.h>
#include <base/task.h>
#include <db/db_graph.h>
#include <ifmap/ifmap_link_table.h>
#include <ifmap/ifmap_server_parser.h>
#include <ifmap/ifmap_server.h>
#include <ifmap/ifmap_xmpp.h>
#include <ifmap/client/ifmap_manager.h>
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
#include "bgp/bgp_sandesh.h"
#include "cmn/dns_options.h"
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <uve/uve.h>

namespace opt = boost::program_options;
using namespace boost::asio::ip;
using namespace std;

uint64_t start_time;
TaskTrigger *dns_info_trigger;
Timer *dns_info_log_timer;

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

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

static void IFMap_Initialize(IFMapServer *server) {
    IFMapLinkTable_Init(server->database(), server->graph());
    IFMapServerParser *parser = IFMapServerParser::GetInstance("vnc_cfg");
    vnc_cfg_ParserInit(parser);
    vnc_cfg_Server_ModuleInit(server->database(), server->graph());
    // bgp_schema_ParserInit(parser);
    // bgp_schema_Server_ModuleInit(server->database(), server->graph());
    server->Initialize();
}

void Dns::ShutdownDiscoveryClient(DiscoveryServiceClient *ds_client) {
    if (ds_client) {
        ds_client->Shutdown();
        delete ds_client;
    }
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

int main(int argc, char *argv[]) {
    // Create DB table and event manager
    Dns::Init();

    Options options;

    // Process options from command-line and configuration file.
    if (!OptionsParse(options, argc, argv)) {
        exit(-1);
    }

    Dns::SetProgramName(argv[0]);
    Module::type module = Module::DNS;
    string module_name = g_vns_constants.ModuleNames.find(module)->second;

    LoggingInit(options.log_file(), options.log_file_size(),
                options.log_files_count(), options.use_syslog(),
                options.syslog_facility(), module_name);

    string build_info_str;
    Dns::GetVersion(build_info_str);
    MiscUtils::LogVersionInfo(build_info_str, Category::DNSAGENT);

    if (options.collectors_configured()) {
        Dns::SetCollector(options.collector_server_list()[0]);
    }
    Dns::SetHttpPort(options.http_server_port());
    Dns::SetDnsPort(options.dns_server_port());

    BgpSandeshContext sandesh_context;
    boost::system::error_code ec;
    string hostname = host_name(ec);
    Dns::SetHostName(hostname);
    if (options.discovery_server().empty()) {
        NodeType::type node_type = 
            g_vns_constants.Module2NodeType.find(module)->second;
        Sandesh::InitGenerator(
                    module_name,
                    options.hostname(), 
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    g_vns_constants.INSTANCE_ID_DEFAULT,
                    Dns::GetEventManager(),
                    options.http_server_port(), 0,
                    options.collector_server_list(),
                    &sandesh_context);
    }
    Sandesh::SetLoggingParams(options.log_local(), options.log_category(),
                              options.log_level());

    // XXX Disable logging -- for test purposes only
    if (options.log_disable()) {
        SetLoggingDisabled(true);
    }

    // DNS::SetTestMode(options.test_mode());

    DB config_db;
    DBGraph config_graph;
    IFMapServer ifmap_server(&config_db, &config_graph,
                             Dns::GetEventManager()->io_service());
    sandesh_context.ifmap_server = &ifmap_server;
    IFMap_Initialize(&ifmap_server);

    DnsManager dns_manager;
    Dns::SetDnsManager(&dns_manager);
    dns_manager.Initialize(&config_db, &config_graph);
    DnsConfigParser parser(&config_db);
    parser.Parse(FileRead(options.config_file()));

    DnsAgentXmppManager::Init();
    start_time = UTCTimestampUsec();
    dns_info_trigger =
            new TaskTrigger(boost::bind(&DnsInfoLogger),
                    TaskScheduler::GetInstance()->GetTaskId("dns::Config"), 0);

    dns_info_log_timer = 
        TimerManager::CreateTimer(*(Dns::GetEventManager()->io_service()),
                                                   "Dns Info log timer");
    dns_info_log_timer->Start(60*1000, boost::bind(&DnsInfoLogTimer), NULL);

    //Register services with Discovery Service Server
    DiscoveryServiceClient *ds_client = NULL;
    if (!options.discovery_server().empty()) {
        tcp::endpoint dss_ep;
        boost::system::error_code error;
        dss_ep.address(address::from_string(options.discovery_server(), error));
        dss_ep.port(options.discovery_port());
        ds_client = new DiscoveryServiceClient(Dns::GetEventManager(), dss_ep,
            g_vns_constants.ModuleNames.find(Module::DNS)->second);
        ds_client->Init();
        Dns::SetDiscoveryServiceClient(ds_client);

        // Publish DNServer Service
        Dns::SetSelfIp(options.host_ip());

        if (!options.host_ip().empty()) {
            stringstream pub_ss;
            pub_ss << "<dns-server><ip-address>" << options.host_ip() <<
                      "</ip-address><port>" << options.dns_server_port() <<
                      "</port></dns-server>";
            std::string pub_msg;
            pub_msg = pub_ss.str();
            ds_client->Publish(DiscoveryServiceClient::DNSService, pub_msg);
        }

        //subscribe to collector service if not configured
        if (!options.collectors_configured()) {
            Module::type module = Module::DNS;
            NodeType::type node_type = 
                g_vns_constants.Module2NodeType.find(module)->second;
            string subscriber_name = 
                g_vns_constants.ModuleNames.find(module)->second;
            string node_type_name =
                g_vns_constants.NodeTypeNames.find(node_type)->second;
            Sandesh::CollectorSubFn csf = 0;
            csf = boost::bind(&DiscoveryServiceClient::Subscribe, ds_client,
                              _1, _2, _3);
            vector<string> list;
            list.clear();
            Sandesh::InitGenerator(subscriber_name,
                                   options.hostname(),
                                   node_type_name,
                                   g_vns_constants.INSTANCE_ID_DEFAULT,
                                   Dns::GetEventManager(),
                                   options.http_server_port(),
                                   csf,
                                   list,
                                   &sandesh_context);
        }
    }

    IFMapServerParser *ifmap_parser = IFMapServerParser::GetInstance("vnc_cfg");

    IFMapManager *ifmapmgr = new IFMapManager(&ifmap_server,
                options.ifmap_server_url(), options.ifmap_user(),
                options.ifmap_password(), options.ifmap_certs_store(),
                boost::bind(&IFMapServerParser::Receive, ifmap_parser,
                            &config_db, _1, _2, _3),
                        Dns::GetEventManager()->io_service(), ds_client);
    ifmap_server.set_ifmap_manager(ifmapmgr);

    Dns::GetEventManager()->Run();
 
    Dns::ShutdownDiscoveryClient(ds_client);

    return 0;
}
