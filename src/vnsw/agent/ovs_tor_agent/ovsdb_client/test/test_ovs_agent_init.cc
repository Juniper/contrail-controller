/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <csetjmp>

#include <base/os.h>
#include <base/test/task_test_util.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent_factory.h>
#include <init/agent_param.h>
#include <cfg/cfg_init.h>
#include <ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h>

#include <oper/operdb_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/test/ksync_test.h>
#include <uve/agent_uve.h>
#include <uve/test/agent_uve_test.h>
#include "test/test_init.h"
#include <test/test_agent_init.h>
#include "test_ovs_agent_init.h"
#include "test-xml/test_xml_oper.h"
#include "test_xml_physical_device.h"
#include "test_xml_ovsdb.h"

#include <boost/filesystem/operations.hpp>

#define OVSDB_SERVER "build/third_party/openvswitch/ovsdb/ovsdb-server"
#define DB_FILE_TEMPLATE "controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/vtep.db"

std::string ssl_cert("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/ssl/tor-agent-self-cert.pem");
std::string ssl_priv("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/ssl/tor-agent-self-privkey.pem");
std::string ssl_cacert("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/ssl/cacert-tor-agent.pem");
std::string ovsdb_cert("--certificate=controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/ssl/ovsdb-cert.pem");
std::string ovsdb_priv("--private-key=controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/ssl/ovsdb-privkey.pem");
std::string ovsdb_cacert("--ca-cert=controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/ssl/tor-agent-self-cert.pem");

using namespace OVSDB;
std::string db_file_name;
std::string lock_file_name;
bool server_inited = false;
bool server_stopping = false;
int server_pid;
jmp_buf env;
uint32_t ovsdb_port = 0;

void stop_ovsdb_server() {
    if (server_inited == false || server_stopping == true) {
        return;
    }
    server_stopping = true;
    kill(server_pid, SIGTERM);
    int status;
    while (server_pid != wait(&status));
    // after the child process has exited, delete the db file and lock file
    boost::filesystem::remove(db_file_name);
    boost::filesystem::remove(lock_file_name);
    server_inited = false;
    server_stopping = false;
}

void signalHandler(int sig_num) {
    stop_ovsdb_server();
    longjmp (env, 1);
}

static void start_ovsdb_server(bool use_ssl) {
    assert(server_inited == false);
    signal(SIGABRT, signalHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGTERM, signalHandler);
    atexit(stop_ovsdb_server);
    server_inited = true;
    db_file_name =
        "build/vtep_" + boost::lexical_cast<std::string>(getpid()) + "_" +
        boost::lexical_cast<std::string>(UTCTimestampUsec()) + ".db";
    lock_file_name = db_file_name;
    size_t pos = lock_file_name.find("vtep_");
    lock_file_name.insert(pos, ".");
    lock_file_name.append(".~lock~");
    // create a new DB for Ovsdb Server from DB Template file
    boost::filesystem::copy_file(DB_FILE_TEMPLATE, db_file_name);
    server_pid = fork();

    if (server_pid == 0) {
        if (use_ssl) {
            std::string remote_str = "--remote=ssl:127.0.0.1:" +
                integerToString(ovsdb_port);
            execlp(OVSDB_SERVER, OVSDB_SERVER, remote_str.c_str(),
                   ovsdb_cert.c_str(), ovsdb_priv.c_str(),
                   ovsdb_cacert.c_str(),
                   db_file_name.c_str(), static_cast<char *>(NULL));
        } else {
            execlp(OVSDB_SERVER, OVSDB_SERVER, "--remote=ptcp:0:127.0.0.1",
                   db_file_name.c_str(), static_cast<char *>(NULL));
        }
    }

    if (use_ssl) {
        // nothing more to do return from here
        return;
    }

    std::string port = "";
    // Get the TCP server port used by ovsdb-server.
    int count = 0;
    do {
        usleep(1000);
        assert(count < 10000);
        count++;
        std::string cmd("netstat -anp | grep tcp | grep ");
        cmd += boost::lexical_cast<std::string>(server_pid);
        cmd += "/ovsdb-server";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) assert(false);
        char buffer[128];
        std::string result;
        while(!feof(pipe)) {
            if(fgets(buffer, 128, pipe) != NULL)
                result += buffer;
        }
        pclose(pipe);
        size_t start = result.find("127.0.0.1:");
        // find will fail if the process has not started the listen on socket
        if (start == string::npos)
            continue;
        start += 10;
        size_t end = result.find_first_of(' ', start);
        assert(end != string::npos);
        port = result.substr(start, end - start);
    } while (port.empty());
    ovsdb_port = strtoul(port.c_str(), NULL, 0);
}

OvsdbClientTcpSessionTest::OvsdbClientTcpSessionTest(Agent *agent,
                                                     OvsPeerManager *manager,
                                                     TcpServer *server,
                                                     Socket *sock,
                                                     bool async_ready)
    : OvsdbClientTcpSession(agent, manager, server, sock, async_ready) {
}

OvsdbClientTcpSessionTest::~OvsdbClientTcpSessionTest() {
}

OvsdbClientTcpTest::OvsdbClientTcpTest(Agent *agent, IpAddress tor_ip,
                                       int tor_port, IpAddress tsn_ip,
                                       int keepalive, OvsPeerManager *manager)
    : OvsdbClientTcp(agent, tor_ip, tor_port, tsn_ip, keepalive, -1, manager),
    enable_connect_(true) {
}

OvsdbClientTcpTest::~OvsdbClientTcpTest() {
}

