/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <cmn/agent_factory.h>
#include <cmn/agent_stats.h>
#include <cmn/event_notifier.h>
#include <init/agent_param.h>

#include <cfg/cfg_init.h>
#include <vgw/cfg_vgw.h>

#include <oper/operdb_init.h>
#include <oper/sg.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface.h>
#include <oper/route_common.h>
#include <oper/agent_profile.h>
#include <filter/acl.h>
#include <controller/controller_init.h>
#include <resource_manager/resource_manager.h>

#include "agent_init.h"

AgentInit::AgentInit() :
    agent_(new Agent()), agent_param_(NULL), trigger_(),
    enable_controller_(true) {
}

AgentInit::~AgentInit() {
    TaskScheduler *scheduler = agent_->task_scheduler();
    stats_.reset();
    trigger_.reset();
    controller_.reset();
    cfg_.reset();
    oper_.reset();
    resource_manager_.reset();
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

int AgentInit::ModuleType() {
    return Module::VROUTER_AGENT;
}

string AgentInit::ModuleName() {
    Module::type module = static_cast<Module::type>(ModuleType());
    return g_vns_constants.ModuleNames.find(module)->second;
}

string AgentInit::AgentName() {
    return agent_param_->agent_name();
}

string AgentInit::InstanceId() {
    return g_vns_constants.INSTANCE_ID_DEFAULT;
}

void AgentInit::InitPlatform() {
    boost::system::error_code ec;
    Ip4Address ip = Ip4Address::from_string("127.0.0.1", ec);
    if (ec.value() != 0) {
        assert(0);
    }

    if (agent_param_->platform() == AgentParam::VROUTER_ON_NIC) {
        agent_->set_vrouter_server_ip(ip);
        agent_->set_vrouter_server_port(VROUTER_SERVER_PORT);
        agent_->set_pkt_interface_name("pkt0");
    } else if (agent_param_->platform() == AgentParam::VROUTER_ON_HOST_DPDK) {
        agent_->set_vrouter_server_ip(ip);
        agent_->set_vrouter_server_port(VROUTER_SERVER_PORT);
        agent_->set_pkt_interface_name("unix");
    }
}

// Start of Agent init.
// Trigger init in DBTable task context
int AgentInit::Start() {
    agent_->set_task_scheduler(TaskScheduler::GetInstance());

    // Init platform specific information
    InitPlatform();

    // Copy tunable parameters into agent_
    agent_->CopyConfig(agent_param_);

    string module_name = ModuleName();
    agent_->set_agent_name(AgentName());
    agent_->set_instance_id(InstanceId());
    agent_->set_module_type(ModuleType());
    agent_->set_module_name(module_name);


    std::string log_property_file = agent_param_->log_property_file();
    if (log_property_file.size()) {
        LoggingInit(log_property_file);
    }
    else {
        LoggingInit(agent_param_->log_file(), agent_param_->log_file_size(),
                    agent_param_->log_files_count(), agent_param_->use_syslog(),
                    agent_param_->syslog_facility(), module_name,
                    SandeshLevelTolog4Level(
                        Sandesh::StringToLevel(agent_param_->log_level())));
    }
    agent_param_->LogConfig();

    int ret = agent_param_->Validate();
    if (ret != 0) {
        return ret;
    }

    agent_param_->PostValidateLogConfig();

    int task_id = agent_->task_scheduler()->GetTaskId(AGENT_INIT_TASKNAME);
    trigger_.reset(new TaskTrigger(boost::bind(&AgentInit::InitBase, this),
                                   task_id, 0));
    trigger_->Set();
    return 0;
}

// Start init sequence
bool AgentInit::InitBase() {
    FactoryInit();
    InitLoggingBase();
    CreatePeersBase();
    CreateModulesBase();
    CreateDBTablesBase();
    RegisterDBClientsBase();
    InitModulesBase();
    InitCollectorBase();
    CreateVrfBase();
    CreateNextHopsBase();
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

// Connect to collector specified in config
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
    //Event notify manager
    event_notifier_.reset(new EventNotifier(agent()));
    agent()->set_event_notifier(event_notifier_.get());

    cfg_.reset(new AgentConfig(agent()));
    agent_->set_cfg(cfg_.get());

    oper_.reset(new OperDB(agent()));
    agent_->set_oper_db(oper_.get());

    if (enable_controller_) {
        controller_.reset(new VNController(agent()));
        agent_->set_controller(controller_.get());
    }

    stats_.reset(new AgentStats(agent()));
    agent()->set_stats(stats_.get());

    resource_manager_.reset(new ResourceManager(agent()));
    agent()->set_resource_manager(resource_manager_.get());

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

    if (resource_manager_.get()) {
        resource_manager_->Init();
    }

    InitModules();
}

static void CreateVrfIndependentNextHop(Agent *agent) {

    DiscardNH::Create();
    DiscardNHKey key1;
    NextHop *nh = static_cast<NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key1));
    agent->nexthop_table()->set_discard_nh(nh);

    //Reserve index 2, this would be used to
    //set as RPF NH when packet has to be discarded
    //due to source route mismatch
    assert(agent->nexthop_table()->ReserveIndex() ==
               NextHopTable::kRpfDiscardIndex);
    L2ReceiveNH::Create();
    L2ReceiveNHKey key2;
    nh = static_cast<NextHop *>
                (agent->nexthop_table()->FindActiveEntry(&key2));
    agent->nexthop_table()->set_l2_receive_nh(nh);
}

