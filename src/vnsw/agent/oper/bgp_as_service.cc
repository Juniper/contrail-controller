/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <string>
#include <vector>
#include <boost/uuid/uuid_io.hpp>

#include <vnc_cfg_types.h>
#include <base/logging.h>
#include <base/string_util.h>
#include <base/bgp_as_service_utils.h>
#include <base/address_util.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <ifmap/ifmap_node.h>
#include <ifmap/ifmap_link.h>
#include <cfg/cfg_init.h>
#include <cmn/agent.h>
#include <oper/operdb_init.h>
#include <oper/bgp_as_service.h>
#include <oper/bgp_router.h>
#include <oper/audit_list.h>
#include <oper/agent_sandesh.h>
#include <oper/config_manager.h>
#include <oper/vn.h>
#include <bgp_schema_types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "oper/global_system_config.h"
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/bgp_as_service_index.h>

SandeshTraceBufferPtr BgpAsAServiceTraceBuf(SandeshTraceBufferCreate
                                            ("BgpAsAService", 500));

BgpAsAService::BgpAsAService(const Agent *agent) :
    agent_(agent),
    bgp_as_a_service_entry_map_(),
    bgp_as_a_service_port_map_(),
    service_delete_cb_list_(), health_check_cb_list_() {
}

BgpAsAService::~BgpAsAService() {
}

const BgpAsAService::BgpAsAServiceEntryMap &BgpAsAService::bgp_as_a_service_map() const {
    return bgp_as_a_service_entry_map_;
}

const BgpAsAService::BgpAsAServicePortMap &BgpAsAService::bgp_as_a_service_port_map() const {
    return bgp_as_a_service_port_map_;
}

void BgpAsAService::BgpAsAServiceList::Insert(const BgpAsAServiceEntry *rhs) {
    if (rhs->health_check_configured_)
        rhs->new_health_check_add_ = true;
    list_.insert(*rhs);
}

void BgpAsAService::BgpAsAServiceList::Update(const BgpAsAServiceEntry *lhs,
                                              const BgpAsAServiceEntry *rhs) {
    lhs->dest_port_ = rhs->dest_port_;
    lhs->primary_control_node_zone_ = rhs->primary_control_node_zone_;
    lhs->secondary_control_node_zone_ = rhs->secondary_control_node_zone_;
    if (rhs->health_check_configured_) {
        if (lhs->hc_delay_usecs_ != rhs->hc_delay_usecs_ ||
            lhs->hc_timeout_usecs_ != rhs->hc_timeout_usecs_ ||
            lhs->hc_retries_ != rhs->hc_retries_) {
            // if HC properties change, trigger update
            lhs->new_health_check_add_ = true;
            lhs->hc_delay_usecs_ = rhs->hc_delay_usecs_;
            lhs->hc_timeout_usecs_ = rhs->hc_timeout_usecs_;
            lhs->hc_retries_ = rhs->hc_retries_;
        }
    }
    if (lhs->health_check_configured_ != rhs->health_check_configured_) {
        lhs->health_check_configured_ = rhs->health_check_configured_;
        if (lhs->health_check_configured_) {
            lhs->old_health_check_delete_ = false;
            lhs->new_health_check_add_ = true;
            lhs->old_health_check_uuid_ = boost::uuids::nil_uuid();
            lhs->health_check_uuid_ = rhs->health_check_uuid_;
        } else {
            lhs->old_health_check_delete_ = true;
            lhs->new_health_check_add_ = false;
            lhs->old_health_check_uuid_ = lhs->health_check_uuid_;
            lhs->health_check_uuid_ = boost::uuids::nil_uuid();
        }
        return;
    }
    if (lhs->health_check_uuid_ != rhs->health_check_uuid_) {
        // delete old health check and add new
        lhs->new_health_check_add_ = true;
        lhs->old_health_check_delete_ = true;
        lhs->old_health_check_uuid_ = lhs->health_check_uuid_;
        lhs->health_check_uuid_ = rhs->health_check_uuid_;
    }
}

void BgpAsAService::BgpAsAServiceList::Remove(BgpAsAServiceEntryListIterator &it) {
    it->set_del_pending(true);
}

void BgpAsAService::BgpAsAServiceList::Flush() {
    list_.clear();
}

