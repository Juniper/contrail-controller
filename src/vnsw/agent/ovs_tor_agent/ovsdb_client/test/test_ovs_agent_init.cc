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

#include <boost/filesystem/operations.hpp>

#define OVSDB_SERVER "build/third_party/openvswitch/ovsdb/ovsdb-server"
#define DB_FILE_TEMPLATE "controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/vtep.db"

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

static void start_ovsdb_server() {
    assert(server_inited == false);
    signal(SIGABRT, signalHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGTERM, signalHandler);
    atexit(stop_ovsdb_server);
    server_inited = true;
    db_file_name =
        "build/debug/vnsw/agent/ovs_tor_agent/ovsdb_client/test/vtep_" +
        boost::lexical_cast<std::string>(getpid()) + "_" +
        boost::lexical_cast<std::string>(UTCTimestampUsec()) + ".db";
    lock_file_name = db_file_name;
    size_t pos = lock_file_name.find("vtep_");
    lock_file_name.insert(pos, ".");
    lock_file_name.append(".~lock~");
    // create a new DB for Ovsdb Server from DB Template file
    boost::filesystem::copy_file(DB_FILE_TEMPLATE, db_file_name);
    server_pid = fork();

    if (server_pid == 0) {
        execlp(OVSDB_SERVER, OVSDB_SERVER, "--remote=ptcp:0:127.0.0.1",
               db_file_name.c_str(), static_cast<char *>(NULL));
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
        start_ovsdb_server();
        ovsdb_client_.reset(new OVSDB::OvsdbClientTcp(agent(),
                    IpAddress(Ip4Address::from_string("127.0.0.1")), ovsdb_port,
                    IpAddress(Ip4Address::from_string("127.0.0.1")), 0,
                    ovs_peer_manager()));
        agent()->set_ovsdb_client(ovsdb_client_.get());
    }
}

void TestOvsAgentInit::CreateDBTables() {
    TestAgentInit::CreateDBTables();
}

void TestOvsAgentInit::RegisterDBClients() {
    TestAgentInit::RegisterDBClients();
    if (ovs_init_)
        ovsdb_client_->RegisterClients();
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

OVSDB::OvsdbClientTcp *TestOvsAgentInit::ovsdb_client() const {
    return ovsdb_client_.get();
}

void TestOvsAgentInit::set_ovs_init(bool ovs_init) {
    ovs_init_ = ovs_init;
}

void TestOvsAgentInit::KSyncShutdown() {
    ovsdb_client_->shutdown();
}

TestClient *OvsTestInit(const char *init_file, bool ovs_init) {
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
    param->set_test_mode(true);
    agent->set_ksync_sync_mode(true);

    // Initialize agent and kick start initialization
    init->Start();
    WaitForInitDone(agent);

    client->Init();
    client->WaitForIdle();
    client->SetFlowFlushExclusionPolicy();
    client->SetFlowAgeExclusionPolicy();

    AsioRun();

    return client;
}
