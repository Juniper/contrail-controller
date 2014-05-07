/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_init_hpp
#define vnsw_agent_init_hpp

#include <boost/program_options.hpp>

class Agent;
class AgentParam;

// The class to drive agent initialization. 
// Defines control parameters used to enable/disable agent features
class AgentInit {
public:
    AgentInit() :
        agent_(NULL), params_(NULL), create_vhost_(true), ksync_enable_(true),
        services_enable_(true), packet_enable_(true), uve_enable_(true),
        vgw_enable_(true), router_id_dep_enable_(true), init_done_(false),
        trigger_() { }

    ~AgentInit() {
        trigger_->Reset();
    }

    bool Run();
    void Start();
    void Shutdown();

    void InitLogging();
    void InitCollector();
    void CreateModules();
    void CreateDBTables();
    void CreateDBClients();
    void InitModules();
    void InitPeers();
    void CreateVrf();
    void CreateNextHops();
    void CreateInterfaces();
    void InitDiscovery();
    void InitDone();

    void Init(AgentParam *param, Agent *agent,
              const boost::program_options::variables_map &var_map);
    void InitXenLinkLocalIntf();
    void InitVmwareInterface();
    void DeleteRoutes();
    void DeleteNextHops();
    void DeleteVrfs();
    void DeleteInterfaces();

    bool ksync_enable() const { return ksync_enable_; }
    bool services_enable() const { return services_enable_; }
    bool packet_enable() const { return packet_enable_; }
    bool create_vhost() const { return create_vhost_; }
    bool uve_enable() const { return uve_enable_; }
    bool vgw_enable() const { return vgw_enable_; }
    bool router_id_dep_enable() const { return router_id_dep_enable_; }
    bool init_done() const { return init_done_; }

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
    bool init_done_;

    std::auto_ptr<TaskTrigger> trigger_;
    DISALLOW_COPY_AND_ASSIGN(AgentInit);
};

#endif // vnsw_agent_init_hpp