void BgpAsAService::StartHealthCheck(const boost::uuids::uuid &vm_uuid,
                                     const BgpAsAServiceEntryList &list) {
    for (BgpAsAServiceEntryListConstIterator iter = list.begin();
         iter != list.end(); ++iter) {
        if (!health_check_cb_list_.empty() && iter->new_health_check_add_ &&
            iter->health_check_uuid_ != boost::uuids::nil_uuid()) {
            std::vector<HealthCheckCb>::iterator hcb_it =
                health_check_cb_list_.begin();
            while (hcb_it != health_check_cb_list_.end()) {
                (*hcb_it)(vm_uuid, iter->source_port_,
                             iter->health_check_uuid_, true);
                hcb_it++;
            }
        }
        iter->new_health_check_add_ = false;
    }
}

static const std::string GetBgpRouterVrfName(const Agent *agent,
                                             IFMapNode *node) {
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator it = node->begin(table->GetGraph());
         it != node->end(table->GetGraph()); ++it) {
        IFMapNode *vrf_node = static_cast<IFMapNode *>(it.operator->());
        if (agent->config_manager()->SkipNode
            (vrf_node, agent->cfg()->cfg_vrf_table())) {
            continue;
        }
        return vrf_node->name();
    }
    return std::string();
}

static const std::string GetControlNodeZoneName(IFMapNode *node) {
    IFMapAgentTable *table =
        static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator it = node->begin(table->GetGraph());
         it != node->end(table->GetGraph()); ++it) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(it.operator->());
        if (strcmp(adj_node->table()->Typename(),
                CONTROL_NODE_ZONE_CONFIG_NAME) == 0) {
            return adj_node->name();
        }
    }
    return std::string();
}

