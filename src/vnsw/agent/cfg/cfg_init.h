/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_init_cfg_hpp
#define vnsw_agent_init_cfg_hpp

#include <cmn/agent_cmn.h>
#include <ifmap/ifmap_agent_table.h>
#include <sandesh/sandesh_trace.h>

using namespace boost::uuids;

class CfgFilter;
class CfgListener;
class InterfaceCfgClient;
class MirrorCfgTable;
class IntfMirrorCfgTable;

class AgentConfig  {
public:
    AgentConfig(Agent *agent);
    virtual ~AgentConfig();

    IFMapAgentTable *cfg_vm_interface_table() const {
        return cfg_vm_interface_table_;
    }
    IFMapAgentTable *cfg_vm_table() const {return cfg_vm_table_;}
    IFMapAgentTable *cfg_vn_table() const {return cfg_vn_table_;}
    IFMapAgentTable *cfg_sg_table() const {return cfg_sg_table_;}
    IFMapAgentTable *cfg_acl_table() const {return cfg_acl_table_;}
    IFMapAgentTable *cfg_vrf_table() const {return cfg_vrf_table_;}
    IFMapAgentTable *cfg_instanceip_table() const {
        return cfg_instanceip_table_;
    }
    IFMapAgentTable *cfg_floatingip_table() const {
        return cfg_floatingip_table_;
    }
    IFMapAgentTable *cfg_aliasip_table() const {
        return cfg_aliasip_table_;
    }
    IFMapAgentTable *cfg_floatingip_pool_table() const {
        return cfg_floatingip_pool_table_;
    }
    IFMapAgentTable *cfg_aliasip_pool_table() const {
        return cfg_aliasip_pool_table_;
    }
    IFMapAgentTable *cfg_network_ipam_table() const {
        return cfg_network_ipam_table_;
    }
    IFMapAgentTable *cfg_vn_network_ipam_table() const {
        return cfg_vn_network_ipam_table_;
    }
    IFMapAgentTable *cfg_vm_port_vrf_table() const {
        return cfg_vm_port_vrf_table_;
    }
    IFMapAgentTable *cfg_route_table() const {
        return cfg_route_table_;
    }

    IFMapAgentTable *cfg_service_template_table() const {
        return cfg_service_template_table_;
    }

    IFMapAgentTable *cfg_subnet_table() const {
        return cfg_subnet_table_;
    }

    IFMapAgentTable *cfg_logical_port_table() const {
        return cfg_logical_port_table_;
    }

    IFMapAgentTable *cfg_service_instance_table() const {
        return cfg_service_instance_table_;
    }

    IFMapAgentTable *cfg_security_group_table() const {
        return cfg_security_group_table_;
    }

    IFMapAgentTable *cfg_physical_device_table() const {
        return cfg_physical_device_table_;
    }

    IFMapAgentTable *cfg_qos_table() const {
        return cfg_qos_table_;
    }

    IFMapAgentTable *cfg_global_qos_table() const {
        return cfg_global_qos_table_;
    }

    IFMapAgentTable *cfg_qos_queue_table() const {
        return cfg_qos_queue_table_;
    }

    IFMapAgentTable *cfg_forwarding_class_table() const {
        return cfg_forwarding_class_table_;
    }

    IFMapAgentTable *cfg_bridge_domain_table() const {
        return cfg_bridge_domain_table_;
    }

    IFMapAgentTable *cfg_vm_port_bridge_domain_table() const {
        return cfg_vm_port_bridge_domain_table_;
    }

    IFMapAgentTable *cfg_health_check_table() const {
        return cfg_health_check_table_;
    }

    Agent *agent() const { return agent_; }
    CfgFilter *cfg_filter() const { return cfg_filter_.get(); }
    IFMapAgentParser *cfg_parser() const { return cfg_parser_.get(); }
    DBGraph *cfg_graph() const { return cfg_graph_.get(); }
    MirrorCfgTable *cfg_mirror_table() const { return cfg_mirror_table_.get(); }
    InterfaceCfgClient *cfg_interface_client() const {
        return cfg_interface_client_.get();
    }
    IntfMirrorCfgTable *cfg_intf_mirror_table() const {
        return cfg_intf_mirror_table_.get();
    }

    void CreateDBTables(DB *db);
    void RegisterDBClients(DB *db);
    void Register(const char *node_name, AgentDBTable *table,
                  int need_property_id);
    void Init();
    void InitDone();
    void Shutdown();
private:
    Agent *agent_;
    std::auto_ptr<CfgFilter> cfg_filter_;
    std::auto_ptr<IFMapAgentParser> cfg_parser_; 
    std::auto_ptr<DBGraph> cfg_graph_;
    std::auto_ptr<InterfaceCfgClient> cfg_interface_client_;
    std::auto_ptr<MirrorCfgTable> cfg_mirror_table_;
    std::auto_ptr<IntfMirrorCfgTable> cfg_intf_mirror_table_;

    DBTableBase::ListenerId lid_;

    IFMapAgentTable *cfg_vm_interface_table_;
    IFMapAgentTable *cfg_vm_table_;
    IFMapAgentTable *cfg_vn_table_;
    IFMapAgentTable *cfg_sg_table_;
    IFMapAgentTable *cfg_acl_table_;
    IFMapAgentTable *cfg_vrf_table_;
    IFMapAgentTable *cfg_instanceip_table_;
    IFMapAgentTable *cfg_floatingip_table_;
    IFMapAgentTable *cfg_aliasip_table_;
    IFMapAgentTable *cfg_floatingip_pool_table_;
    IFMapAgentTable *cfg_aliasip_pool_table_;
    IFMapAgentTable *cfg_network_ipam_table_;
    IFMapAgentTable *cfg_vn_network_ipam_table_;
    IFMapAgentTable *cfg_vm_port_vrf_table_;
    IFMapAgentTable *cfg_route_table_;
    IFMapAgentTable *cfg_service_template_table_;
    IFMapAgentTable *cfg_subnet_table_;
    IFMapAgentTable *cfg_logical_port_table_;
    IFMapAgentTable *cfg_loadbalancer_table_;
    IFMapAgentTable *cfg_loadbalancer_pool_table_;
    IFMapAgentTable *cfg_service_instance_table_;
    IFMapAgentTable *cfg_security_group_table_;
    IFMapAgentTable *cfg_physical_device_table_;
    IFMapAgentTable *cfg_health_check_table_;
    IFMapAgentTable *cfg_qos_table_;
    IFMapAgentTable *cfg_global_qos_table_;
    IFMapAgentTable *cfg_qos_queue_table_;
    IFMapAgentTable *cfg_forwarding_class_table_;
    IFMapAgentTable *cfg_bridge_domain_table_;
    IFMapAgentTable *cfg_vm_port_bridge_domain_table_;

    DISALLOW_COPY_AND_ASSIGN(AgentConfig);
};

extern SandeshTraceBufferPtr CfgTraceBuf;
#define CONFIG_TRACE(obj, ...) \
do {\
    Config##obj::TraceMsg(CfgTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0);\

#endif // vnsw_agent_init_cfg_hpp
