/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <cmn/agent_factory.h>
#include <init/agent_param.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>

#include <oper/operdb_init.h>
#include <oper/sg.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface.h>
#include <oper/route_common.h>
#include <filter/acl.h>
#include <controller/controller_init.h>

#include "agent_init.h"

AgentInit::AgentInit() :
    agent_(new Agent()), agent_param_(NULL), trigger_(),
    enable_controller_(true) {
}

AgentInit::~AgentInit() {
    TaskScheduler *scheduler = agent_->task_scheduler();
    trigger_.reset();
    controller_.reset();
    cfg_.reset();
    oper_.reset();
    agent_->db()->ClearFactoryRegistry();
    agent_.reset();

    scheduler->Terminate();
}

/****************************************************************************
 * Initialization routines
****************************************************************************/
void AgentInit::ProcessOptions
    (const std::string &config_file, const std::string &program_name) {
    agent_param_->Init(config_file, program_name);
}

string AgentInit::ModuleName() {
    Module::type module = Module::VROUTER_AGENT;
    return g_vns_constants.ModuleNames.find(module)->second;
}

string AgentInit::InstanceId() {
    return g_vns_constants.INSTANCE_ID_DEFAULT;
}

// Start of Agent init.
// Trigger init in DBTable task context
int AgentInit::Start() {
    // Call to GetScheduler::GetInstance() will also create Task Scheduler
    if (TaskScheduler::GetInstance() == NULL) {
        TaskScheduler::Initialize();
    }
    agent_->set_task_scheduler(TaskScheduler::GetInstance());
    string module_name = ModuleName();
    agent_->set_discovery_client_name(module_name);
    agent_->set_instance_id(InstanceId());

    // Copy tunable parameters into agent_
    agent_->CopyConfig(agent_param_);

    LoggingInit(agent_param_->log_file(), agent_param_->log_file_size(),
                agent_param_->log_files_count(), agent_param_->use_syslog(),
                agent_param_->syslog_facility(), module_name);

    agent_param_->LogConfig();

    int ret = agent_param_->Validate();
    if (ret != 0) {
        return ret;
    }

    int task_id = agent_->task_scheduler()->GetTaskId("db::DBTable");
    trigger_.reset(new TaskTrigger(boost::bind(&AgentInit::InitBase, this),
                                   task_id, 0));
    trigger_->Set();
    return 0;
}

// Start init sequence
bool AgentInit::InitBase() {
    FactoryInit();
    InitLoggingBase();
    InitCollectorBase();
    CreatePeersBase();
    CreateModulesBase();
    CreateDBTablesBase();
    RegisterDBClientsBase();
    InitModulesBase();
    CreateVrfBase();
    CreateNextHopsBase();
    InitDiscoveryBase();
    CreateInterfacesBase();
    InitDoneBase();

    bool ret = Init();
    agent_->set_init_done(true);
    ConnectToControllerBase();
    return ret;
}

void AgentInit::InitLoggingBase() {
    Sandesh::SetLoggingParams(agent_param_->log_local(),
                              agent_param_->log_category(),
                              agent_param_->log_level(),
                              false,
                              agent_param_->log_flow());
    InitLogging();
}

// Connect to collector specified in config, if discovery server is not set
void AgentInit::InitCollectorBase() {
    agent_->InitCollector();
    InitCollector();
}

// Create peers
void AgentInit::CreatePeersBase() {
    agent_->InitPeers();
    CreatePeers();
}

// Create the basic modules for agent operation.
// Optional modules or modules that have different implementation are created
// by init module
void AgentInit::CreateModulesBase() {
    cfg_.reset(new AgentConfig(agent()));
    agent_->set_cfg(cfg_.get());

    oper_.reset(new OperDB(agent()));
    agent_->set_oper_db(oper_.get());

    if (enable_controller_) {
        controller_.reset(new VNController(agent()));
    }
    agent_->set_controller(controller_.get());

    CreateModules();
}

void AgentInit::CreateDBTablesBase() {
    if (cfg_.get()) {
        cfg_->CreateDBTables(agent_->db());
    }

    if (oper_.get()) {
        oper_->CreateDBTables(agent_->db());
    }

    CreateDBTables();
}

