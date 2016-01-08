/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_bgp_aas_hpp
#define vnsw_agent_bgp_aas_hpp

class IFMapNode;
#include "oper/interface_common.h"
#define DEFAULT_CONTROL_NODE_BGP_PORT 179

class BgpAsAService {
public:
    typedef boost::function<void(boost::uuids::uuid, uint32_t)> Callback;

    //Keep the BGP as a service data here.
    //Is used when flow is established or when CN is updated.
    struct BgpAsAServiceEntry : public VmInterface::ListEntry {
        BgpAsAServiceEntry();
        BgpAsAServiceEntry(const BgpAsAServiceEntry &rhs);
        BgpAsAServiceEntry(const IpAddress &local_peer_ip,
                           uint32_t source_port,
                           const std::string &vrf_name);
        ~BgpAsAServiceEntry();
        bool operator == (const BgpAsAServiceEntry &rhs) const;
        bool operator() (const BgpAsAServiceEntry &lhs,
                         const BgpAsAServiceEntry &rhs) const;
        bool IsLess(const BgpAsAServiceEntry *rhs) const;

        IpAddress local_peer_ip_;
        uint32_t source_port_;
        std::string vrf_name_;
    };
    typedef std::set<BgpAsAServiceEntry, BgpAsAServiceEntry> BgpAsAServiceEntryList;
    typedef BgpAsAServiceEntryList::iterator BgpAsAServiceEntryListIterator;
    typedef BgpAsAServiceEntryList::const_iterator BgpAsAServiceEntryListConstIterator;

    struct BgpAsAServiceList {
        BgpAsAServiceList() : list_() { };
        BgpAsAServiceList(BgpAsAServiceEntryList list) : list_(list) { };
        ~BgpAsAServiceList() { };
        void Insert(const BgpAsAServiceEntry *rhs);
        void Update(const BgpAsAServiceEntry *lhs,
                    const BgpAsAServiceEntry *rhs);
        void Remove(BgpAsAServiceEntryListIterator &it);
        void Flush();

        BgpAsAServiceEntryList list_;
    };
    typedef std::map<boost::uuids::uuid, BgpAsAServiceList> BgpAsAServiceEntryMap;
    typedef BgpAsAServiceEntryMap::iterator BgpAsAServiceEntryMapIterator;
    typedef BgpAsAServiceEntryMap::const_iterator BgpAsAServiceEntryMapConstIterator;

    BgpAsAService(const Agent *agent);
    ~BgpAsAService();

    bool IsBgpServicePort(uint32_t port);
    bool IsBgpService(const VmInterface *vm_intf,
                      const IpAddress &source_ip,
                      const IpAddress &dest_ip) const;
    bool GetBgpRouterServiceDestination(const VmInterface *vm_intf,
                                        const IpAddress &source,
                                        const IpAddress &dest,
                                        IpAddress *nat_server,
                                        uint32_t *sport) const;
    void BgpAsAServiceIfMapNodeRequest(const std::string &vrf_name,
                                       std::list<IFMapNode *> &node_list,
                                       const boost::uuids::uuid &vm_uuid);
    void DeleteVmInterface(const boost::uuids::uuid &vm_uuid);
    const BgpAsAService::BgpAsAServiceEntryMap &bgp_as_a_service_map() const;
    void RegisterCallback(Callback callback) { callback_ = callback; }

private:
    void BindBgpAsAServicePorts(const std::string &port_range);
    void BuildBgpAsAServiceInfo(IFMapNode *bgp_as_a_service_node,
                                BgpAsAServiceEntryList &new_list,
                                const std::string &vrf_name);

    const Agent *agent_;
    BgpAsAServiceEntryMap bgp_as_a_service_entry_map_;
    Callback callback_;
    DISALLOW_COPY_AND_ASSIGN(BgpAsAService);
};
#endif