void BgpAsAService::BuildBgpAsAServiceInfo(IFMapNode *bgp_as_a_service_node,
                                      std::list<IFMapNode *> &bgp_router_nodes,
                                      BgpAsAServiceEntryList &new_list,
                                      const std::string &vm_vrf_name,
                                      const boost::uuids::uuid &vm_uuid) {
    IFMapAgentTable *table =
        static_cast<IFMapAgentTable *>(bgp_as_a_service_node->table());
    autogen::BgpAsAService *bgp_as_a_service =
        dynamic_cast<autogen::BgpAsAService *>(bgp_as_a_service_node->GetObject());
    assert(bgp_as_a_service);

    IpAddress peer_ip;
    uint32_t source_port = 0;
    bool health_check_configured = false;
    boost::uuids::uuid health_check_uuid = boost::uuids::nil_uuid();
    uint64_t hc_delay_usecs = 0;
    uint64_t hc_timeout_usecs = 0;
    uint32_t hc_retries = 0;
    std::string primary_control_node_zone;
    std::string secondary_control_node_zone;

    // Find the health check config first
    for (DBGraphVertex::adjacency_iterator it = bgp_as_a_service_node->begin(table->GetGraph());
         it != bgp_as_a_service_node->end(table->GetGraph()); ++it) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(it.operator->());

        if (adj_node->table() == agent_->cfg()->cfg_health_check_table()) {
            if (agent_->config_manager()->SkipNode(adj_node)) {
                continue;
            }
            autogen::ServiceHealthCheck *hc =
                static_cast<autogen::ServiceHealthCheck *> (adj_node->GetObject());
            assert(hc);
            // consider only BFD health check for BGPaaS
            if (hc->properties().monitor_type.find("BFD") == std::string::npos)
                continue;
            health_check_configured = true;
            autogen::IdPermsType id_perms = hc->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       health_check_uuid);
            hc_delay_usecs = hc->properties().delay * 1000000 +
                             hc->properties().delayUsecs;
            hc_timeout_usecs = hc->properties().timeout * 1000000 +
                               hc->properties().timeoutUsecs;
            hc_retries = hc->properties().max_retries;
        }

        if (strcmp(adj_node->table()->Typename(),
                    BGPAAS_CONTROL_NODE_ZONE_CONFIG_NAME) == 0) {
            autogen::BgpaasControlNodeZone
                *bgpaas_cnz = static_cast<autogen::BgpaasControlNodeZone *>
                    (adj_node->GetObject());
            const autogen::BGPaaSControlNodeZoneAttributes &attr =
                static_cast<autogen::BGPaaSControlNodeZoneAttributes>
                    (bgpaas_cnz->data());
            const std::string cnz_name = GetControlNodeZoneName(adj_node);
            if (cnz_name.size()) {
                if (strcmp(attr.bgpaas_control_node_zone_type.c_str(),
                           "primary") == 0) {
                    primary_control_node_zone = cnz_name;
                    continue;
                }
                if (strcmp(attr.bgpaas_control_node_zone_type.c_str(),
                           "secondary") == 0) {
                    secondary_control_node_zone = cnz_name;
                    continue;
                }
            }
        }
    }

    //Look for neighbour bgp-router to take the source port
    for (DBGraphVertex::adjacency_iterator it = bgp_as_a_service_node->begin(table->GetGraph());
         it != bgp_as_a_service_node->end(table->GetGraph()); ++it) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(it.operator->());
        if (strcmp(adj_node->table()->Typename(), BGP_ROUTER_CONFIG_NAME) == 0) {
            //Verify that bgp-router object is of use for this VMI.
            //List of valid bgp-router for vmi is in bgp_router_nodes.
            if (std::find(bgp_router_nodes.begin(), bgp_router_nodes.end(),
                          adj_node) == bgp_router_nodes.end()) {
                continue;
            }
            autogen::BgpRouter *bgp_router=
                dynamic_cast<autogen::BgpRouter *>(adj_node->GetObject());
            const std::string &vrf_name =
                GetBgpRouterVrfName(agent_, adj_node);
            if (vrf_name.empty() || (vrf_name != vm_vrf_name) ||
                (strcmp(bgp_router->parameters().router_type.c_str(),
                        VALID_BGP_ROUTER_TYPE) != 0))
                continue; //Skip the node with no VRF, notification will come.
            boost::system::error_code ec;
            peer_ip =
                IpAddress::from_string(bgp_router->parameters().address, ec);
            if (ec.value() != 0) {
                std::stringstream ss;
                ss << "Ip address parsing failed for ";
                ss << bgp_router->parameters().address;
                BGPASASERVICETRACE(Trace, ss.str().c_str());
                continue;
            }

            BgpAsAServiceEntryMapIterator old_bgp_as_a_service_entry_list_iter =
                                    bgp_as_a_service_entry_map_.find(vm_uuid);
            source_port = 0;
            /*
             *  verify the same session is added again here
             */
            if (old_bgp_as_a_service_entry_list_iter !=
                        bgp_as_a_service_entry_map_.end()) {
                BgpAsAServiceEntryList temp_bgp_as_a_service_entry_list;
                temp_bgp_as_a_service_entry_list.insert(
                                    BgpAsAServiceEntry(peer_ip,
                                        bgp_router->parameters().source_port,
                                        bgp_router->parameters().port,
                                        health_check_configured,
                                        health_check_uuid,
                                        bgp_as_a_service->bgpaas_shared(),
                                        hc_delay_usecs,
                                        hc_timeout_usecs,
                                        hc_retries,
                                        primary_control_node_zone,
                                        secondary_control_node_zone));
                /*
                 * if it is same session then retain original source port
                 */
                if (temp_bgp_as_a_service_entry_list ==
                     old_bgp_as_a_service_entry_list_iter->second->list_) {
                    source_port = bgp_router->parameters().source_port;
                }
            }
            if (!source_port) {
                if (bgp_as_a_service->bgpaas_shared()) {
                    source_port = AddBgpVmiServicePortIndex(
                                        bgp_router->parameters().source_port,
                                        vm_uuid);
                } else {
                    source_port = bgp_router->parameters().source_port;
                }
            }
            if (source_port) {
                new_list.insert(BgpAsAServiceEntry(peer_ip, source_port,
                                            bgp_router->parameters().port,
                                                   health_check_configured,
                                                   health_check_uuid,
                                                   bgp_as_a_service->bgpaas_shared(),
                                                   hc_delay_usecs,
                                                   hc_timeout_usecs,
                                                   hc_retries,
                                                   primary_control_node_zone,
                                                   secondary_control_node_zone));
            }
        }
    }

}

