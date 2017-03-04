/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_global_router_h_
#define vnsw_agent_global_router_h_

#include <cmn/agent_cmn.h>
#include <oper/ecmp_load_balance.h>

class OperDB;
class VnEntry;
class VrfEntry;
class IFMapNode;
class AgentRouteResync;
class AgentRouteEncap;
class AgentUtXmlFlowThreshold;
class EcmpLoadBalance;

namespace autogen {
    struct LinklocalServiceEntryType;
    struct FlowAgingTimeout;
    struct GlobalVrouterConfig;
}

struct LinkLocalDBState : DBState {
    const VrfEntry *vrf_;
    // set of linklocal addresses added to the VN
    std::set<Ip4Address> addresses_;

    LinkLocalDBState(const VrfEntry *vrf) : DBState(), vrf_(vrf) {}
    void Add(const Ip4Address &address) { addresses_.insert(address); }
    void Delete(const Ip4Address &address) { addresses_.erase(address); }
};

// Handle Global Vrouter configuration
class GlobalVrouter : public OperIFMapTable {
public:
    static const std::string kMetadataService;
    static const Ip4Address kLoopBackIp;
    static const int32_t kDefaultFlowExportRate = 100;
    static const int32_t kDisableSampling = -1;

    struct LinkLocalServiceKey {
        Ip4Address linklocal_service_ip;
        uint16_t   linklocal_service_port;

        LinkLocalServiceKey(const Ip4Address &addr, uint16_t port)
            : linklocal_service_ip(addr), linklocal_service_port(port) {}
        bool operator<(const LinkLocalServiceKey &rhs) const;
    };

    // Config data for each linklocal service
    struct LinkLocalService {
        std::string linklocal_service_name;
        std::string ipfabric_dns_service_name;
        std::vector<Ip4Address>  ipfabric_service_ip;
        uint16_t                 ipfabric_service_port;

        LinkLocalService(const std::string &service_name,
                         const std::string &fabric_dns_name,
                         const std::vector<Ip4Address> &fabric_ip,
                         uint16_t fabric_port);
        bool operator==(const LinkLocalService &rhs) const;
        bool IsAddressInUse(const Ip4Address &ip) const;
    };

    struct FlowAgingTimeoutKey {
        FlowAgingTimeoutKey(uint8_t proto, uint16_t port_arg):
            protocol(proto), port(port_arg) { }
        uint8_t protocol;
        uint16_t port;
        bool operator==(const FlowAgingTimeoutKey &rhs) const {
            return (protocol == rhs.protocol && port == rhs.port);
        }

        bool operator<(const FlowAgingTimeoutKey &rhs) const {
            if (protocol != rhs.protocol) {
                return protocol < rhs.protocol;
            }
            return port < rhs.port;
        }
    };

    // map of linklocal service data, with (ip, port) as key
    typedef std::map<LinkLocalServiceKey, LinkLocalService> LinkLocalServicesMap;
    typedef std::pair<LinkLocalServiceKey, LinkLocalService> LinkLocalServicesPair;

    typedef std::map<FlowAgingTimeoutKey, uint32_t> FlowAgingTimeoutMap;
    typedef std::pair<FlowAgingTimeoutKey, uint32_t> FlowAgingTimeoutPair;

    GlobalVrouter(Agent *agent);
    virtual ~GlobalVrouter();
    void CreateDBClients();

    const LinkLocalServicesMap &linklocal_services_map() const {
        return linklocal_services_map_;
    }
    int32_t flow_export_rate() const { return flow_export_rate_; }

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);

    void GlobalVrouterConfig(IFMapNode *node);
    bool FindLinkLocalService(const std::string &service_name,
                              Ip4Address *service_ip, uint16_t *service_port,
                              Ip4Address *fabric_ip, uint16_t *fabric_port) const;
    bool FindLinkLocalService(const Ip4Address &service_ip,
                              uint16_t service_port, std::string *service_name,
                              Ip4Address *fabric_ip, uint16_t *fabric_port) const;
    bool FindLinkLocalService(const std::string &service_name,
                              std::set<Ip4Address> *service_ip) const;
    bool FindLinkLocalService(const Ip4Address &service_ip,
                              std::set<std::string> *service_names) const;
    void LinkLocalRouteUpdate(const std::vector<Ip4Address> &addr_list);
    bool IsAddressInUse(const Ip4Address &ip) const;
    bool IsLinkLocalAddressInUse(const Ip4Address &ip) const;
    Agent::ForwardingMode forwarding_mode() const {return forwarding_mode_;}

    uint64_t PendingFabricDnsRequests() const;
    void ResyncRoutes();
    const EcmpLoadBalance &ecmp_load_balance() const;
    bool configured() const { return configured_; }

    friend class AgentUtXmlFlowThreshold;
private:
    class FabricDnsResolver;
    class LinkLocalRouteManager;
    typedef std::vector<autogen::LinklocalServiceEntryType> LinkLocalServiceList;
    typedef std::vector<autogen::FlowAgingTimeout> FlowAgingTimeoutList;

    void UpdateLinkLocalServiceConfig(const LinkLocalServiceList &linklocal_list);
    void DeleteLinkLocalServiceConfig();
    bool ChangeNotify(LinkLocalServicesMap *old_value,
                      LinkLocalServicesMap *new_value);
    void AddLinkLocalService(const LinkLocalServicesMap::iterator &it);
    void DeleteLinkLocalService(const LinkLocalServicesMap::iterator &it);
    void ChangeLinkLocalService(const LinkLocalServicesMap::iterator &old_it,
                                const LinkLocalServicesMap::iterator &new_it);
    void UpdateFlowAging(autogen::GlobalVrouterConfig *cfg);
    void DeleteFlowAging();

    LinkLocalServicesMap linklocal_services_map_;
    boost::scoped_ptr<LinkLocalRouteManager> linklocal_route_mgr_;
    boost::scoped_ptr<FabricDnsResolver> fabric_dns_resolver_;
    boost::scoped_ptr<AgentRouteResync> agent_route_resync_walker_;
    Agent::ForwardingMode forwarding_mode_;
    int32_t flow_export_rate_;
    FlowAgingTimeoutMap flow_aging_timeout_map_;
    EcmpLoadBalance ecmp_load_balance_;
    bool configured_; //true when global-vrouter-config stanza is present
};

#endif // vnsw_agent_global_router_h_