TcpSession *OvsdbClientTcpTest::AllocSession(Socket *socket) {
    TcpSession *session = new OvsdbClientTcpSessionTest(agent_, peer_manager_,
                                                        this, socket);
    session->set_observer(boost::bind(&OvsdbClientTcp::OnSessionEvent,
                                      this, _1, _2));
    return session;
}

void OvsdbClientTcpTest::Connect(TcpSession *session, Endpoint remote) {
    if (enable_connect_) {
        OvsdbClientTcp::Connect(session, remote);
    }
}

void OvsdbClientTcpTest::set_enable_connect(bool enable) {
    if (enable_connect_ != enable) {
        enable_connect_ = enable;
        if (enable_connect_) {
            Connect(session_, server_ep_);
        }
    }
}

TestOvsAgentInit::TestOvsAgentInit() : TestAgentInit() {
}

TestOvsAgentInit::~TestOvsAgentInit() {
}

/****************************************************************************
 * Initialization routines
 ***************************************************************************/
// Create the basic modules for agent operation.
// Optional modules or modules that have different implementation are created
// by init module
void TestOvsAgentInit::CreateModules() {
    TestAgentInit::CreateModules();
    if (ovs_init_) {
        if (use_ssl_) {
            ovsdb_client_.reset(new OVSDB::OvsdbClientSsl(agent(),
                        IpAddress(Ip4Address::from_string("127.0.0.1")), 0,
                        IpAddress(Ip4Address::from_string("127.0.0.2")), 0, -1,
                        ssl_cert, ssl_priv, ssl_cacert, ovs_peer_manager()));
            agent()->set_ovsdb_client(ovsdb_client_.get());
        } else {
            start_ovsdb_server(false);
            ovsdb_client_.reset(new OVSDB::OvsdbClientTcpTest(agent(),
                        IpAddress(Ip4Address::from_string("127.0.0.1")), ovsdb_port,
                        IpAddress(Ip4Address::from_string("127.0.0.2")), 0,
                        ovs_peer_manager()));
            agent()->set_ovsdb_client(ovsdb_client_.get());
        }
    }
}

void TestOvsAgentInit::CreateDBTables() {
    TestAgentInit::CreateDBTables();
}

void TestOvsAgentInit::RegisterDBClients() {
    TestAgentInit::RegisterDBClients();
    if (ovs_init_) {
        ovsdb_client_->RegisterClients();
        if (use_ssl_) {
            // ssl server port will be available only after
            // RegisterClients, get the port and start ovsdb server
            ovsdb_port = ovsdb_client_->port();
            start_ovsdb_server(true);
        }
    }
}

void TestOvsAgentInit::CreatePeers() {
    ovs_peer_manager_.reset(new OvsPeerManager(agent()));
}

/****************************************************************************
 * Access routines
 ****************************************************************************/
OvsPeerManager *TestOvsAgentInit::ovs_peer_manager() const {
    return ovs_peer_manager_.get();
}

OVSDB::OvsdbClient *TestOvsAgentInit::ovsdb_client() const {
    return ovsdb_client_.get();
}

void TestOvsAgentInit::set_ovs_init(bool ovs_init) {
    ovs_init_ = ovs_init;
}

void TestOvsAgentInit::set_use_ssl(bool use_ssl) {
    use_ssl_ = use_ssl;
}

void TestOvsAgentInit::KSyncShutdown() {
    if (ovsdb_client_.get())
        ovsdb_client_->shutdown();
    KSyncTest *ksync = static_cast<KSyncTest *>(client->agent()->ksync());
    ksync->Shutdown();
}

void LoadAndRun(const std::string &file_name) {
    AgentUtXmlTest test(file_name);
    AgentUtXmlOperInit(&test);
    AgentUtXmlPhysicalDeviceInit(&test);
    AgentUtXmlOvsdbInit(&test);
    if (test.Load() == true) {
        test.ReadXml();
        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

bool LoadXml(AgentUtXmlTest &test) {
    AgentUtXmlOperInit(&test);
    AgentUtXmlPhysicalDeviceInit(&test);
    AgentUtXmlOvsdbInit(&test);
    if (test.Load() == true) {
        test.ReadXml();
        string str;
        test.ToString(&str);
        cout << str << endl;
        return true;
    }
    return false;
}

TestClient *OvsTestInit(const char *init_file, bool ovs_init, bool use_ssl) {
    TestClient *client = new TestClient(new TestOvsAgentInit());
    TestOvsAgentInit *init =
        static_cast<TestOvsAgentInit *>(client->agent_init());
    Agent *agent = client->agent();

    AgentParam *param = client->param();
    init->set_agent_param(param);
    // Read agent parameters from config file and arguments
    init->ProcessOptions(init_file, "test");
    param->set_agent_stats_interval(AgentParam::kAgentStatsInterval);
    param->set_flow_stats_interval(AgentParam::kFlowStatsInterval);
    param->set_vrouter_stats_interval(AgentParam::kVrouterStatsInterval);

    // Initialize the agent-init control class
    int introspect_port = 0;
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               agent->event_manager(), introspect_port, NULL);

    init->set_ksync_enable(false);
    init->set_packet_enable(false);
    init->set_services_enable(false);
    init->set_create_vhost(false);
    init->set_uve_enable(true);
    init->set_vgw_enable(false);
    init->set_router_id_dep_enable(false);
    init->set_ovs_init(ovs_init);
    init->set_use_ssl(use_ssl);
    param->set_test_mode(true);
    agent->set_ksync_sync_mode(true);

    // Initialize agent and kick start initialization
    init->Start();
    WaitForInitDone(agent);

    client->Init();
    client->WaitForIdle();
    client->SetFlowFlushExclusionPolicy();

    AsioRun();

    return client;
}
