/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_test_agent_init_hpp
#define vnsw_test_agent_init_hpp

#include <boost/program_options.hpp>

class Agent;
class AgentParam;

// The class to drive agent initialization. 
// Defines control parameters used to enable/disable agent features
class TestAgentInit {
public:
    TestAgentInit();
    ~TestAgentInit();

    bool Run();
    void Start();
    void Shutdown();
    void IoShutdown();
    void FlushFlows();
    void ServicesShutdown();

    void InitLogging();
    void InitCollector();
    void CreateModules();
    void CreateDBTables();
    void RegisterDBClients();
    void InitModules();
    void InitPeers();
    void CreateVrf();
    void CreateNextHops();
    void CreateInterfaces();
    void InitDiscovery();
    void InitDone();

    void Init(AgentParam *param, Agent *agent,
              const boost::program_options::variables_map &var_map);
    void InitVmwareInterface();
    void DeleteRoutes();
    DBTableWalker *DeleteInterfaces();
    DBTableWalker *DeleteVms();
    DBTableWalker *DeleteVns();
    DBTableWalker *DeleteVrfs();
    DBTableWalker *DeleteNextHops();
    DBTableWalker *DeleteSecurityGroups();
    DBTableWalker *DeleteAcls();

    bool ksync_enable() const { return ksync_enable_; }
    bool services_enable() const { return services_enable_; }
    bool packet_enable() const { return packet_enable_; }
    bool create_vhost() const { return create_vhost_; }
    bool uve_enable() const { return uve_enable_; }
    bool vgw_enable() const { return vgw_enable_; }
    bool router_id_dep_enable() const { return router_id_dep_enable_; }

    void set_ksync_enable(bool flag) { ksync_enable_ = flag; }
    void set_services_enable(bool flag) { services_enable_ = flag; }
    void set_packet_enable(bool flag) { packet_enable_ = flag; }
    void set_create_vhost(bool flag) { create_vhost_ = flag; }
    void set_uve_enable(bool flag) { uve_enable_ = flag; }
    void set_vgw_enable(bool flag) { vgw_enable_ = flag; }
    void set_router_id_dep_enable(bool flag) { router_id_dep_enable_ = flag; }
private:
    Agent *agent_;
    AgentParam *params_;

    bool create_vhost_;
    bool ksync_enable_;
    bool services_enable_;
    bool packet_enable_;
    bool uve_enable_;
    bool vgw_enable_;
    bool router_id_dep_enable_;

    std::auto_ptr<TaskTrigger> trigger_;
    std::auto_ptr<DiagTable> diag_table_;
    std::auto_ptr<ServicesModule> services_;
    std::auto_ptr<PktModule> pkt_;
    DISALLOW_COPY_AND_ASSIGN(TestAgentInit);
};

#endif // vnsw_test_agent_init_hpp