void AgentInit::RegisterDBClientsBase() {
    if (cfg_.get()) {
        cfg_->RegisterDBClients(agent_->db());
    }

    if (oper_.get()) {
        oper_->RegisterDBClients();
    }

    RegisterDBClients();
}

void AgentInit::InitModulesBase() {
    if (cfg_.get()) {
        cfg_->Init();
    }

    if (oper_.get()) {
        oper_->Init();
    }

    InitModules();
}

void AgentInit::CreateVrfBase() {
    // Create the default VRF
    VrfTable *vrf_table = agent_->vrf_table();

    vrf_table->CreateStaticVrf(agent_->fabric_vrf_name());
    VrfEntry *vrf = vrf_table->FindVrfFromName(agent_->fabric_vrf_name());
    assert(vrf);

    agent_->set_fabric_inet4_unicast_table(vrf->GetInet4UnicastRouteTable());
    agent_->set_fabric_inet4_multicast_table
        (vrf->GetInet4MulticastRouteTable());
    agent_->set_fabric_l2_unicast_table(vrf->GetLayer2RouteTable());

    CreateVrf();
}

void AgentInit::CreateNextHopsBase() {
    DiscardNH::Create();
    ResolveNH::Create();

    DiscardNHKey key;
    NextHop *nh = static_cast<NextHop *>
                (agent_->nexthop_table()->FindActiveEntry(&key));
    agent_->nexthop_table()->set_discard_nh(nh);
    CreateNextHops();
}

void AgentInit::CreateInterfacesBase() {
    CreateInterfaces();
}

void AgentInit::InitDiscoveryBase() {
    if (cfg_.get()) {
        cfg_->InitDiscovery();
    }

    InitDiscovery();
}

void AgentInit::ConnectToControllerBase() {
    if (agent_->router_id_configured() == false) {
        LOG(DEBUG,
            "Router ID not configured. Connection to controller postponed");
    } else {
        LOG(DEBUG, "Router ID configured. Connection to controller initiated");
        // Connect to controller and DNS servers
        agent_->controller()->Connect();
    }

    ConnectToController();
}

void AgentInit::InitDoneBase() {
    if (cfg_.get()) {
        cfg_->InitDone();
    }
    InitDone();
}

/****************************************************************************
 * Shutdown routines
 ***************************************************************************/
typedef boost::function<bool(void)> TaskFnPtr;
static void RunInTaskContext(AgentInit *init, uint32_t task_id, TaskFnPtr fn) {
    TaskTrigger trigger(fn, task_id, 0);
    trigger.Set();
    init->WaitForIdle();
    return;
}

// Shutdown IO channel to controller+DNS
void AgentInit::IoShutdownBase() {
    agent_->controller()->Cleanup();
    agent_->controller()->DisConnect();
    IoShutdown();
}

static bool IoShutdownInternal(AgentInit *init) {
    init->IoShutdownBase();
    return true;
}

void AgentInit::FlushFlowsBase() {
    FlushFlows();
    return;
}

static bool FlushFlowsInternal(AgentInit *init) {
    init->FlushFlowsBase();
    return true;
}

void AgentInit::VgwShutdownBase() {
    VgwShutdown();
    return;
}

static bool VgwShutdownInternal(AgentInit *init) {
    init->VgwShutdownBase();
    return true;
}

void AgentInit::DeleteRoutesBase() {
    DeleteRoutes();
    if (agent_->oper_db())
        agent_->oper_db()->DeleteRoutes();
}

static bool DeleteRoutesInternal(AgentInit *init) {
    init->DeleteRoutesBase();
    return true;
}

static bool FlushTable(AgentDBTable *table, DBTableWalker *walker) {
    table->Flush(walker);
    return true;
}

