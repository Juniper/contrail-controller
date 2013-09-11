/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_init.h"
#include "oper/mirror_table.h"

static AgentTestInit *agent_init;

pthread_t asio_thread;

void *asio_poll(void *arg){
    Agent::GetInstance()->GetEventManager()->Run();
    return NULL;
}

void AsioRun() {
    pthread_attr_t attr;
    int ret;

    pthread_attr_init(&attr);
    if ((ret = pthread_create(&asio_thread, &attr, asio_poll, NULL)) != 0) {
        LOG(ERROR, "pthread_create error : " <<  strerror(ret) );
        assert(0);
    }
}

void AsioStop() {
    pthread_join(asio_thread, NULL);
}

bool AgentTestInit::Run() {

    /*
     * Initialization sequence
     * 0> Read config store from kernel, if available, to replay the Nova config
     * 1> Create all DB table
     * 2> Register listener for DB tables
     * 3> Initialize data which updates entries in DB table
     * By above init sequence no notification to client will be lost
     */

    switch(state_) {
        case MOD_INIT:
            Sandesh::InitGeneratorTest("VNSWAgent", "Agent", 
                                   Agent::GetInstance()->GetEventManager(), 
                                   sandesh_port_, NULL);
            Sandesh::SetLoggingParams(log_locally_, "", "");
            InitModules();
            state_ = STATIC_OBJ_OPERDB;
            // Continue with the next state

        case STATIC_OBJ_OPERDB: {
            OperDB::CreateStaticObjects(boost::bind(&AgentTestInit::Trigger, this));
            state_ = STATIC_OBJ_PKT;
            break;
        }

        case STATIC_OBJ_PKT:
            PktModule::CreateStaticObjects();
            state_ = CONFIG_INIT;
            // Continue with the next state

        case CONFIG_INIT:
            state_ = CONFIG_RUN;
            if (init_file_) {
                AgentConfig::Init(Agent::GetInstance()->GetDB(), init_file_, 
                                  boost::bind(&AgentTestInit::Trigger, this));
                // ServicesModule::ConfigInit();
                break;
            }
            // else Continue with the next state

        case CONFIG_RUN:
            if (ksync_init_) {
                KSync::VnswIfListenerInit();
            } else {
                //KSync::VnswIfListenerInit();
            }

            if (ksync_init_) {
                //Update MAC address of vhost interface
                //with that of ethernet interface
                KSync::UpdateVhostMac();
            }

            if (Agent::GetInstance()->GetRouterIdConfigured()) {
                // RouterIdDepInit();
            } else {
                LOG(DEBUG, 
                    "Router ID Dependent modules (Nova & BGP) not initialized");
            }

            state_ = INIT_DONE;
            // Continue with the next state

        case INIT_DONE: {
            break;
        }

        case SHUTDOWN: {
            client->Shutdown();
            state_ = SHUTDOWN_DONE;
            break;
        }

        default:
            assert(0);
    }

    return true;
}

void AgentTestInit::InitModules() {
    if (ksync_init_) {
        KSync::NetlinkInit();
        KSync::ResetVRouter();
        KSync::CreateVhostIntf();
    } else {
        KSync::NetlinkInitTest();
    }

    CfgModule::CreateDBTables(Agent::GetInstance()->GetDB());
    OperDB::CreateDBTables(Agent::GetInstance()->GetDB());
 
    CfgModule::RegisterDBClients(Agent::GetInstance()->GetDB());

    MirrorCfgTable *mtable = MirrorCfgTable::CreateMirrorCfgTable();
    Agent::GetInstance()->SetMirrorCfgTable(mtable);
    
    Agent::GetInstance()->SetIntfMirrorCfgTable(IntfMirrorCfgTable::CreateIntfMirrorCfgTable());

    if (ksync_init_) {
        KSync::RegisterDBClients(Agent::GetInstance()->GetDB());
    } else {
        KSync::RegisterDBClientsTest(Agent::GetInstance()->GetDB());
    }

    if (client_)
        client_->Init();

    if (pkt_init_) {
        // if ksync_init_, use normal tap, otherwise test_tap
        PktModule::Init(ksync_init_);  
    }

    if (services_init_) {
        ServicesModule::Init(ksync_init_);
    }

    if (uve_init_) {
        AgentUve::Init(interval_);
    }
    MulticastHandler::Register();
}

