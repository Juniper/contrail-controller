/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>

#include <pthread.h>
#include <boost/program_options.hpp>
#include <base/logging.h>
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
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <uve/uve.h>
#include <base/misc_utils.h>

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

int main(int argc, char *argv[]) {
    bool enable_local_logging = false;
    const string default_log_file = "<stdout>";
    opt::options_description desc("Command line options");
    desc.add_options()
            ("help", "help message")
            ("config-file", 
                opt::value<string>()->default_value("dns_config.xml"),
                "Configuration file") 
            ("discovery-server", opt::value<string>(),
             "IP address of Discovery Server")
            ("discovery-port",
             opt::value<int>()->default_value(ContrailPorts::DiscoveryServerPort),
             "Port of Discovery Server")
            ("map-server-url", opt::value<string>(), "MAP server URL")
            ("map-user", opt::value<string>(), "MAP server username")
            ("map-password", opt::value<string>(), "MAP server password")
            ("log-local", opt::bool_switch(&enable_local_logging),
                "Enable local logging of sandesh messages")
            ("log-level", opt::value<string>()->default_value("SYS_NOTICE"),
                "Severity level for local logging of sandesh messages")
            ("log-category", opt::value<string>()->default_value(""),
                "Category filter for local logging of sandesh messages")
            ("collector", opt::value<string>(), "IP address of sandesh collector")
            ("collector-port", opt::value<int>(), "Port of sandesh collector")
            ("http-server-port",
                opt::value<int>()->default_value(
                ContrailPorts::HttpPortDns), "Sandesh HTTP listener port")
            ("host-ip", opt::value<string>(),
             "IP address of DNServer")
            ("use-certs", opt::value<string>(),
                "Use certificates to communicate with MAP server; Specify certificate store")
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
        Dns::GetVersion(build_info_str);
        cout << build_info_str << endl;
        exit(0);
    }

    if (var_map["log-file"].as<string>() == default_log_file) {
        LoggingInit();
    } else {
        LoggingInit(var_map["log-file"].as<string>());
    }
    string build_info_str;
    Dns::GetVersion(build_info_str);
    MiscUtils::LogVersionInfo(build_info_str, Category::DNSAGENT);
    // Create DB table and event manager
    Dns::Init();

    int collector_server_port = 0;
    if (var_map.count("collector-port")) {
        collector_server_port = var_map["collector-port"].as<int>();
    }
    std::string collector_server;
    if (var_map.count("collector")) {
        collector_server = var_map["collector"].as<string>();
        Dns::SetCollector(collector_server);
    }
    int sandesh_http_port = var_map["http-server-port"].as<int>();
    Dns::SetHttpPort(sandesh_http_port);

    BgpSandeshContext sandesh_context;
    boost::system::error_code ec;
    string hostname = host_name(ec);
    Dns::SetHostName(hostname);
    if (!var_map.count("discovery-server")) {
        Sandesh::InitGenerator(
                    g_vns_constants.ModuleNames.find(Module::DNS)->second,
                    hostname,
                    Dns::GetEventManager(),
                    sandesh_http_port, &sandesh_context);
    }
    if ((collector_server_port != 0) && (!collector_server.empty())) {
        Sandesh::ConnectToCollector(collector_server, collector_server_port);
    }
    Sandesh::SetLoggingParams(enable_local_logging,
                              var_map["log-category"].as<string>(),
                              var_map["log-level"].as<string>());

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
    parser.Parse(FileRead(var_map["config-file"].as<string>()));

    Dns::SetProgramName(argv[0]);
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
    if (var_map.count("discovery-server")) {
        tcp::endpoint dss_ep;
        dss_ep.address(
            address::from_string(var_map["discovery-server"].as<string>(), ec));
        dss_ep.port(var_map["discovery-port"].as<int>());
        ds_client = new DiscoveryServiceClient(Dns::GetEventManager(), dss_ep);
        ds_client->Init();

        // Publish DNServer Service
        string self_ip; 
        if (var_map.count("host-ip")) {
            self_ip = var_map["host-ip"].as<string>();
        } else {
            tcp::resolver resolver(*(Dns::GetEventManager()->io_service()));
            tcp::resolver::query query(host_name(ec), "");
            tcp::resolver::iterator iter = resolver.resolve(query);
            self_ip = iter->endpoint().address().to_string();
        }
        Dns::SetSelfIp(self_ip);

        if (!self_ip.empty()) {
            stringstream pub_ss;
            pub_ss << "<dns-server><ip-address>" << self_ip <<
                      "</ip-address><port>" << ContrailPorts::DnsXmpp <<
                      "</port></dns-server>";
            std::string pub_msg;
            pub_msg = pub_ss.str();
            ds_client->Publish(DiscoveryServiceClient::DNSService, pub_msg);
        }

        //subscribe to collector service if not configured
        if (!var_map.count("collector")) {
            string subscriber_name = 
                g_vns_constants.ModuleNames.find(Module::DNS)->second;

            Sandesh::CollectorSubFn csf = 0;
            csf = boost::bind(&DiscoveryServiceClient::Subscribe, ds_client,
                              subscriber_name, _1, _2, _3);
            vector<string> list;
            list.clear();
            Sandesh::InitGenerator(subscriber_name,
                                   hostname, Dns::GetEventManager(),
                                   sandesh_http_port,
                                   csf,
                                   list,
                                   &sandesh_context);
        }
    }

    std::string certstore = var_map.count("use-certs") ? 
                            var_map["use-certs"].as<string>() : string("");
    std::string map_server_url;
    if (var_map.count("map-server-url")) {
        map_server_url = var_map["map-server-url"].as<string>();
    }
    IFMapServerParser *ifmap_parser = IFMapServerParser::GetInstance("vnc_cfg");

    IFMapManager *ifmapmgr = new IFMapManager(&ifmap_server, map_server_url,
                        var_map["map-user"].as<string>(),
                        var_map["map-password"].as<string>(), certstore,
                        boost::bind(&IFMapServerParser::Receive, ifmap_parser,
                                &config_db, _1, _2, _3),
                        Dns::GetEventManager()->io_service(), ds_client,
                        g_vns_constants.ModuleNames.find(Module::DNS)->second);
    ifmap_server.set_ifmap_manager(ifmapmgr);

    Dns::GetEventManager()->Run();
 
    Dns::ShutdownDiscoveryClient(ds_client);

    return 0;
}
