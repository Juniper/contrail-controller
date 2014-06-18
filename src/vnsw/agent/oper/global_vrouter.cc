/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

// global_vrouter.cc - operational data for global vrouter configuration

#include <boost/foreach.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent_param.h>
#include <vnc_cfg_types.h>
#include <ifmap/ifmap_link.h>
#include <oper/mirror_table.h>
#include <oper/nexthop.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/route_common.h>
#include <oper/operdb_init.h>
#include <oper/global_vrouter.h>
#include <oper/agent_route_encap.h>
#include <base/util.h>

const std::string GlobalVrouter::kMetadataService = "metadata";

////////////////////////////////////////////////////////////////////////////////

// Link local service
GlobalVrouter::LinkLocalService::LinkLocalService(
                         const std::string &service_name,
                         const std::string &fabric_dns_name,
                         const std::vector<Ip4Address> &fabric_ip,
                         uint16_t fabric_port)
    : linklocal_service_name(service_name),
      ipfabric_dns_service_name(fabric_dns_name),
      ipfabric_service_ip(fabric_ip),
      ipfabric_service_port(fabric_port) {}

bool GlobalVrouter::LinkLocalService::operator==(
                    const LinkLocalService &rhs) const {
    if (linklocal_service_name == rhs.linklocal_service_name &&
        ipfabric_dns_service_name == rhs.ipfabric_dns_service_name &&
        ipfabric_service_ip == rhs.ipfabric_service_ip &&
        ipfabric_service_port == rhs.ipfabric_service_port)
        return true;
    return false;
}

bool GlobalVrouter::LinkLocalService::IsAddressInUse(const Ip4Address &ip) const {
     BOOST_FOREACH(Ip4Address ipfabric_addr, ipfabric_service_ip) {
         if (ipfabric_addr == ip)
             return true;
    }
    return false;
}

bool GlobalVrouter::LinkLocalServiceKey::operator<(
                    const LinkLocalServiceKey &rhs) const {
    if (linklocal_service_ip != rhs.linklocal_service_ip)
        return linklocal_service_ip < rhs.linklocal_service_ip;
    return linklocal_service_port < rhs.linklocal_service_port;
}

////////////////////////////////////////////////////////////////////////////////

// Async resolve DNS names (used to resolve names given in linklocal config)
class GlobalVrouter::FabricDnsResolver {
public:
    typedef boost::asio::ip::udp boost_udp;
    static const uint32_t kDnsTimeout = 15 * 60 * 1000; // fifteen minutes

    FabricDnsResolver(GlobalVrouter *vrouter, boost::asio::io_service &io)
        : global_vrouter_(vrouter), io_(io) {
        // start timer to re-resolve the DNS names to IP addresses
        timer_ = TimerManager::CreateTimer(io_, "DnsHandlerTimer");
        timer_->Start(kDnsTimeout,
                      boost::bind(&GlobalVrouter::FabricDnsResolver::OnTimeout, this));
    }
    virtual ~FabricDnsResolver() {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
    }

    // Called in DB context, gives the list of names to be resolved
    void ResolveList(const std::vector<std::string> &name_list) {
        std::vector<Ip4Address> empty_addr_list;
        ResolveMap new_addr_map;
        tbb::mutex::scoped_lock lock(mutex_);
        BOOST_FOREACH(std::string name, name_list) {
            ResolveName(name);
            ResolveMap::iterator it = address_map_.find(name);
            if (it != address_map_.end()) {
                address_map_.erase(it);
                new_addr_map.insert(ResolvePair(name, it->second)); 
            } else {
                new_addr_map.insert(ResolvePair(name, empty_addr_list)); 
            }
        }
        address_map_.swap(new_addr_map);
    }

    // Called from client tasks to resolve name to address
    bool Resolve(const std::string &name, Ip4Address *address) {
        tbb::mutex::scoped_lock lock(mutex_);
        ResolveMap::iterator it = address_map_.find(name);
        if (it != address_map_.end() && it->second.size()) {
            int index = rand() % it->second.size();
            *address = it->second[index];
            return true;
        }
        return false;
    }

