/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_bgp_as_service_hpp
#define vnsw_agent_bgp_as_service_hpp

#include "oper/interface_common.h"

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
#define VALID_BGP_ROUTER_TYPE "bgpaas-client"

extern SandeshTraceBufferPtr BgpAsAServiceTraceBuf;
#define BGPASASERVICETRACE(obj, ...)                                                     \
do {                                                                                     \
    BgpAsAService##obj::TraceMsg(BgpAsAServiceTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false);

class IFMapNode;
class BgpAsAService {
public:
    static const uint32_t DefaultBgpPort = 179;
    typedef boost::function<void(boost::uuids::uuid, uint32_t)> ServiceDeleteCb;

    //Keep the BGP as a service data here.
    //Is used when flow is established or when CN is updated.
    struct BgpAsAServiceEntry : public VmInterface::ListEntry {
        BgpAsAServiceEntry();
        BgpAsAServiceEntry(const BgpAsAServiceEntry &rhs);
        BgpAsAServiceEntry(const IpAddress &local_peer_ip,
                           uint32_t source_port);
        ~BgpAsAServiceEntry();
        bool operator == (const BgpAsAServiceEntry &rhs) const;
        bool operator() (const BgpAsAServiceEntry &lhs,
                         const BgpAsAServiceEntry &rhs) const;
        bool IsLess(const BgpAsAServiceEntry *rhs) const;

        IpAddress local_peer_ip_;
        uint32_t source_port_;
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

    BgpAsAService(const Agent *agent);
    ~BgpAsAService();

    bool IsBgpService(const VmInterface *vm_intf,
                      const IpAddress &source_ip,
                      const IpAddress &dest_ip) const;
    bool GetBgpRouterServiceDestination(const VmInterface *vm_intf,
                                        const IpAddress &source,
                                        const IpAddress &dest,
                                        IpAddress *nat_server,
                                        uint32_t *sport) const;
    void ProcessConfig(const std::string &vrf_name,
                       std::list<IFMapNode *> &node_list,
                       const boost::uuids::uuid &vmi_uuid);
    void DeleteVmInterface(const boost::uuids::uuid &vmi_uuid);
    const BgpAsAService::BgpAsAServiceEntryMap &bgp_as_a_service_map() const;
    void RegisterServiceDeleteCb(ServiceDeleteCb callback) {
        service_delete_cb_ = callback;
    }

private:
    void BindBgpAsAServicePorts(const std::vector<uint16_t> &ports);
    void BuildBgpAsAServiceInfo(IFMapNode *bgp_as_a_service_node,
                                BgpAsAServiceEntryList &new_list,
                                const std::string &vrf_name);

    const Agent *agent_;
    BgpAsAServiceEntryMap bgp_as_a_service_entry_map_;
    ServiceDeleteCb service_delete_cb_;
    DISALLOW_COPY_AND_ASSIGN(BgpAsAService);
};
#endif
