/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_global_router_h_
#define vnsw_agent_global_router_h_

#include <cmn/agent_cmn.h>

class OperDB;
class VnEntry;
class VrfEntry;
class IFMapNode;
class AgentRouteEncap;

namespace autogen {
    struct LinklocalServiceEntryType;
}

struct LinkLocalDBState : DBState {
    const VrfEntry *vrf_;

    LinkLocalDBState(const VrfEntry *vrf) : DBState(), vrf_(vrf) {}
};

// Handle Global Vrouter configuration
class GlobalVrouter {
public:
    static const std::string kMetadataService;

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

    // map of linklocal service data, with (ip, port) as key
    typedef std::map<LinkLocalServiceKey, LinkLocalService> LinkLocalServicesMap;
    typedef std::pair<LinkLocalServiceKey, LinkLocalService> LinkLocalServicesPair;

    GlobalVrouter(OperDB *oper);
    virtual ~GlobalVrouter();
    void CreateDBClients();

    const OperDB *oper_db() const { return oper_; }
    const LinkLocalServicesMap &linklocal_services_map() const {
        return linklocal_services_map_;
    }

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

private:
    class FabricDnsResolver;
    class LinkLocalRouteManager;
    typedef std::vector<autogen::LinklocalServiceEntryType> LinkLocalServiceList;

    void UpdateLinkLocalServiceConfig(const LinkLocalServiceList &linklocal_list);
    void DeleteLinkLocalServiceConfig();
    bool ChangeNotify(LinkLocalServicesMap *old_value,
                      LinkLocalServicesMap *new_value);
    void AddLinkLocalService(const LinkLocalServicesMap::iterator &it);
    void DeleteLinkLocalService(const LinkLocalServicesMap::iterator &it);
    void ChangeLinkLocalService(const LinkLocalServicesMap::iterator &old_it,
                                const LinkLocalServicesMap::iterator &new_it);

    OperDB *oper_;
    LinkLocalServicesMap linklocal_services_map_;
    boost::scoped_ptr<LinkLocalRouteManager> linklocal_route_mgr_;
    boost::scoped_ptr<FabricDnsResolver> fabric_dns_resolver_;
    boost::scoped_ptr<AgentRouteEncap> agent_route_encap_update_walker_;
};

#endif // vnsw_agent_global_router_h_