void AgentInit::DeleteDBEntriesBase() {
    DBTableWalker walker;
    int task_id = agent_->task_scheduler()->GetTaskId("db::DBTable");

    RunInTaskContext(this, task_id, boost::bind(&DeleteRoutesInternal, this));

    RunInTaskContext(this, task_id,
                     boost::bind(&FlushTable, agent_->interface_table(),
                                 &walker));
    agent_->set_vhost_interface(NULL);

    RunInTaskContext(this, task_id,
                     boost::bind(&FlushTable, agent_->vm_table(), &walker));

    RunInTaskContext(this, task_id,
                     boost::bind(&FlushTable, agent_->vn_table(), &walker));


    agent_->vrf_table()->DeleteStaticVrf(agent_->fabric_vrf_name());
    RunInTaskContext(this, task_id,
                     boost::bind(&FlushTable, agent_->vrf_table(), &walker));

    RunInTaskContext(this, task_id,
                     boost::bind(&FlushTable, agent_->nexthop_table(),
                                 &walker));
    agent_->nexthop_table()->set_discard_nh(NULL);


    RunInTaskContext(this, task_id,
                     boost::bind(&FlushTable, agent_->sg_table(), &walker));

    RunInTaskContext(this, task_id,
                     boost::bind(&FlushTable, agent_->acl_table(), &walker));
}

static bool WaitForDbCount(DBTableBase *table, AgentInit *init,
                           uint32_t count, int msec) {
    while ((table->Size() > count)  && (msec > 0)) {
        init->WaitForIdle();
        usleep(1000);
        msec -= 1;
    }

    return (table->Size() == count);
}

void AgentInit::WaitForDBEmpty() {
    WaitForDbCount(agent_->interface_table(), this, 0, 10000);
    WaitForDbCount(agent_->vrf_table(), this, 0, 10000);
    WaitForDbCount(agent_->nexthop_table(), this, 0, 10000);
    WaitForDbCount(agent_->vm_table(), this, 0, 10000);
    WaitForDbCount(agent_->vn_table(), this, 0, 10000);
    WaitForDbCount(agent_->mpls_table(), this, 0, 10000);
    WaitForDbCount(agent_->interface_config_table(), this, 0, 10000);
    WaitForDbCount(agent_->acl_table(), this, 0, 10000);
}

void AgentInit::ServicesShutdownBase() {
    ServicesShutdown();
    return;
}

static bool ServicesShutdownInternal(AgentInit *init) {
    init->ServicesShutdownBase();
    return true;
}

void AgentInit::PktShutdownBase() {
    PktShutdown();
    return;
}

static bool PktShutdownInternal(AgentInit *init) {
    init->PktShutdownBase();
    return true;
}

void AgentInit::ModulesShutdownBase() {
    ModulesShutdown();
    if (agent_->oper_db()) {
        agent_->oper_db()->Shutdown();
    }

    if (agent_->cfg()) {
        agent_->cfg()->Shutdown();
    }
    return;
}

static bool ModulesShutdownInternal(AgentInit *init) {
    init->ModulesShutdownBase();
    return true;
}

void AgentInit::UveShutdownBase() {
    UveShutdown();
    return;
}

static bool UveShutdownInternal(AgentInit *init) {
    init->UveShutdownBase();
    return true;
}

void AgentInit::KSyncShutdownBase() {
    KSyncShutdown();
    return;
}

static bool KSyncShutdownInternal(AgentInit *init) {
    init->KSyncShutdownBase();
    return true;
}

void AgentInit::Shutdown() {
    int task_id = agent_->task_scheduler()->GetTaskId("db::DBTable");

    RunInTaskContext(this, task_id, boost::bind(&IoShutdownInternal, this));
    RunInTaskContext(this, task_id, boost::bind(&FlushFlowsInternal, this));
    RunInTaskContext(this, task_id, boost::bind(&VgwShutdownInternal, this));
    DeleteDBEntriesBase();
    WaitForDBEmpty();
    RunInTaskContext(this, task_id, boost::bind(&ServicesShutdownInternal,
                                                this));
    RunInTaskContext(this, task_id, boost::bind(&UveShutdownInternal, this));
    RunInTaskContext(this, task_id, boost::bind(&PktShutdownInternal, this));
    RunInTaskContext(this, task_id, boost::bind(&ModulesShutdownInternal,
                                                this));
    RunInTaskContext(this, task_id, boost::bind(&KSyncShutdownInternal, this));

    Sandesh::Uninit();
    WaitForIdle();

    agent_->event_manager()->Shutdown();
    WaitForIdle();
}
