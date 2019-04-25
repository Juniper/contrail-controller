/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_global_router_h_
#define vnsw_agent_global_router_h_

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>
#include <oper/ecmp_load_balance.h>
#include <oper/agent_route_walker.h>

class OperDB;
class VnEntry;
class VrfEntry;
class IFMapNode;
class AgentRouteResync;
class AgentRouteEncap;
class AgentUtXmlFlowThreshold;
class EcmpLoadBalance;
class CryptTunnelTable;

namespace autogen {
    struct LinklocalServiceEntryType;
    struct FlowAgingTimeout;
    class GlobalVrouterConfig;
}

struct LinkLocalDBState : DBState {
    const VrfEntry *vrf_;
    // set of linklocal addresses added to the VN
    std::set<Ip4Address> addresses_;

    LinkLocalDBState(const VrfEntry *vrf) : DBState(), vrf_(vrf) {}
    void Add(const Ip4Address &address) { addresses_.insert(address); }
    void Delete(const Ip4Address &address) { addresses_.erase(address); }
};

struct PortConfig {
    PortConfig() : port_count(0) {}

    struct PortRange {
        PortRange(uint16_t start, uint16_t end):
            port_start(start), port_end(end) {}

        uint16_t port_start;
        uint16_t port_end;
    };

    void Trim();
    uint16_t port_count;
    std::vector<PortRange> port_range;
};

// Handle Global Vrouter configuration
class GlobalVrouter : public OperIFMapTable {
public:
    enum CryptMode {
        CRYPT_ALL_TRAFFIC,
        CRYPT_FLOW,
        CRYPT_NONE
    };
    static const std::string kMetadataService;
    static const Ip4Address kLoopBackIp;
    static const int32_t kDefaultFlowExportRate = 0;
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
        DBTable::DBTableWalkRef vn_update_walk_ref_;
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

    typedef std::string CryptTunnelKey;
    struct CryptTunnel {
        CryptMode mode;
        CryptTunnel(CryptMode cmode): mode(cmode) {};
        bool operator==(const CryptTunnel &rhs) const {
            return (mode == rhs.mode);
        }
        bool operator<(const CryptTunnel &rhs) const {
            return (mode < rhs.mode);
        }
    };

    // map of linklocal service data, with (ip, port) as key
    typedef std::map<LinkLocalServiceKey, LinkLocalService> LinkLocalServicesMap;
    typedef std::pair<LinkLocalServiceKey, LinkLocalService> LinkLocalServicesPair;

    typedef std::map<CryptTunnelKey, CryptTunnel> CryptTunnelsMap;
    typedef std::pair<CryptTunnelKey, CryptTunnel> CryptTunnelsPair;

    typedef std::map<FlowAgingTimeoutKey, uint32_t> FlowAgingTimeoutMap;
    typedef std::pair<FlowAgingTimeoutKey, uint32_t> FlowAgingTimeoutPair;

    //Map used to audit for port pool configuration change
    typedef std::map<uint8_t, PortConfig> ProtocolPortSet;

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
    void UpdateSLOConfig(IFMapNode *node);
    bool FindLinkLocalService(const std::string &service_name,
                              Ip4Address *service_ip, uint16_t *service_port,
                              std::string *fabric_hostname,
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
    boost::uuids::uuid slo_uuid() const {return slo_uuid_;}

    uint64_t PendingFabricDnsRequests() const;
    void ResyncRoutes();
    const EcmpLoadBalance &ecmp_load_balance() const;
    bool configured() const { return configured_; }

    friend class AgentUtXmlFlowThreshold;
private:
    class FabricDnsResolver;
    class LinkLocalRouteManager;
    typedef std::vector<autogen::LinklocalServiceEntryType> LinkLocalServiceList;
    typedef std::vector<autogen::EncryptionTunnelEndpoint> EncryptionTunnelEndpointList;
    typedef std::vector<autogen::FlowAgingTimeout> FlowAgingTimeoutList;

    void UpdateLinkLocalServiceConfig(const LinkLocalServiceList &linklocal_list);
    void DeleteLinkLocalServiceConfig();
    bool ChangeNotify(LinkLocalServicesMap *old_value,
                      LinkLocalServicesMap *new_value);
    void AddLinkLocalService(const LinkLocalServicesMap::iterator &it);
    void DeleteLinkLocalService(const LinkLocalServicesMap::iterator &it);
    void ChangeLinkLocalService(const LinkLocalServicesMap::iterator &old_it,
                                const LinkLocalServicesMap::iterator &new_it);

    bool IsVrouterPresentCryptTunnelConfig(const EncryptionTunnelEndpointList &endpoint_list);
    void UpdateCryptTunnelEndpointConfig(const EncryptionTunnelEndpointList &endpoint_list,
                                         const std::string encrypt_mode_str);
    void DeleteCryptTunnelEndpointConfig();
    bool ChangeNotifyCryptTunnels(CryptTunnelsMap *old_value,
                                  CryptTunnelsMap *new_value);
    void AddCryptTunnelEndpoint(const CryptTunnelsMap::iterator &it);
    void DeleteCryptTunnelEndpoint(const CryptTunnelsMap::iterator &it);
    void ChangeCryptTunnelEndpoint(const CryptTunnelsMap::iterator &old_it,
                                   const CryptTunnelsMap::iterator &new_it);

    void UpdateFlowAging(autogen::GlobalVrouterConfig *cfg);
    void DeleteFlowAging();

    void UpdatePortConfig(autogen::GlobalVrouterConfig *cfg);
    void DeletePortConfig();

    LinkLocalServicesMap linklocal_services_map_;
    CryptTunnelsMap crypt_tunnels_map_;
    CryptMode crypt_mode_;
    boost::scoped_ptr<LinkLocalRouteManager> linklocal_route_mgr_;
    boost::scoped_ptr<FabricDnsResolver> fabric_dns_resolver_;
    AgentRouteWalkerPtr agent_route_resync_walker_;
    Agent::ForwardingMode forwarding_mode_;
    int32_t flow_export_rate_;
    FlowAgingTimeoutMap flow_aging_timeout_map_;
    ProtocolPortSet protocol_port_set_;
    EcmpLoadBalance ecmp_load_balance_;
    bool configured_; //true when global-vrouter-config stanza is present
    boost::uuids::uuid slo_uuid_; //SLO associated with global-vrouter
};

#endif // vnsw_agent_global_router_h_
