/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_init_contrail_cmn_init_hpp
#define vnsw_agent_init_contrail_cmn_init_hpp

#include <boost/program_options.hpp>
#include <init/agent_init.h>

class Agent;
class AgentParam;

// The class to drive agent initialization. 
// Defines control parameters used to enable/disable agent features
class ContrailInitCommon : public AgentInit {
public:
    ContrailInitCommon();
    virtual ~ContrailInitCommon();

    void ProcessOptions(const std::string &config_file,
                        const std::string &program_name,
                        const boost::program_options::variables_map &var_map);
    int Start();

    // Initialization virtual methods
    void CreateModules();
    void RegisterDBClients();
    void InitModules();
    void CreateVrf();
    void CreateInterfaces();
    void InitDone();

    // Shutdown virtual methods
    void IoShutdown();
    void FlushFlows();
    void ServicesShutdown();
    void ModulesShutdown();
    void PktShutdown();

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
    bool create_vhost_;
    bool ksync_enable_;
    bool services_enable_;
    bool packet_enable_;
    bool uve_enable_;
    bool vgw_enable_;
    bool router_id_dep_enable_;

    std::auto_ptr<AgentStats> stats_;
    std::auto_ptr<KSync> ksync_;
    std::auto_ptr<AgentUve> uve_;

    std::auto_ptr<DiagTable> diag_table_;
    std::auto_ptr<ServicesModule> services_;
    std::auto_ptr<PktModule> pkt_;
    std::auto_ptr<VirtualGateway> vgw_;
    DISALLOW_COPY_AND_ASSIGN(ContrailInitCommon);
};

#endif // vnsw_agent_init_contrail_cmn_init_hpp
