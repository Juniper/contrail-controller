/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_bgp_as_service_hpp
#define vnsw_agent_bgp_as_service_hpp

#include "oper/interface_common.h"
#include <base/index_allocator.h>
#include "oper/global_system_config.h"

////////////////////////////////////////////////////////////////////////////
// BGP as a service
//
// Function:
// This service enables a VM tries to establish BGP session to control-node.
// It will not try to connect to control-node directly as its unaware of same,
// instaead it will try to connect to its gateway or DNS ip.
// For example in subnet of 1.1.1.0/24, VM will try to connect on well defined
// BGP port to either 1.1.1.1(=gw) or 1.1.1.2(=DNS). Agent sees this traffic and
// creates a NAT. The calculation of NAT is done as follows:
// Pkt from VM:
// source: VM-SIP, destination: DIP(gw/dns), source port: VM-sport, destination
// port: BGP-port.
//
// After NAT:
// source: vrouter IP, destination: Control-node#1(if DIP was gw),
// Control-node#2(if DIP was DNS), source port: BGP-router port,
// destination port: BGP-port.
//
// This way VM is nat'd to control-node.
// Config object(bgp-router) will provide BGP-router port used in NAT.
// If new set of control-node changes flows should use new set given.
//
// What all is done here?
//
// 1) Reserves a set of BGP port which can potentially be used in
// bgp-router object.
// This is provided via contrail-vrouter-agent.conf and agents binds on
// to these ports, so that host does not use it.
//
// 2) Handles config changes. ProcessConfig is called from VM interfaces.
// It traverses the link from VM to bgp-as-a-service to get peer ip which VM
// may use to peer with control-node. Note: This may be VM IP or additional
// IP provisioned for bgp in VM. From bgp-as-a-service config of bgp-router
// is taken and that will tell the port number used for source nat'ng VM
// traffic to control-node. Lastly it takes VRF from bgp-router to validate
// that bgp-router and bgp-as-a-service belong to same VRF as of VM.
//
// 3) Validators - Flow uses these to verify if a flow can be catgorised for
// BGP service or not. It also provides the control-node to be used for
// nat'ng based on VM destination.
////////////////////////////////////////////////////////////////////////////

#define BGP_ROUTER_CONFIG_NAME "bgp-router"
#define BGP_AS_SERVICE_CONFIG_NAME "bgp-as-a-service"
#define BGPAAS_CONTROL_NODE_ZONE_CONFIG_NAME "bgpaas-control-node-zone"
#define VALID_BGP_ROUTER_TYPE "bgpaas-client"