    // Timer handler; re-resolve all DNS names
    bool OnTimeout() {
        tbb::mutex::scoped_lock lock(mutex_);
        for (ResolveMap::const_iterator it = address_map_.begin();
             it != address_map_.end(); ++it) {
            ResolveName(it->first);
        }
        return true;
    }

    // Called in DB context
    bool IsAddressInUse(const Ip4Address &ip) {
        tbb::mutex::scoped_lock lock(mutex_);
        for (ResolveMap::const_iterator it = address_map_.begin();
             it != address_map_.end(); ++it) {
            BOOST_FOREACH(Ip4Address address, it->second) {
                if (address == ip)
                    return true;
            }
        }
        return false;
    }

private:
    typedef std::map<std::string, std::vector<Ip4Address> > ResolveMap;
    typedef std::pair<std::string, std::vector<Ip4Address> > ResolvePair;

    void ResolveName(const std::string &name) {
        boost_udp::resolver *resolver = new boost_udp::resolver(io_);

        resolver->async_resolve(
            boost_udp::resolver::query(boost_udp::v4(), name, "domain"),
            boost::bind(&GlobalVrouter::FabricDnsResolver::ResolveHandler, this,
                        _1, _2, name, resolver));
    }

    // called in asio context, handle resolve response 
    void ResolveHandler(const boost::system::error_code& error,
                        boost_udp::resolver::iterator resolve_it,
                        std::string &name, boost_udp::resolver *resolver) {
        std::vector<Ip4Address> old_list;
        ResolveMap::iterator addr_it;
        tbb::mutex::scoped_lock lock(mutex_);
        addr_it = address_map_.find(name);
        if (addr_it != address_map_.end()) {
            old_list.swap(addr_it->second);
            if (!error) {
                boost_udp::resolver::iterator end;
                while (resolve_it != end) {
                    boost_udp::endpoint ep = *resolve_it;
                    addr_it->second.push_back(ep.address().to_v4());
                    resolve_it++;
                }
            }
            global_vrouter_->LinkLocalRouteUpdate(addr_it->second);
        }
        delete resolver;
    }

    Timer *timer_;
    tbb::mutex mutex_;
    ResolveMap address_map_;
    GlobalVrouter *global_vrouter_;
    boost::asio::io_service &io_;
};

////////////////////////////////////////////////////////////////////////////////

// Add / delete routes to ip fabric servers used for link local services
// Also, add / delete receive routes for link local addresses in different VRFs
class GlobalVrouter::LinkLocalRouteManager {
public:
    LinkLocalRouteManager(GlobalVrouter *vrouter) 
        : global_vrouter_(vrouter), vn_id_(DBTableBase::kInvalidId) {}

    virtual ~LinkLocalRouteManager() { DeleteDBClients(); }

    void CreateDBClients();
    void DeleteDBClients();
    void AddArpRoute(const Ip4Address &srv);
    void UpdateAllVns(const LinkLocalServiceKey &key, bool is_add);

private:
    bool VnUpdateWalk(DBEntryBase *entry, const LinkLocalServiceKey key,
                      bool is_add);
    void VnWalkDone();
    bool VnNotify(DBTablePartBase *partition, DBEntryBase *entry);

    GlobalVrouter *global_vrouter_;
    DBTableBase::ListenerId vn_id_;
    std::set<Ip4Address> ipfabric_address_list_;
    std::set<Ip4Address> linklocal_address_list_;
};

void GlobalVrouter::LinkLocalRouteManager::CreateDBClients() {
    vn_id_ = global_vrouter_->oper_db()->agent()->vn_table()->Register(
             boost::bind(&GlobalVrouter::LinkLocalRouteManager::VnNotify,
                         this, _1, _2));
}

void GlobalVrouter::LinkLocalRouteManager::DeleteDBClients() {
    global_vrouter_->oper_db()->agent()->vn_table()->Unregister(vn_id_);
}

void GlobalVrouter::LinkLocalRouteManager::AddArpRoute(const Ip4Address &srv) {
    if (!ipfabric_address_list_.insert(srv).second)
        return;

    Inet4UnicastAgentRouteTable::CheckAndAddArpReq(global_vrouter_->oper_db()->
                                                   agent()->fabric_vrf_name(),
                                                   srv);
}