void BgpAsAService::ProcessConfig(const std::string &vrf_name,
                           std::list<IFMapNode *> &bgp_router_node_map,
                           std::list<IFMapNode *> &bgp_as_service_node_map,
                           const boost::uuids::uuid &vm_uuid) {
    std::list<IFMapNode *>::const_iterator it =
        bgp_as_service_node_map.begin();
    BgpAsAServiceEntryList new_bgp_as_a_service_entry_list;

    while (it != bgp_as_service_node_map.end()) {
        BuildBgpAsAServiceInfo(*it, bgp_router_node_map,
                               new_bgp_as_a_service_entry_list,
                               vrf_name, vm_uuid);
        it++;
    }

    //Audit and enqueue updates/deletes of flow
    BgpAsAServiceEntryMapIterator old_bgp_as_a_service_entry_list_iter =
        bgp_as_a_service_entry_map_.find(vm_uuid);
    bool changed = false;
    if (old_bgp_as_a_service_entry_list_iter !=
        bgp_as_a_service_entry_map_.end()) {
        //Audit
        changed = AuditList<BgpAsAServiceList, BgpAsAServiceEntryListIterator>
            (*(old_bgp_as_a_service_entry_list_iter->second),
             old_bgp_as_a_service_entry_list_iter->second->list_.begin(),
             old_bgp_as_a_service_entry_list_iter->second->list_.end(),
             new_bgp_as_a_service_entry_list.begin(),
             new_bgp_as_a_service_entry_list.end());
    } else if (new_bgp_as_a_service_entry_list.size() != 0) {
        StartHealthCheck(vm_uuid, new_bgp_as_a_service_entry_list);
        bgp_as_a_service_entry_map_[vm_uuid] =
            new BgpAsAServiceList(new_bgp_as_a_service_entry_list);
    }

    if (changed && !service_delete_cb_list_.empty()) {
        //Enqueue flow handler request.
        BgpAsAServiceEntryListIterator iter =
            old_bgp_as_a_service_entry_list_iter->second->list_.begin();
        while (iter !=
               old_bgp_as_a_service_entry_list_iter->second->list_.end()) {
            BgpAsAServiceEntryListIterator prev = iter++;
            if (prev->del_pending_) {
                std::vector<ServiceDeleteCb>::iterator scb_it =
                    service_delete_cb_list_.begin();
                while (scb_it != service_delete_cb_list_.end()) {
                    (*scb_it)(vm_uuid, prev->source_port_);
                    scb_it++;
                }
                if (prev->is_shared_) {
                    FreeBgpVmiServicePortIndex(prev->source_port_);
                }
                old_bgp_as_a_service_entry_list_iter->second->list_.erase(prev);
                continue;
            }
            if (prev->old_health_check_delete_) {
                if (!health_check_cb_list_.empty() &&
                    prev->old_health_check_uuid_ != boost::uuids::nil_uuid()) {
                    std::vector<HealthCheckCb>::iterator hcb_it =
                        health_check_cb_list_.begin();
                    while (hcb_it != health_check_cb_list_.end()) {
                        (*hcb_it)(vm_uuid, prev->source_port_,
                                     prev->old_health_check_uuid_, false);
                        hcb_it++;
                     }
                }
                prev->old_health_check_delete_ = false;
                prev->old_health_check_uuid_ = boost::uuids::nil_uuid();
            }
            if (prev->new_health_check_add_) {
                if (!health_check_cb_list_.empty() &&
                    prev->health_check_uuid_ != boost::uuids::nil_uuid()) {
                    std::vector<HealthCheckCb>::iterator hcb_it =
                        health_check_cb_list_.begin();
                    while (hcb_it != health_check_cb_list_.end()) {
                        (*hcb_it)(vm_uuid, prev->source_port_,
                                     prev->health_check_uuid_, true);
                        hcb_it++;
                     }
                }
                prev->new_health_check_add_ = false;
            }
        }
    }
}

void BgpAsAService::DeleteVmInterface(const boost::uuids::uuid &vm_uuid) {
    if (service_delete_cb_list_.empty())
        return;

    BgpAsAServiceEntryMapIterator iter =
       bgp_as_a_service_entry_map_.find(vm_uuid);
    if (iter == bgp_as_a_service_entry_map_.end())
        return;

    BgpAsAServiceEntryList list = iter->second->list_;
    BgpAsAServiceEntryListIterator list_iter = list.begin();
    while (list_iter != list.end()) {
        std::vector<ServiceDeleteCb>::iterator scb_it =
            service_delete_cb_list_.begin();
        while (scb_it != service_delete_cb_list_.end()) {
            (*scb_it)(vm_uuid, (*list_iter).source_port_);
            scb_it++;
        }
        if ((*list_iter).is_shared_) {
            FreeBgpVmiServicePortIndex((*list_iter).source_port_);
        }
        list_iter++;
    }
    delete iter->second;
    bgp_as_a_service_entry_map_.erase(iter);
}