extern SandeshTraceBufferPtr BgpAsAServiceTraceBuf;
#define BGPASASERVICETRACE(obj, ...)                                                     \
do {                                                                                     \
    BgpAsAService##obj::TraceMsg(BgpAsAServiceTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false)

class IFMapNode;
class BgpAsAService {
public:
    static const uint32_t DefaultBgpPort = 179;
    typedef boost::function<void(boost::uuids::uuid, uint32_t)> ServiceDeleteCb;
    typedef boost::function<void(const boost::uuids::uuid &, uint32_t,
                                 const boost::uuids::uuid &, bool)> HealthCheckCb;

    //Keep the BGP as a service data here.
    //Is used when flow is established or when CN is updated.
    struct BgpAsAServiceEntry : public VmInterface::ListEntry {
        BgpAsAServiceEntry();
        BgpAsAServiceEntry(const BgpAsAServiceEntry &rhs);
        BgpAsAServiceEntry(const IpAddress &local_peer_ip,
                           uint32_t source_port,
                           uint32_t dest_port,
                           bool health_check_configured,
                           const boost::uuids::uuid &health_check_uuid,
                           bool is_shared,
                           uint64_t hc_delay_usecs,
                           uint64_t hc_timeout_usecs,
                           uint32_t hc_retries,
                           const std::string &primary_control_node_zone,
                           const std::string &secondary_control_node_zone);
        ~BgpAsAServiceEntry();
        bool operator == (const BgpAsAServiceEntry &rhs) const;
        bool operator() (const BgpAsAServiceEntry &lhs,
                         const BgpAsAServiceEntry &rhs) const;
        bool IsLess(const BgpAsAServiceEntry *rhs) const;

        bool IsControlNodeZoneConfigured() const {
            return (primary_control_node_zone_.size() ||
                secondary_control_node_zone_.size());
        }

        bool installed_;
        IpAddress local_peer_ip_;
        uint32_t source_port_;
        mutable uint32_t dest_port_;
        mutable bool health_check_configured_;
        mutable boost::uuids::uuid health_check_uuid_;
        // the following three are used to invoke add / delete of health check
        // after health check audit is done
        mutable bool new_health_check_add_;
        mutable bool old_health_check_delete_;
        mutable boost::uuids::uuid old_health_check_uuid_;
        mutable uint64_t hc_delay_usecs_;
        mutable uint64_t hc_timeout_usecs_;
        mutable uint32_t hc_retries_;
        bool is_shared_;
        mutable std::string primary_control_node_zone_;
        mutable std::string secondary_control_node_zone_;
        mutable std::string primary_bgp_peer_;
        mutable std::string secondary_bgp_peer_;
    };
    typedef std::set<BgpAsAServiceEntry, BgpAsAServiceEntry> BgpAsAServiceEntryList;
    typedef BgpAsAServiceEntryList::iterator BgpAsAServiceEntryListIterator;
    typedef BgpAsAServiceEntryList::const_iterator BgpAsAServiceEntryListConstIterator;

    struct BgpAsAServiceList {
        BgpAsAServiceList() : list_() { }
        BgpAsAServiceList(BgpAsAServiceEntryList list) : list_(list) { };
        ~BgpAsAServiceList() { }
        void Insert(const BgpAsAServiceEntry *rhs);
        void Update(const BgpAsAServiceEntry *lhs,
                    const BgpAsAServiceEntry *rhs);
        void Remove(BgpAsAServiceEntryListIterator &it);
        void Flush();

        BgpAsAServiceEntryList list_;
    };
    typedef std::map<boost::uuids::uuid, BgpAsAServiceList*> BgpAsAServiceEntryMap;
    typedef BgpAsAServiceEntryMap::iterator BgpAsAServiceEntryMapIterator;
    typedef BgpAsAServiceEntryMap::const_iterator BgpAsAServiceEntryMapConstIterator;

    typedef std::map<uint32_t, IndexVector<boost::uuids::uuid>* > BgpAsAServicePortMap;
    typedef BgpAsAServicePortMap::iterator BgpAsAServicePortMapIterator;
    typedef BgpAsAServicePortMap::const_iterator BgpAsAServicePortMapConstIterator;

    BgpAsAService(const Agent *agent);
    ~BgpAsAService();

    bool IsBgpService(const VmInterface *vm_intf,
                      const IpAddress &source_ip,
                      const IpAddress &dest_ip) const;
    bool GetBgpRouterServiceDestination(const VmInterface *vm_intf,
                                        const IpAddress &source,
                                        const IpAddress &dest,
                                        IpAddress *nat_server,
                                        uint32_t *sport, uint32_t *dport) const;
    bool GetBgpHealthCheck(const VmInterface *vm_intf,
                           boost::uuids::uuid *health_check_uuid) const;
    size_t AllocateBgpVmiServicePortIndex(const uint32_t sport,
                                          const boost::uuids::uuid vm_uuid);
    void FreeBgpVmiServicePortIndex(const uint32_t sport);
    uint32_t AddBgpVmiServicePortIndex(const uint32_t source_port,
                                       const boost::uuids::uuid vm_uuid);
    void ProcessConfig(const std::string &vrf_name,
                       std::list<IFMapNode *> &bgp_router_node_list,
                       std::list<IFMapNode *> &bgp_as_service_node_list,
                       const boost::uuids::uuid &vmi_uuid);
    void DeleteVmInterface(const boost::uuids::uuid &vmi_uuid);
    const BgpAsAService::BgpAsAServiceEntryMap &bgp_as_a_service_map() const;
    const BgpAsAService::BgpAsAServicePortMap &bgp_as_a_service_port_map() const;
    void RegisterServiceDeleteCb(ServiceDeleteCb callback) {
        service_delete_cb_list_.push_back(callback);
    }
    void RegisterHealthCheckCb(HealthCheckCb callback) {
        health_check_cb_list_.push_back(callback);
    }

    bool IsConfigured() {
        if (bgp_as_a_service_entry_map_.size()) {
            return true;
        } else {
            return false;
        }
    }
    BGPaaServiceParameters::BGPaaServicePortRangePair
                                        bgp_as_a_service_port_range() const {
        return std::make_pair(bgp_as_a_service_parameters_.port_start,
                                    bgp_as_a_service_parameters_.port_end);
    }
    void UpdateBgpAsAServiceSessionInfo();

private:
    void StartHealthCheck(const boost::uuids::uuid &vm_uuid,
                          const BgpAsAServiceEntryList &list);
    void BuildBgpAsAServiceInfo(IFMapNode *bgp_as_a_service_node,
                                std::list<IFMapNode *> &bgp_router_nodes,
                                BgpAsAServiceEntryList &new_list,
                                const std::string &vrf_name,
                                const boost::uuids::uuid &vm_uuid);

    const Agent *agent_;
    BgpAsAServiceEntryMap bgp_as_a_service_entry_map_;
    BgpAsAServicePortMap  bgp_as_a_service_port_map_;
    std::vector<ServiceDeleteCb> service_delete_cb_list_;
    std::vector<HealthCheckCb> health_check_cb_list_;
    BGPaaServiceParameters bgp_as_a_service_parameters_;
    DISALLOW_COPY_AND_ASSIGN(BgpAsAService);
};
#endif