// Walk thru all the VNs
void GlobalVrouter::LinkLocalRouteManager::UpdateAllVns(
                    const LinkLocalServiceKey &key, bool is_add) {
    if (is_add) {
        if (!linklocal_address_list_.insert(key.linklocal_service_ip).second)
            return;
    } else {
        if (!linklocal_address_list_.erase(key.linklocal_service_ip))
            return;
    }

    Agent *agent = global_vrouter_->oper_db()->agent();
    DBTableWalker *walker = agent->db()->GetWalker();
    walker->WalkTable(VnTable::GetInstance(), NULL,
      boost::bind(&GlobalVrouter::LinkLocalRouteManager::VnUpdateWalk,
                  this, _2, key, is_add),
      boost::bind(&GlobalVrouter::LinkLocalRouteManager::VnWalkDone, this));
}

// Vn Walk method
// For each Vn, add or delete receive route for the specified linklocal service
bool GlobalVrouter::LinkLocalRouteManager::VnUpdateWalk(
    DBEntryBase *entry, const LinkLocalServiceKey key, bool is_add) {

    VnEntry *vn_entry = static_cast<VnEntry *>(entry);
    if (vn_entry->IsDeleted()) {
        return true;
    }

    VrfEntry *vrf_entry = vn_entry->GetVrf();
    if (!vrf_entry) {
        return true;
    }

    Agent *agent = global_vrouter_->oper_db()->agent();
    // Do not create the routes for the default VRF
    if (agent->fabric_vrf_name() == vrf_entry->GetName()) {
        return true;
    }

    Inet4UnicastAgentRouteTable *rt_table =
        static_cast<Inet4UnicastAgentRouteTable *>(vrf_entry->
            GetInet4UnicastRouteTable());

    if (is_add) {
        if (vn_entry->Ipv4Forwarding()) {
            rt_table->AddVHostRecvRoute(agent->link_local_peer(),
                                        vrf_entry->GetName(),
                                        agent->vhost_interface_name(),
                                        key.linklocal_service_ip, 32,
                                        vn_entry->GetName(),
                                        true);
        }
    } else {
        rt_table->DeleteReq(agent->link_local_peer(), vrf_entry->GetName(),
                            key.linklocal_service_ip, 32, NULL);
    }
    return true;
}

void GlobalVrouter::LinkLocalRouteManager::VnWalkDone() {
}

