/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_BGP_ROUTER_H
#define __AGENT_OPER_BGP_ROUTER_H

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

#define BGP_ROUTER_TYPE "control-node"
#define CONTROL_NODE_ZONE_CONFIG_NAME "control-node-zone"

class IFMapNode;
class BgpRouter;
class ControlNodeZone;
class BgpRouterConfig;

typedef boost::shared_ptr<BgpRouter> BgpRouterPtr;
typedef boost::shared_ptr<ControlNodeZone> ControlNodeZonePtr;
typedef std::map<std::string, BgpRouterPtr> BgpRouterTree;
typedef std::map<std::string, ControlNodeZonePtr> ControlNodeZoneTree;

class BgpRouter {
public:
    BgpRouter(const std::string &name,
              const std::string &ip4_address,
              const uint32_t &port);
    ~BgpRouter();

    void set_ip4_address(const std::string &ip_address, const uint32_t &port);
    void set_control_node_zone_name(const std::string &contol_node_zone_name);

    const std::string &name() const { return name_; }
    const Ip4Address &ip4_address() const { return ip4_address_; }
    const uint32_t &port() const { return port_; }
    const std::string &control_node_zone_name() const {
        return control_node_zone_name_;
    }
private:
    std::string name_;
    Ip4Address ip4_address_;
    uint32_t port_;
    std::string control_node_zone_name_;
    DISALLOW_COPY_AND_ASSIGN(BgpRouter);
};

class ControlNodeZone {
public:
    ControlNodeZone(const std::string &name, const std::string &display_name,
                    const boost::uuids::uuid &uuid);
    ~ControlNodeZone();
private:
    friend class BgpRouterConfig;
    std::string name_;
    std::string display_name_;
    boost::uuids::uuid uuid_;
    BgpRouterTree bgp_router_tree_;
    DISALLOW_COPY_AND_ASSIGN(ControlNodeZone);
};

class BgpRouterConfig : public OperIFMapTable {
public:
    BgpRouterConfig(Agent *agent);
    virtual ~BgpRouterConfig();

    uint32_t GetBgpRouterCount() { return bgp_router_tree_.size(); }
    uint32_t GetBgpRouterCount(const std::string &cnz_name);
    uint32_t GetControlNodeZoneCount() {
        return control_node_zone_tree_.size();
    }

    BgpRouterPtr GetBgpRouterFromIpAddress(const std::string &ip4_address);
    BgpRouterPtr GetBgpRouterFromControlNodeZone(const std::string &cnz_name);

    void UpdateControlNodeZoneConfig(IFMapNode *bgp_router_node,
                                     BgpRouterPtr bgp_router);
    void DeleteControlNodeZoneConfig(IFMapNode *bgp_router_node,
                                     BgpRouterPtr bgp_router);

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
private:
    tbb::mutex mutex_;
    BgpRouterTree bgp_router_tree_;
    ControlNodeZoneTree control_node_zone_tree_;
    DISALLOW_COPY_AND_ASSIGN(BgpRouterConfig);
};

#endif
