/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_router_id_h
#define vnsw_agent_router_id_h

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include "interface.h"
#include "vn.h"

/****************************************************************************
 * Module responsible to keep host-os and agent in-sync
 * - Adds route to host-os for link-local addresses allocated for a vm-interface
 * - If VHOST interface is not configured with IP address, will read IP address
 *   from host-os and update agent
 * - Notifies creation of xapi* interface
 ****************************************************************************/

#define XAPI_INTF_PREFIX "xapi"

#define VNSWIF_TRACE_BUF "VnswIfTrace"

extern SandeshTraceBufferPtr VnswIfTraceBuf;

#define VNSWIF_TRACE(...) do {                                       \
        VnswIfInfoTrace::TraceMsg(VnswIfTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (false)

class VnswInterfaceListenerBase {
public:
    static const int kVnswRtmProto = 109;
    static const uint32_t kMaxBufferSize = 4096;

    struct Event {
        enum Type {
            NONE,
            ADD_ADDR,
            DEL_ADDR,
            ADD_INTERFACE,
            DEL_INTERFACE,
            ADD_ROUTE,
            DEL_ROUTE,
            ADD_LL_ROUTE,
            DEL_LL_ROUTE
        };

        // Constructor for add/delete/change of link-local route
        Event(Type event, const std::string &interface, const Ip4Address &addr):
            event_(event), interface_(interface), addr_(addr),
            plen_(Address::kMaxV4PrefixLen), gw_(0), flags_(0), protocol_(0),
            ipam_(false) {
        }

        // Constructor for interface add/delete/change notification
        Event(Type event, const std::string &interface, uint32_t flags,
                unsigned short int type) :
            event_(event), interface_(interface), addr_(0), plen_(0),
            gw_(0), flags_(flags), protocol_(0), ipam_(false), type_(type) {
        }

        // Constructor for interface address add/delete/change notification
        Event(Type event, const std::string &interface, const Ip4Address &addr,
              uint8_t plen, uint32_t flags, bool ipam) :
            event_(event), interface_(interface), addr_(addr), plen_(plen),
            gw_(0), flags_(flags), protocol_(0), ipam_(ipam) {
        }

        // Constructor for route add/delete/change notification
        Event(Type event, const Ip4Address &addr, uint8_t plen,
              const std::string &interface, const Ip4Address &gw,
              uint8_t protocol, uint32_t flags) :
            event_(event), interface_(interface), addr_(addr), plen_(plen),
            flags_(flags), protocol_(protocol), ipam_(false) {
        }

        Type event_;
        std::string interface_;
        Ip4Address addr_;
        uint8_t plen_;
        Ip4Address gw_;
        uint32_t flags_;
        uint8_t protocol_;
        bool ipam_;
        unsigned short int type_;
    };
    enum bond_intf_type {
        VR_FABRIC = 1,
        VR_BOND_SLAVES,
    };

    struct HostInterfaceEntry {
        HostInterfaceEntry() :
            addr_(0), plen_(0), link_up_(true), oper_seen_(false),
            host_seen_(false), oper_id_(Interface::kInvalidIndex) {
        }

        ~HostInterfaceEntry() { }
        Ip4Address addr_;
        uint8_t plen_;
        bool link_up_;
        bool oper_seen_;
        bool host_seen_;
        uint32_t oper_id_;
    };

    class VnDBState : public DBState {
    public:
        void Add(VnswInterfaceListenerBase *base, const VnEntry *vn);
        void Delete(VnswInterfaceListenerBase *base);
        void Enqueue(VnswInterfaceListenerBase *base, const VnIpam &entry,
                     const Event::Type event);
    private:
        std::set<VnIpam> ipam_list_;
    };

    class IpSubnet {
    public:
        IpSubnet(const Ip4Address &ip, uint8_t plen):
            ip_(ip), plen_(plen) {}

        bool operator < (const IpSubnet &rhs) const {
            if (ip_ != rhs.ip_) {
                return ip_ < rhs.ip_;
            }

            return plen_ < rhs.plen_;
        }

        Ip4Address ip_;
        uint8_t plen_;
    };

    bool AddIpam(const Ip4Address &ip, uint8_t plen) {
        bool ret = false;
        IpSubnet ips(ip, plen);
        if (ipam_subnet_.find(ips) == ipam_subnet_.end()) {
            ipam_subnet_[ips] = 0;
            ret = true;
        }
        ipam_subnet_[ips]++;
        return ret;
    }

    bool DelIpam(const Ip4Address &ip, uint8_t plen) {
        IpSubnet ips(ip, plen);
        if (ipam_subnet_.find(ips) == ipam_subnet_.end()) {
            return false;
        }

        ipam_subnet_[ips]--;
        if (ipam_subnet_[ips] == 0) {
            ipam_subnet_.erase(ips);
            return true;
        }

        return false;
    }

    Agent* agent() {
        return agent_;
    }
protected:
    typedef std::map<std::string, HostInterfaceEntry *> HostInterfaceTable;
    typedef std::set<Ip4Address> LinkLocalAddressTable;
    typedef std::map<IpSubnet, uint32_t> IpamSubnetMap;

public:
    VnswInterfaceListenerBase(Agent *agent);
    virtual ~VnswInterfaceListenerBase();

    void Init();
    void Shutdown();
    bool IsValidLinkLocalAddress(const Ip4Address &addr) const;
    void Enqueue(Event *event);
    uint32_t GetHostInterfaceCount() const {
        return host_interface_table_.size();
    }

    HostInterfaceEntry *GetHostInterfaceEntry(const std::string &name);

    uint32_t vhost_update_count() const { return vhost_update_count_; }

    uint32_t ll_add_count() const { return ll_add_count_; }
    uint32_t ll_del_count() const { return ll_del_count_; }

    bool IsHostLinkStateUp(const std::string &name) const {
        HostInterfaceTable::const_iterator it = host_interface_table_.find(name);
        bool link_state = false;
        if (it != host_interface_table_.end())
            link_state = it->second->link_up_;
        return link_state;
    }

protected:
    friend class TestVnswIf;
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);
    void FabricRouteNotify(DBTablePartBase *part, DBEntryBase *e);
    void VnNotify(DBTablePartBase *part, DBEntryBase *e);

// Pure virtuals to be implemented by derivative class
    virtual int CreateSocket() = 0;
    virtual void SyncCurrentState() = 0;
    virtual void RegisterAsyncReadHandler() = 0;
    virtual void UpdateLinkLocalRoute(const Ip4Address &addr, uint8_t plen,
                                      bool del_rt) = 0;

    bool ProcessEvent(Event *re);

    virtual void UpdateLinkLocalRouteAndCount(const Ip4Address &addr,
                                              uint8_t plen, bool del_rt);
    void LinkLocalRouteFromLinkLocalEvent(Event *event);
    void LinkLocalRouteFromRouteEvent(Event *event);
    void AddLinkLocalRoutes();
    void DelLinkLocalRoutes();
    void AddIpamRoutes();

    void SetSeen(const std::string &name, bool oper, uint32_t oper_idx);
    void ResetSeen(const std::string &name, bool oper);
    void Activate(const std::string &name, const HostInterfaceEntry *entry);
    void DeActivate(const std::string &name, const HostInterfaceEntry *entry);
    void SetLinkState(const std::string &name, bool link_up);
    bool IsInterfaceActive(const HostInterfaceEntry *entry);
    void HandleInterfaceEvent(const Event *event);

    void SetAddress(const Event *event);
    void ResetAddress(const Event *event);
    void HandleAddressEvent(const Event *event);

    Agent *agent_;
    uint8_t *read_buf_;
    uint8_t tx_buf_[kMaxBufferSize];

    DBTableBase::ListenerId intf_listener_id_;
    DBTableBase::ListenerId fabric_listener_id_;
    DBTableBase::ListenerId vn_listener_id_;
    int seqno_;
    bool vhost_intf_up_;

    LinkLocalAddressTable ll_addr_table_;
    HostInterfaceTable host_interface_table_;
    WorkQueue<Event *> *revent_queue_;

    uint32_t vhost_update_count_;
    uint32_t ll_add_count_;
    uint32_t ll_del_count_;
    IpamSubnetMap ipam_subnet_;

    DISALLOW_COPY_AND_ASSIGN(VnswInterfaceListenerBase);
};

#endif
