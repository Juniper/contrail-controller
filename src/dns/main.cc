/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

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
using boost::system::error_code;
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

static int dns_main(int argc, char *argv[]) {
    Dns::Init();

    error_code error;

    // Specify defaults for all options possible.
    string collector_server;
    unsigned short collector_port = ContrailPorts::CollectorPort;
    string dns_config_file = "dns_config.xml";
    string discovery_server;
    uint16_t discovery_port = ContrailPorts::DiscoveryServerPort;
    string hostname(boost::asio::ip::host_name(error));
    string hostip = GetHostIp(Dns::GetEventManager()->io_service(), hostname);
    uint16_t http_server_port = ContrailPorts::HttpPortDns;
    string log_category = "";
    bool disable_logging = false;
    string log_file = "<stdout>";
    string log_level = "SYS_NOTICE";
    bool log_local = false;

    string ifmap_server_url;
    string ifmap_password = "control-user-passwd";
    string ifmap_user = "control-user";
    string certs_store = "";
    string config_file = "/etc/contrail/dns.conf";

    // Command line only options.
    opt::options_description generic("Generic options");
    generic.add_options()
        ("conf-file", opt::value<string>()->default_value(config_file),
             "Configuration file")
        ("version", "Display version information")
        ("help", "help message")
    ;

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
        ("COLLECTOR.port",
             opt::value<uint16_t>()->default_value(collector_port),
             "Port of sandesh collector")
        ("COLLECTOR.server",
             opt::value<string>()->default_value(collector_server),
             "IP address of sandesh collector")

        ("DEFAULTS.dns-config-file",
             opt::value<string>()->default_value(dns_config_file),
             "DNS Configuration file")
        ("DEFAULTS.hostip", opt::value<string>()->default_value(hostip),
             "IP address of control-node")
        ("DEFAULTS.http-server-port",
             opt::value<uint16_t>()->default_value(http_server_port),
             "Sandesh HTTP listener port")

        ("DISCOVERY.port", opt::value<uint16_t>()->default_value(discovery_port),
            "Port of Discovery Server")
        ("DISCOVERY.server",
             opt::value<string>()->default_value(discovery_server),
              "IP address of Discovery Server")

        ("IFMAP.password", opt::value<string>()->default_value(ifmap_password),
             "IFMAP server password")
        ("IFMAP.server-url",
             opt::value<string>()->default_value(ifmap_server_url),
             "IFMAP server URL")
        ("IFMAP.user", opt::value<string>()->default_value(ifmap_user),
             "IFMAP server username")
        ("IFMAP.certs-store", opt::value<string>()->default_value(certs_store),
             "Certificates store to use for communication with IFMAP server")

        ("LOG.category", opt::value<string>()->default_value(log_category),
            "Category filter for local logging of sandesh messages")
        ("LOG.disable", opt::bool_switch(&disable_logging),
             "Disable sandesh logging")
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

    vector<string> tokens;
    string line;
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
        Dns::GetVersion(build_info_str);
        cout << build_info_str << endl;
        exit(0);
    }

    GetOptValue<uint16_t>(var_map, collector_port, "COLLECTOR.port", 0);
    GetOptValue<string>(var_map, collector_server, "COLLECTOR.server", "");
    GetOptValue<string>(var_map, dns_config_file, "DEFAULTS.dns-config-file", "");
    GetOptValue<string>(var_map, hostip, "DEFAULTS.hostip", "");
    GetOptValue<uint16_t>(var_map, http_server_port, "DEFAULTS.http-server-port", 0);
    GetOptValue<uint16_t>(var_map, discovery_port, "DISCOVERY.port", 0);
    GetOptValue<string>(var_map, discovery_server, "DISCOVERY.server", "");
    GetOptValue<string>(var_map, log_category, "LOG.category", "");
    GetOptValue<string>(var_map, log_file, "LOG.file", "");
    GetOptValue<string>(var_map, log_level, "LOG.level", "");
    GetOptValue<string>(var_map, ifmap_password, "IFMAP.password", "");
    GetOptValue<string>(var_map, ifmap_server_url, "IFMAP.server-url", "");
    GetOptValue<string>(var_map, ifmap_user, "IFMAP.user", "");
    GetOptValue<string>(var_map, certs_store, "IFMAP.certs-store", ""); 
    if (log_file == "<stdout>") {
        LoggingInit();
    } else {
        LoggingInit(log_file);
    }
    string build_info_str;
    Dns::GetVersion(build_info_str);
    MiscUtils::LogVersionInfo(build_info_str, Category::DNSAGENT);
    // Create DB table and event manager
    Dns::Init();

    if (!collector_server.empty()) {
        Dns::SetCollector(collector_server);
    }
    Dns::SetHttpPort(http_server_port);

    BgpSandeshContext sandesh_context;
    Dns::SetHostName(hostname);
    if (discovery_server.empty()) {
        Module::type module = Module::DNS;
        NodeType::type node_type = 
            g_vns_constants.Module2NodeType.find(module)->second;
        Sandesh::InitGenerator(
                    g_vns_constants.ModuleNames.find(module)->second,
                    hostname,
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    g_vns_constants.INSTANCE_ID_DEFAULT,
                    Dns::GetEventManager(),
                    http_server_port, &sandesh_context);
    }
    if ((collector_port != 0) && (!collector_server.empty())) {
        Sandesh::ConnectToCollector(collector_server, collector_port);
    }
    Sandesh::SetLoggingParams(log_local, log_category, log_level);

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
    parser.Parse(FileRead(dns_config_file));

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
    if (!discovery_server.empty()) {
        error_code ec;
        tcp::endpoint dss_ep;
        dss_ep.address(address::from_string(discovery_server, ec));
        dss_ep.port(discovery_port);
        ds_client = new DiscoveryServiceClient(Dns::GetEventManager(), dss_ep,
            g_vns_constants.ModuleNames.find(Module::DNS)->second);
        ds_client->Init();

        // Publish DNServer Service
        Dns::SetSelfIp(hostip);

        if (!hostip.empty()) {
            stringstream pub_ss;
            pub_ss << "<dns-server><ip-address>" << hostip <<
                      "</ip-address><port>" << ContrailPorts::DnsXmpp <<
                      "</port></dns-server>";
            std::string pub_msg;
            pub_msg = pub_ss.str();
            ds_client->Publish(DiscoveryServiceClient::DNSService, pub_msg);
        }

        //subscribe to collector service if not configured
        if (collector_server.empty()) {
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
                                   hostname, node_type_name,
                                   g_vns_constants.INSTANCE_ID_DEFAULT,
                                   Dns::GetEventManager(),
                                   http_server_port,
                                   csf,
                                   list,
                                   &sandesh_context);
        }
    }

    if (!ifmap_server_url.empty()) {
        IFMapServerParser *ifmap_parser = IFMapServerParser::GetInstance("vnc_cfg");

        IFMapManager *ifmapmgr = new IFMapManager(&ifmap_server, ifmap_server_url,
                                                ifmap_user, ifmap_password, certs_store,
                                        boost::bind(&IFMapServerParser::Receive, ifmap_parser,
                                                    &config_db, _1, _2, _3),
                                        Dns::GetEventManager()->io_service(), ds_client);
        ifmap_server.set_ifmap_manager(ifmapmgr);
    }

    Dns::GetEventManager()->Run();
 
    Dns::ShutdownDiscoveryClient(ds_client);

    return 0;
}

int main(int argc, char *argv[]) {
    try {
        return dns_main(argc, argv);
    } catch (boost::program_options::error &e) {
        LOG(ERROR, "Error " << e.what());
        cout << "Error " << e.what();
    } catch (...) {
        LOG(ERROR, "Options Parser: Caught fatal unknown exception");
        cout << "Options Parser: Caught fatal unknown exception";
    }

    return(-1);
}
