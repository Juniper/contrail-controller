/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_init_hpp
#define vnsw_agent_init_hpp

class Agent;
class AgentParam;

// The class to drive agent initialization. 
// Defines different agent initialization states
// Defines control parameters used to enable/disable agent features
class AgentInit {
public:
    enum State {
        CREATE_MODULES,
        CREATE_DB_TABLES,
        CREATE_DB_CLIENTS,
        INIT_MODULES,
        CREATE_VRF,
        CREATE_INTERFACE,
        INIT_DONE
    };

    AgentInit() :
        agent_(NULL), params_(NULL), create_vhost_(true), ksync_enable_(true),
        services_enable_(true), packet_enable_(true), uve_enable_(true),
        vgw_enable_(true), router_id_dep_enable_(true), state_(CREATE_MODULES),
        trigger_list_(), vrf_trigger_(NULL), intf_trigger_(NULL),
        vrf_client_id_(-1), intf_client_id_(-1) { }

    ~AgentInit() {
        for (std::vector<TaskTrigger *>::iterator it = trigger_list_.begin();
             it != trigger_list_.end(); ++it) {
            (*it)->Reset();
            delete *it;
        }

        if (vrf_trigger_) {
            vrf_trigger_->Reset();
            delete vrf_trigger_;
        }

        if (intf_trigger_) {
            intf_trigger_->Reset();
            delete intf_trigger_;
        }
    }

    State state() { return state_; }
    const std::string &StateToString() { 
        static const std::string StateString[] = {
            "create_modules",
            "create_db_table",
            "create_db_clients",
            "init_modules",
            "create_vrf",
            "create_interface",
            "init_done"
        };
        return StateString[state_];
    }

    bool Run();
    void TriggerInit();
    void CreateDefaultVrf();
    void CreateInterfaces(DB *db);
    void Init(AgentParam *param, Agent *agent,
              const boost::program_options::variables_map &var_map);
    void Start();
    void InitXenLinkLocalIntf();
    void InitVmwareInterface();
    void DeleteStaticEntries();
    void Shutdown();

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
    void InitModules();
    void OnInterfaceCreate(DBEntryBase *entry);
    void OnVrfCreate(DBEntryBase *entry);

    void DeleteRoutes();
    void DeleteNextHop();
    void DeleteVrf();
    void DeleteInterface();

    Agent *agent_;
    AgentParam *params_;

    bool create_vhost_;
    bool ksync_enable_;
    bool services_enable_;
    bool packet_enable_;
    bool uve_enable_;
    bool vgw_enable_;
    bool router_id_dep_enable_;
    State state_;

    std::vector<TaskTrigger *> trigger_list_;
    TaskTrigger *vrf_trigger_;
    TaskTrigger *intf_trigger_;

    DBTableBase::ListenerId vrf_client_id_;
    DBTableBase::ListenerId intf_client_id_;
    DISALLOW_COPY_AND_ASSIGN(AgentInit);
};

#endif // vnsw_agent_init_hpp