bool BgpAsAService::IsBgpService(const VmInterface *vm_intf,
                                 const IpAddress &source_ip,
                                 const IpAddress &dest_ip) const {
    bool ret = false;
    BgpAsAServiceEntryMapConstIterator iter =
       bgp_as_a_service_entry_map_.find(vm_intf->GetUuid());
    if (iter == bgp_as_a_service_entry_map_.end()) {
        return false;
    }

    while (iter != bgp_as_a_service_entry_map_.end()) {
        BgpAsAService::BgpAsAServiceEntryListIterator it =
            iter->second->list_.begin();
        while (it != iter->second->list_.end()) {
            if ((*it).local_peer_ip_ == source_ip)
                return true;
            it++;
        }
        iter++;
    }

    const VnEntry *vn = vm_intf->vn();
    if (vn == NULL) return false;

    const IpAddress &vm_ip = vm_intf->primary_ip_addr();
    if ((vn->GetGatewayFromIpam(vm_ip) == dest_ip) ||
        (vn->GetDnsFromIpam(vm_ip) == dest_ip)) {
        ret = true;
    }
    return ret;
}

void BgpAsAService::FreeBgpVmiServicePortIndex(const uint32_t sport) {
    BGPaaServiceParameters:: BGPaaServicePortRangePair ports =
                        bgp_as_a_service_port_range();
    BGPaaSUtils::BgpAsServicePortIndexPair portinfo =
                    BGPaaSUtils::DecodeBgpaasServicePort(sport,
                        ports.first, ports.second);

    BgpAsAServicePortMapIterator port_map_it =
                    bgp_as_a_service_port_map_.find(portinfo.first);
    if (port_map_it == bgp_as_a_service_port_map_.end()) {
        return;
    }

    size_t vmi_service_port_index = portinfo.second;
    agent_->resource_manager()->Release(Resource::BGP_AS_SERVICE_INDEX,
                                        vmi_service_port_index);
    port_map_it->second->Remove(vmi_service_port_index);

    if (port_map_it->second->NoneIndexSet()) {
        delete port_map_it->second;
        bgp_as_a_service_port_map_.erase(port_map_it);
    }
}

size_t BgpAsAService::AllocateBgpVmiServicePortIndex(const uint32_t sport,
                                        const boost::uuids::uuid vm_uuid) {
    BgpAsAServicePortMapIterator port_map_it =
                    bgp_as_a_service_port_map_.find(sport);
    if (port_map_it == bgp_as_a_service_port_map_.end()) {
        bgp_as_a_service_port_map_[sport] = new IndexVector<boost::uuids::uuid>();
    }
    ResourceManager::KeyPtr rkey(new BgpAsServiceIndexResourceKey
                                 (agent_->resource_manager(), vm_uuid));
    uint32_t index = static_cast<IndexResourceData *>
        (agent_->resource_manager()->Allocate(rkey).get())->index();
    bgp_as_a_service_port_map_[sport]->InsertAtIndex(index, vm_uuid);
    return index;
}

uint32_t BgpAsAService::AddBgpVmiServicePortIndex(const uint32_t source_port,
                                            const boost::uuids::uuid vm_uuid) {
    BGPaaServiceParameters:: BGPaaServicePortRangePair ports =
                        bgp_as_a_service_port_range();

    size_t vmi_service_port_index = AllocateBgpVmiServicePortIndex(source_port,
                                                                   vm_uuid);
    if (vmi_service_port_index == BitSet::npos) {
        std::stringstream ss;
        ss << "Service Port Index is not available for ";
        ss << source_port;
        BGPASASERVICETRACE(Trace, ss.str().c_str());
        return 0;
    }
    return BGPaaSUtils::EncodeBgpaasServicePort(
                                source_port,
                                vmi_service_port_index,
                                ports.first, ports.second);
}