// VN notify handler
bool GlobalVrouter::LinkLocalRouteManager::VnNotify(DBTablePartBase *partition,
                                                    DBEntryBase *entry) {
    VnEntry *vn_entry = static_cast<VnEntry *>(entry);
    VrfEntry *vrf_entry = vn_entry->GetVrf();
    Agent *agent = global_vrouter_->oper_db()->agent();
    if (vn_entry->IsDeleted() || !vn_entry->Ipv4Forwarding() || !vrf_entry) {
        LinkLocalDBState *state = static_cast<LinkLocalDBState *> 
            (vn_entry->GetState(partition->parent(), vn_id_));
        if (!state)
            return true;
        Inet4UnicastAgentRouteTable *rt_table =
            static_cast<Inet4UnicastAgentRouteTable *>
            (state->vrf_->GetInet4UnicastRouteTable());
        const GlobalVrouter::LinkLocalServicesMap &services =
                   global_vrouter_->linklocal_services_map();
        for (GlobalVrouter::LinkLocalServicesMap::const_iterator it =
             services.begin(); it != services.end(); ++it) {
            rt_table->DeleteReq(agent->link_local_peer(),
                                state->vrf_->GetName(),
                                it->first.linklocal_service_ip, 32, NULL);
        }
        vn_entry->ClearState(partition->parent(), vn_id_);
        delete state;
        return true;
    }

    // Do not create the routes for the default VRF
    if (agent->fabric_vrf_name() == vrf_entry->GetName()) {
        return true;
    }

    if (vn_entry->Ipv4Forwarding()) {
        if (vn_entry->GetState(partition->parent(), vn_id_))
            return true;
        LinkLocalDBState *state = new LinkLocalDBState(vrf_entry);
        vn_entry->SetState(partition->parent(), vn_id_, state);
        Inet4UnicastAgentRouteTable *rt_table =
            static_cast<Inet4UnicastAgentRouteTable *>
            (vrf_entry->GetInet4UnicastRouteTable());

        const GlobalVrouter::LinkLocalServicesMap &services =
                   global_vrouter_->linklocal_services_map();
        for (GlobalVrouter::LinkLocalServicesMap::const_iterator it =
             services.begin(); it != services.end(); ++it) {
            rt_table->AddVHostRecvRoute(agent->link_local_peer(),
                                        vrf_entry->GetName(),
                                        agent->vhost_interface_name(),
                                        it->first.linklocal_service_ip, 32,
                                        vn_entry->GetName(),
                                        true);
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

GlobalVrouter::GlobalVrouter(OperDB *oper)
    : oper_(oper), linklocal_services_map_(),
      linklocal_route_mgr_(new LinkLocalRouteManager(this)),
      fabric_dns_resolver_(new FabricDnsResolver(this,
                           *(oper->agent()->event_manager()->io_service()))),
      agent_route_encap_update_walker_(new AgentRouteEncap(oper->agent())) {
}

GlobalVrouter::~GlobalVrouter() {
}

void GlobalVrouter::CreateDBClients() {
    linklocal_route_mgr_->CreateDBClients();
}

// Handle incoming global vrouter configuration
void GlobalVrouter::GlobalVrouterConfig(IFMapNode *node) {
    Agent::VxLanNetworkIdentifierMode cfg_vxlan_network_identifier_mode = 
                                            Agent::AUTOMATIC;
    bool encap_changed = false;
    if (node->IsDeleted() == false) {
        autogen::GlobalVrouterConfig *cfg = 
            static_cast<autogen::GlobalVrouterConfig *>(node->GetObject());
        encap_changed = TunnelType::EncapPrioritySync(cfg->encapsulation_priorities());
        if (cfg->vxlan_network_identifier_mode() == "configured") {
            cfg_vxlan_network_identifier_mode = Agent::CONFIGURED;
        }
        UpdateLinkLocalServiceConfig(cfg->linklocal_services());
    } else {
        DeleteLinkLocalServiceConfig();
        TunnelType::DeletePriorityList();
        encap_changed = true;
    }

    if (cfg_vxlan_network_identifier_mode !=                             
        oper_->agent()->vxlan_network_identifier_mode()) {
        oper_->agent()->set_vxlan_network_identifier_mode(
                        cfg_vxlan_network_identifier_mode);
        oper_->agent()->vn_table()->UpdateVxLanNetworkIdentifierMode();
        oper_->agent()->interface_table()->
                        UpdateVxLanNetworkIdentifierMode();
    }

    if (encap_changed) {
        AGENT_LOG(GlobalVrouterLog, "Rebake all routes for changed encap");
        agent_route_encap_update_walker_.get()->Update();
    }
}

// Get link local service configuration info, for a given service name
bool GlobalVrouter::FindLinkLocalService(const std::string &service_name,
                                         Ip4Address *service_ip,
                                         uint16_t *service_port,
                                         Ip4Address *fabric_ip,
                                         uint16_t *fabric_port) {
    std::string name = boost::to_lower_copy(service_name);

    for (GlobalVrouter::LinkLocalServicesMap::const_iterator it =
         linklocal_services_map_.begin();
         it != linklocal_services_map_.end(); ++it) {
        if (it->second.linklocal_service_name == name) {
            *service_ip = it->first.linklocal_service_ip;
            *service_port = it->first.linklocal_service_port;
            *fabric_port = it->second.ipfabric_service_port;
            if (it->second.ipfabric_service_ip.size()) {
                // if there are multiple addresses, return one of them
                int index = rand() % it->second.ipfabric_service_ip.size();
                *fabric_ip = it->second.ipfabric_service_ip[index];
                return true;
            } else if (!it->second.ipfabric_dns_service_name.empty()) {
                return fabric_dns_resolver_->Resolve(
                       it->second.ipfabric_dns_service_name, fabric_ip);
            }
        }
    }
    return false;
}

// Get link local service info for a given linklocal service <ip, port>
bool GlobalVrouter::FindLinkLocalService(const Ip4Address &service_ip,
                                         uint16_t service_port,
                                         std::string *service_name,
                                         Ip4Address *fabric_ip,
                                         uint16_t *fabric_port) {
    LinkLocalServicesMap::iterator it =
        linklocal_services_map_.find(LinkLocalServiceKey(service_ip,
                                                         service_port));
    if (it == linklocal_services_map_.end()) {
#if 0
        if (!service_port) {
            // to support ping to metadata address
            Ip4Address metadata_service_ip, metadata_fabric_ip;
            uint16_t metadata_service_port, metadata_fabric_port;
            if (FindLinkLocalService(GlobalVrouter::kMetadataService,
                                     &metadata_service_ip,
                                     &metadata_service_port,
                                     &metadata_fabric_ip,
                                     &metadata_fabric_port) &&
                service_ip == metadata_service_ip) {
                *fabric_port = oper_->agent()->metadata_server_port();
                *fabric_ip = oper_->agent()->router_id();
                return true;
            }
        }
#endif
        return false;
    }

    *service_name = it->second.linklocal_service_name;
    if (*service_name == GlobalVrouter::kMetadataService) {
        // for metadata, return vhost0 ip and HTTP proxy port
        *fabric_port = oper_->agent()->metadata_server_port();
        *fabric_ip = oper_->agent()->router_id();
        return true;
    }

    *fabric_port = it->second.ipfabric_service_port;
    // if there are multiple addresses, return one of them
    if (it->second.ipfabric_service_ip.size()) {
        int index = rand() % it->second.ipfabric_service_ip.size();
        *fabric_ip = it->second.ipfabric_service_ip[index];
        return true;
    } else if (!it->second.ipfabric_dns_service_name.empty()) {
        return fabric_dns_resolver_->Resolve(
               it->second.ipfabric_dns_service_name, fabric_ip);
    }
    return false;
}

// Handle changes to link local service configuration
void GlobalVrouter::UpdateLinkLocalServiceConfig(
    const LinkLocalServiceList &linklocal_list) {

    std::vector<std::string> dns_name_list;
    LinkLocalServicesMap linklocal_services_map;
    for (std::vector<autogen::LinklocalServiceEntryType>::const_iterator it =
         linklocal_list.begin(); it != linklocal_list.end(); it++) {
        boost::system::error_code ec;
        Ip4Address llip = Ip4Address::from_string(it->linklocal_service_ip, ec);
        std::vector<Ip4Address> fabric_ip;
        BOOST_FOREACH(std::string ip_fabric_ip, it->ip_fabric_service_ip) {
            Ip4Address ip = Ip4Address::from_string(ip_fabric_ip, ec);
            if (ec) continue;
            fabric_ip.push_back(ip);
        }
        std::string name = boost::to_lower_copy(it->linklocal_service_name);
        linklocal_services_map.insert(LinkLocalServicesPair(
            LinkLocalServiceKey(llip, it->linklocal_service_port),
            LinkLocalService(name, it->ip_fabric_DNS_service_name,
                             fabric_ip, it->ip_fabric_service_port)));
        if (!it->ip_fabric_DNS_service_name.empty())
            dns_name_list.push_back(it->ip_fabric_DNS_service_name);
    }

    linklocal_services_map_.swap(linklocal_services_map);
    fabric_dns_resolver_->ResolveList(dns_name_list);
    ChangeNotify(&linklocal_services_map, &linklocal_services_map_);
}

void GlobalVrouter::DeleteLinkLocalServiceConfig() {
    std::vector<std::string> dns_name_list;
    LinkLocalServicesMap linklocal_services_map;
    
    linklocal_services_map_.swap(linklocal_services_map);
    fabric_dns_resolver_->ResolveList(dns_name_list);
    ChangeNotify(&linklocal_services_map, &linklocal_services_map_);
}

// Identify linklocal service configuration changes from old to new
bool GlobalVrouter::ChangeNotify(LinkLocalServicesMap *old_value,
                                 LinkLocalServicesMap *new_value) {
    bool change = false;
    LinkLocalServicesMap::iterator it_old = old_value->begin();
    LinkLocalServicesMap::iterator it_new = new_value->begin();
    while (it_old != old_value->end() && it_new != new_value->end()) {
        if (it_old->first < it_new->first) {
            // old entry is deleted
            DeleteLinkLocalService(it_old);
            change = true;
            it_old++;
        } else if (it_new->first < it_old->first) {
            // new entry
            AddLinkLocalService(it_new);
            change = true;
            it_new++;
        } else if (it_new->second == it_old->second) {
            // no change in entry
            it_old++;
            it_new++;
        } else {
            // change in entry
            ChangeLinkLocalService(it_old, it_new);
            change = true;
            it_old++;
            it_new++;
        }
    }

    // delete remaining old entries
    for (; it_old != old_value->end(); ++it_old) {
        DeleteLinkLocalService(it_old);
        change = true;
    }

    // add remaining new entries
    for (; it_new != new_value->end(); ++it_new) {
        AddLinkLocalService(it_new);
        change = true;
    }

    return change;
}

// New link local service
void GlobalVrouter::AddLinkLocalService(
                    const LinkLocalServicesMap::iterator &it) {
    linklocal_route_mgr_->UpdateAllVns(it->first, true);
    BOOST_FOREACH(Ip4Address ip, it->second.ipfabric_service_ip) {
        linklocal_route_mgr_->AddArpRoute(ip);
    }
}

// Link local service deleted
void GlobalVrouter::DeleteLinkLocalService(
                    const LinkLocalServicesMap::iterator &it) {
    if (!IsLinkLocalAddressInUse(it->first.linklocal_service_ip))
        linklocal_route_mgr_->UpdateAllVns(it->first, false);
}

// Change in Link local service
void GlobalVrouter::ChangeLinkLocalService(
                    const LinkLocalServicesMap::iterator &old_it,
                    const LinkLocalServicesMap::iterator &new_it) {
    if (old_it->first.linklocal_service_ip !=
        new_it->first.linklocal_service_ip) {
        if (!IsLinkLocalAddressInUse(old_it->first.linklocal_service_ip))
            linklocal_route_mgr_->UpdateAllVns(old_it->first, false);
        linklocal_route_mgr_->UpdateAllVns(new_it->first, true);
    }
    LinkLocalRouteUpdate(new_it->second.ipfabric_service_ip);
}

void GlobalVrouter::LinkLocalRouteUpdate(
                    const std::vector<Ip4Address> &addr_list) {
    BOOST_FOREACH(Ip4Address ip, addr_list) {
        linklocal_route_mgr_->AddArpRoute(ip);
    }
}

bool GlobalVrouter::IsAddressInUse(const Ip4Address &ip) const {
    for (LinkLocalServicesMap::const_iterator it =
         linklocal_services_map_.begin(); it != linklocal_services_map_.end();
         ++it) {
        if (it->second.IsAddressInUse(ip))
            return true;
    }
    return fabric_dns_resolver_->IsAddressInUse(ip);
}

bool GlobalVrouter::IsLinkLocalAddressInUse(const Ip4Address &ip) const {
    for (LinkLocalServicesMap::const_iterator it =
         linklocal_services_map_.begin(); it != linklocal_services_map_.end();
         ++it) {
        if (it->first.linklocal_service_ip == ip)
            return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////

void LinkLocalServiceInfo::HandleRequest() const {
    LinkLocalServiceResponse *resp = new LinkLocalServiceResponse();
    std::vector<LinkLocalServiceData> linklocal_list;
    const GlobalVrouter::LinkLocalServicesMap &services =
                   Agent::GetInstance()->oper_db()->
                   global_vrouter()->linklocal_services_map();

    for (GlobalVrouter::LinkLocalServicesMap::const_iterator it =
         services.begin(); it != services.end(); ++it) {
        LinkLocalServiceData service;
        service.linklocal_service_name = it->second.linklocal_service_name;
        service.linklocal_service_ip =
                     it->first.linklocal_service_ip.to_string();
        service.linklocal_service_port = it->first.linklocal_service_port;
        service.ipfabric_dns_name = it->second.ipfabric_dns_service_name;
        BOOST_FOREACH(Ip4Address ip, it->second.ipfabric_service_ip) {
            service.ipfabric_ip.push_back(ip.to_string());
        }
        service.ipfabric_port = it->second.ipfabric_service_port;
        linklocal_list.push_back(service);
    }

    resp->set_service_list(linklocal_list);
    resp->set_context(context());
    resp->Response();
}