void AgentInit::CreateVrfBase() {
    // Bridge Receive routes are added on VRF creation. Ensure that Bridge
    // Receive-NH which is independent of VRF is created first
    CreateVrfIndependentNextHop(agent_.get());

    // Create the default VRF
    VrfTable *vrf_table = agent_->vrf_table();

    vrf_table->CreateStaticVrf(agent_->fabric_vrf_name());
    VrfEntry *vrf = vrf_table->FindVrfFromName(agent_->fabric_vrf_name());
    assert(vrf);

    agent_->set_fabric_vrf(vrf);
    agent_->set_fabric_inet4_unicast_table(vrf->GetInet4UnicastRouteTable());
    agent_->set_fabric_inet4_multicast_table
        (vrf->GetInet4MulticastRouteTable());
    agent_->set_fabric_l2_unicast_table(vrf->GetBridgeRouteTable());
    agent_->set_fabric_evpn_table(vrf->GetEvpnRouteTable());

    CreateVrf();
}

void AgentInit::CreateNextHopsBase() {
    CreateNextHops();
}

void AgentInit::CreateInterfacesBase() {
    CreateInterfaces();
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
    TaskScheduler *scheduler = agent_->task_scheduler();
    // Enable task latency measurements once init is done
    scheduler->EnableLatencyThresholds(agent_param_->tbb_exec_delay(),
                                       agent_param_->tbb_schedule_delay());
    agent_param_->vgw_config_table()->InitDone(agent_.get());
    if (cfg_.get()) {
        cfg_->InitDone();
    }
    // Enable task latency measurements once init is done
    scheduler->EnableLatencyThresholds
        (agent_param_->tbb_exec_delay() * 1000,
         agent_param_->tbb_schedule_delay() * 1000);

    // Flow related tasks are known have greater latency. Add exception
    // for them
    uint32_t execute_delay = (20 * 1000);
    uint32_t schedule_delay = (20 * 1000);
    scheduler->SetLatencyThreshold(kTaskFlowEvent, execute_delay,
                                   schedule_delay);
    scheduler->SetLatencyThreshold(kTaskFlowKSync, execute_delay,
                                   schedule_delay);
    scheduler->SetLatencyThreshold(kTaskFlowUpdate, execute_delay,
                                   schedule_delay);
    scheduler->SetLatencyThreshold(kTaskFlowStatsCollector, (execute_delay * 2),
                                   (schedule_delay * 2));
    agent_->InitDone();
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
    int task_id = agent_->task_scheduler()->GetTaskId(AGENT_SHUTDOWN_TASKNAME);

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

void AgentInit::ProfileShutdownBase() {
    if (agent_->oper_db() && agent_->oper_db()->agent_profile()) {
        agent_->oper_db()->agent_profile()->Shutdown();
    }
}

static bool ProfileShutdownInternal(AgentInit *init) {
    init->ProfileShutdownBase();
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

void AgentInit::StatsCollectorShutdownBase() {
    StatsCollectorShutdown();
    return;
}

void AgentInit::FlowStatsCollectorShutdownBase() {
    FlowStatsCollectorShutdown();
    return;
}

static bool UveShutdownInternal(AgentInit *init) {
    init->UveShutdownBase();
    return true;
}

static bool StatsCollectorShutdownInternal(AgentInit *init) {
    init->StatsCollectorShutdownBase();
    return true;
}

static bool FlowStatsCollectorShutdownInternal(AgentInit *init) {
    init->FlowStatsCollectorShutdownBase();
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
    int task_id = agent_->task_scheduler()->GetTaskId(AGENT_SHUTDOWN_TASKNAME);

    RunInTaskContext(this, task_id, boost::bind(&IoShutdownInternal, this));
    RunInTaskContext(this, task_id, boost::bind(&ProfileShutdownInternal, this));
    RunInTaskContext(this, task_id, boost::bind(&FlushFlowsInternal, this));
    RunInTaskContext(this, task_id, boost::bind(&VgwShutdownInternal, this));
    DeleteDBEntriesBase();
    WaitForDBEmpty();
    RunInTaskContext(this, task_id, boost::bind(&ServicesShutdownInternal,
                                                this));
    RunInTaskContext(this, task_id, boost::bind
                     (&FlowStatsCollectorShutdownInternal, this));
    RunInTaskContext(this, task_id, boost::bind(&StatsCollectorShutdownInternal,
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