bool BgpAsAService::GetBgpRouterServiceDestination(
                    const VmInterface *vm_intf, const IpAddress &source_ip,
                    const IpAddress &dest, IpAddress *nat_server,
                    uint32_t *sport, uint32_t *dport) const {
    const VnEntry *vn = vm_intf->vn();
    if (vn == NULL) return false;

    const IpAddress &vm_ip = vm_intf->primary_ip_addr();
    const IpAddress &gw = vn->GetGatewayFromIpam(vm_ip);
    const IpAddress &dns = vn->GetDnsFromIpam(vm_ip);

    std::stringstream ss;
    BgpAsAServiceEntryMapConstIterator map_it =
        bgp_as_a_service_entry_map_.find(vm_intf->GetUuid());
    if (map_it == bgp_as_a_service_entry_map_.end()) {
        ss << "vmi-uuid:" << vm_intf->GetUuid() << " not found";
        BGPASASERVICETRACE(Trace, ss.str().c_str());
        return false;
    }

    BgpRouterConfig *bgp_router_cfg = agent_->oper_db()->bgp_router_config();
    BgpAsAServiceEntryListConstIterator it = map_it->second->list_.begin();
    while (it != map_it->second->list_.end()) {
        bool cnz_configured = it->IsControlNodeZoneConfigured();
        BgpRouterPtr bgp_router;
        IpAddress ip_address;
        std::string xmpp_server;
        std::string control_node_zone;
        std::string *bgp_router_name;
        if (dest == gw) {
            if (cnz_configured) {
                bgp_router = bgp_router_cfg->
                    GetBgpRouterFromControlNodeZone(
                        it->primary_control_node_zone_);
                control_node_zone = it->primary_control_node_zone_;
            } else {
                xmpp_server = agent_->controller_ifmap_xmpp_server(0);
                if (xmpp_server.size())
                    bgp_router = bgp_router_cfg->
                        GetBgpRouterFromXmppServer(xmpp_server);
            }
            bgp_router_name = &it->primary_bgp_peer_;
        } else if (dest == dns) {
            if (cnz_configured) {
                bgp_router = bgp_router_cfg->
                    GetBgpRouterFromControlNodeZone(
                        it->secondary_control_node_zone_);
                control_node_zone = it->secondary_control_node_zone_;
            } else {
                xmpp_server = agent_->controller_ifmap_xmpp_server(1);
                if (xmpp_server.size())
                    bgp_router = bgp_router_cfg->
                        GetBgpRouterFromXmppServer(xmpp_server);
            }
            bgp_router_name = &it->secondary_bgp_peer_;
        } else {
            it++;
            continue;
        }
        if (bgp_router.get() == NULL) {
            ss << "BgpRouter not found.";
            ss << " bgpaas-source-ip:" << source_ip.to_string();
            ss << " bgpaas-destination-ip:" << dest.to_string();
            ss << " control-node-zone:" << control_node_zone;
            ss << " xmpp-server:" << xmpp_server;
            BGPASASERVICETRACE(Trace, ss.str().c_str());
            //reset bgp_router for sandesh
            *bgp_router_name = "";
            return false;
        }
        *nat_server = bgp_router->ipv4_address();
        *sport = it->source_port_;
        *dport = it->dest_port_?it->dest_port_:bgp_router->port();
        //update bgp_router for sandesh
        *bgp_router_name = bgp_router->name();
        return true;
    }
    ss << "BgpRouter not found ";
    ss << " bgpaas-source-ip:" << source_ip.to_string();
    ss << " bgpaas-destination-ip" << dest.to_string();
    BGPASASERVICETRACE(Trace, ss.str().c_str());
    return false;
}

bool BgpAsAService::GetBgpHealthCheck(const VmInterface *vm_intf,
                    boost::uuids::uuid *health_check_uuid) const {
    BgpAsAServiceEntryMapConstIterator iter =
       bgp_as_a_service_entry_map_.find(vm_intf->GetUuid());

    while (iter != bgp_as_a_service_entry_map_.end()) {
        BgpAsAService::BgpAsAServiceEntryListIterator it =
            iter->second->list_.begin();
        while (it != iter->second->list_.end()) {
            if (it->health_check_configured_) {
                *health_check_uuid = it->health_check_uuid_;
                return true;
            }
            it++;
        }
        iter++;
    }

    return false;
}