TestClient *TestInit(const char *init_file, bool ksync_init, bool pkt_init,
                     bool services_init, bool uve_init, int interval,
                     bool asio) {
    int sandesh_port = 0;
    bool log_locally = false;
    AgentCmdLineParams cmd_line("", 0, "", "", log_locally, "", "", sandesh_port, "");
    LoggingInit();
    Agent::Init();
    AgentConfig::InitConfig(init_file, cmd_line);
    Agent::GetInstance()->SetTestMode();
    AgentStats::Init();
    if (asio) {
        AsioRun();
    }

    TestClient *client = new TestClient();

    agent_init =
        new AgentTestInit(ksync_init, pkt_init, services_init,
                          init_file, sandesh_port, log_locally, 
                          client, uve_init, interval);
    agent_init->Trigger();

    while (agent_init->GetState() != AgentTestInit::INIT_DONE) {
        usleep(1000);
    }

    client->WaitForIdle();
    int port_count = 0;
    if (pkt_init)
        port_count++; // pkt0
    if (init_file)
        port_count += 2; // vhost, eth
    else {
        Agent::GetInstance()->SetVirtualHostInterfaceName("vhost0");
        VirtualHostInterface::CreateReq("vhost0", Agent::GetInstance()->GetDefaultVrf(), false);
        port_count += 1; // vhost
        boost::system::error_code ec;
        Agent::GetInstance()->SetRouterId(Ip4Address::from_string("10.1.1.1", ec));
        //Add a receive router
        Agent::GetInstance()->GetDefaultInet4UcRouteTable()->AddVHostRecvRoute(
                                         Agent::GetInstance()->GetDefaultVrf(), "vhost0", 
                                         Agent::GetInstance()->GetRouterId(), false);
    }

    return client;
}

TestClient *StatsTestInit() {
    LoggingInit();
    Agent::GetInstance()->Init();
    AgentStats::Init();
    AsioRun();

    TestClient *client = new TestClient();

    bool ksync_init = true;
    bool pkt_init = true;
    bool services_init = true;
    char *init_file = NULL;
    int sandesh_port = 0;
    bool log_locally = false;
    bool uve_init = true;
    AgentTestInit *agent_init =
        new AgentTestInit(ksync_init, pkt_init, services_init,
                          init_file, sandesh_port, log_locally, 
                          client, uve_init, StatsCollector::stats_coll_time);
    agent_init->Trigger();

    sleep(6);
    Agent::GetInstance()->SetVirtualHostInterfaceName("vhost0");
    VirtualHostInterface::CreateReq("vhost0", Agent::GetInstance()->GetDefaultVrf(), false);
    boost::system::error_code ec;
    Agent::GetInstance()->SetRouterId(Ip4Address::from_string("10.1.1.1", ec));

    // Wait for host and vhost interface creation
    client->WaitForIdle();

    return client;
}

static bool WaitForDbCount(DBTableBase *table, uint32_t count, int msec) {
    int i = 0;

    msec = msec * 1000;
    while ((table->Size() > count)  && (i < msec)) {
        usleep(1000);
        i += 1000;
    }

    return (table->Size() == count);
}

static bool WaitForDbFree(const string &name, int msec) {
    int i = 0;

    msec = msec * 1000;
    while (i < msec) {
        if (Agent::GetInstance()->GetDB()->FindTable(name) == NULL) {
            break;
        }

        usleep(1000);
        i += 1000;
    }

    return (Agent::GetInstance()->GetDB()->FindTable(name) == NULL);
}

void TestClient::Shutdown() {
    VnswIfListener::Shutdown();
    AgentConfig::Shutdown();
    MirrorCfgTable::Shutdown();
    AgentUve::GetInstance()->Shutdown();
    CfgModule::Shutdown();
    UveClient::GetInstance()->Shutdown();
    KSync::NetlinkShutdownTest();
    KSync::Shutdown();
    PktModule::Shutdown();  
    ServicesModule::Shutdown();
    MulticastHandler::Shutdown();
    OperDB::Shutdown();
    Agent::GetInstance()->GetDB()->Clear();
    Agent::GetInstance()->GetDB()->ClearFactoryRegistry();
}

void TestShutdown() {
    client->WaitForIdle();

    VNController::DisConnect();
    client->WaitForIdle();

    AgentConfig::DeleteStaticEntries();
    client->WaitForIdle();

    WaitForDbCount(Agent::GetInstance()->GetInterfaceTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetInterfaceTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetVrfTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetVrfTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetNextHopTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetNextHopTable()->Size());

    WaitForDbFree(Agent::GetInstance()->GetDefaultVrf(), 100);
    assert(Agent::GetInstance()->GetDB()->FindTable(Agent::GetInstance()->GetDefaultVrf()) == NULL);

    WaitForDbCount(Agent::GetInstance()->GetVmTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetVmTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetVnTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetVnTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetMplsTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetMplsTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetIntfCfgTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetIntfCfgTable()->Size());

    WaitForDbCount(Agent::GetInstance()->GetAclTable(), 0, 100);
    EXPECT_EQ(0U, Agent::GetInstance()->GetAclTable()->Size());
    client->WaitForIdle();

    agent_init->Shutdown();
    client->WaitForIdle();

    Sandesh::Uninit();
    client->WaitForIdle();

    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    TaskScheduler::GetInstance()->Terminate();

    AgentStats::GetInstance()->Shutdown();
    Agent::GetInstance()->Shutdown();
    delete agent_init;
}