void BgpAsAService::UpdateBgpAsAServiceSessionInfo() {
    if (service_delete_cb_list_.empty())
        return;
    unsigned int source_port = 0;
    unsigned int index = 0;
    BGPaaServiceParameters:: BGPaaServicePortRangePair old_ports =
                                bgp_as_a_service_port_range();
    BGPaaServiceParameters:: BGPaaServicePortRangePair new_ports =
             agent_->oper_db()->global_system_config()->bgpaas_port_range();
    std::pair<BgpAsAServiceEntryListIterator, bool> ret;
    BgpAsAServiceEntryMapIterator iter =
                bgp_as_a_service_entry_map_.begin();
    while (iter != bgp_as_a_service_entry_map_.end()) {
        BgpAsAServiceEntryList list = iter->second->list_;
        BgpAsAServiceEntryListIterator list_iter = list.begin();
        while (list_iter != list.end()) {
            BGPaaSUtils::BgpAsServicePortIndexPair portinfo =
                BGPaaSUtils::DecodeBgpaasServicePort(
                                list_iter->source_port_,
                                old_ports.first,
                                old_ports.second);
            source_port = portinfo.first;
            index = portinfo.second;
            // encode source port with new port range
            source_port =
                BGPaaSUtils::EncodeBgpaasServicePort(source_port,
                                                    index,
                                                    new_ports.first,
                                                    new_ports.second);
            // if old source port is not same as new source pott
            // delte the flow and entry from the list
            // and then insert the entry with the new source port
            if (list_iter->source_port_ != source_port) {
                BgpAsAServiceEntry temp(list_iter->local_peer_ip_,
                                    source_port,
                                    list_iter->dest_port_,
                                    list_iter->health_check_configured_,
                                    list_iter->health_check_uuid_,
                                    list_iter->is_shared_,
                                    list_iter->hc_delay_usecs_,
                                    list_iter->hc_timeout_usecs_,
                                    list_iter->hc_retries_,
                                    list_iter->primary_control_node_zone_,
                                    list_iter->secondary_control_node_zone_);
                std::vector<ServiceDeleteCb>::iterator scb_it =
                    service_delete_cb_list_.begin();
                while (scb_it != service_delete_cb_list_.end()) {
                    (*scb_it)(iter->first, list_iter->source_port_);
                    scb_it++;
                }
                iter->second->list_.erase(*list_iter);
                iter->second->list_.insert(temp);
            }
            list_iter++;
        }
        iter++;
    }
    //update BgpaaS port range
    bgp_as_a_service_parameters_.port_start = new_ports.first;
    bgp_as_a_service_parameters_.port_end = new_ports.second;
}

////////////////////////////////////////////////////////////////////////////
// BGP as a service routines.
////////////////////////////////////////////////////////////////////////////
BgpAsAService::BgpAsAServiceEntry::BgpAsAServiceEntry() :
    VmInterface::ListEntry(), installed_(false),
    local_peer_ip_(), source_port_(0), dest_port_(0),
    health_check_configured_(false), health_check_uuid_(),
    new_health_check_add_(false),old_health_check_delete_(false),
    old_health_check_uuid_(),hc_delay_usecs_(0),
    hc_timeout_usecs_(0), hc_retries_(0), is_shared_(false),
    primary_control_node_zone_(), secondary_control_node_zone_() {
}

BgpAsAService::BgpAsAServiceEntry::BgpAsAServiceEntry
(const BgpAsAService::BgpAsAServiceEntry &rhs) :
    VmInterface::ListEntry(rhs.del_pending_),
    installed_(rhs.installed_),
    local_peer_ip_(rhs.local_peer_ip_),
    source_port_(rhs.source_port_),
    dest_port_(rhs.dest_port_),
    health_check_configured_(rhs.health_check_configured_),
    health_check_uuid_(rhs.health_check_uuid_),
    new_health_check_add_(rhs.new_health_check_add_),
    old_health_check_delete_(rhs.old_health_check_delete_),
    old_health_check_uuid_(rhs.old_health_check_uuid_),
    hc_delay_usecs_(rhs.hc_delay_usecs_),
    hc_timeout_usecs_(rhs.hc_timeout_usecs_),
    hc_retries_(rhs.hc_retries_), is_shared_(rhs.is_shared_),
    primary_control_node_zone_(rhs.primary_control_node_zone_),
    secondary_control_node_zone_(rhs.secondary_control_node_zone_) {
}

BgpAsAService::BgpAsAServiceEntry::BgpAsAServiceEntry
(const IpAddress &local_peer_ip, uint32_t source_port, uint32_t dest_port,
 bool health_check_configured, const boost::uuids::uuid &health_check_uuid,
 bool is_shared, uint64_t hc_delay_usecs, uint64_t hc_timeout_usecs,
 uint32_t hc_retries, const std::string &primary_control_node_zone,
 const std::string &secondary_control_node_zone) :
    VmInterface::ListEntry(),
    installed_(false),
    local_peer_ip_(local_peer_ip),
    source_port_(source_port),
    dest_port_(dest_port),
    health_check_configured_(health_check_configured),
    health_check_uuid_(health_check_uuid),
    new_health_check_add_(health_check_configured),
    old_health_check_delete_(false), old_health_check_uuid_(),
    hc_delay_usecs_(hc_delay_usecs), hc_timeout_usecs_(hc_timeout_usecs),
    hc_retries_(hc_retries), is_shared_(is_shared),
    primary_control_node_zone_(primary_control_node_zone),
    secondary_control_node_zone_(secondary_control_node_zone) {
}

BgpAsAService::BgpAsAServiceEntry::~BgpAsAServiceEntry() {
}

bool BgpAsAService::BgpAsAServiceEntry::operator ==
    (const BgpAsAServiceEntry &rhs) const {
    return ((source_port_ == rhs.source_port_) &&
            (local_peer_ip_ == rhs.local_peer_ip_) &&
            (is_shared_ == rhs.is_shared_));
}

bool BgpAsAService::BgpAsAServiceEntry::operator()
    (const BgpAsAServiceEntry &lhs, const BgpAsAServiceEntry &rhs) const {
    return lhs.IsLess(&rhs);
}

bool BgpAsAService::BgpAsAServiceEntry::IsLess
    (const BgpAsAServiceEntry *rhs) const {
    if (source_port_ != rhs->source_port_)
        return source_port_ < rhs->source_port_;
    if (local_peer_ip_ != rhs->local_peer_ip_)
         return local_peer_ip_ < rhs->local_peer_ip_;
    if (primary_control_node_zone_ != rhs->primary_control_node_zone_)
        return primary_control_node_zone_ < rhs->primary_control_node_zone_;
    if (secondary_control_node_zone_ != rhs->secondary_control_node_zone_)
        return secondary_control_node_zone_ < rhs->secondary_control_node_zone_;
    return is_shared_ < rhs->is_shared_;
}

void BgpAsAServiceSandeshReq::HandleRequest() const {
   BgpAsAServiceSandeshResp *resp = new BgpAsAServiceSandeshResp();
   resp->set_context(context());

   Agent *agent = Agent::GetInstance();

   BgpAsAService::BgpAsAServiceEntryMap map_entry =
       agent->oper_db()->bgp_as_a_service()->bgp_as_a_service_map();
   BgpAsAService::BgpAsAServiceEntryMapIterator map_it =
       map_entry.begin();
   std::vector<BgpAsAServiceSandeshList> bgpaas_map;
   std::string vmi_uuid_str = get_vmi_uuid();
   while (map_it != map_entry.end()) {
       if (vmi_uuid_str.empty() == false) {
           boost::uuids::uuid vmi_uuid = StringToUuid(vmi_uuid_str);
           if (vmi_uuid != map_it->first) {
               map_it++;
               continue;
           }
       }
       BgpAsAService::BgpAsAServiceEntryListIterator it =
           map_it->second->list_.begin();
       while (it != map_it->second->list_.end()) {
           BgpAsAServiceSandeshList entry;
           entry.set_vm_bgp_peer_ip((*it).local_peer_ip_.to_string());
           entry.set_vm_nat_source_port((*it).source_port_);
           entry.set_vmi_uuid(UuidToString(map_it->first));
           entry.set_health_check_configured((*it).health_check_configured_);
           entry.set_health_check_uuid(UuidToString((*it).health_check_uuid_));
           entry.set_health_check_delay_usecs((*it).hc_delay_usecs_);
           entry.set_health_check_timeout_usecs((*it).hc_timeout_usecs_);
           entry.set_health_check_retries((*it).hc_retries_);
           entry.set_is_shared((*it).is_shared_);
           entry.set_primary_control_node_zone(
                (*it).primary_control_node_zone_);
           entry.set_secondary_control_node_zone(
                (*it).secondary_control_node_zone_);
           entry.set_primary_bgp_peer((*it).primary_bgp_peer_);
           entry.set_secondary_bgp_peer((*it).secondary_bgp_peer_);
           bgpaas_map.push_back(entry);
           it++;
       }
       map_it++;
   }
   resp->set_bgp_as_a_service_list(bgpaas_map);
   resp->Response();
}
