/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_router_id_h
#define vnsw_agent_router_id_h

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>

/****************************************************************************
 * Module responsible to keep host-os and agent in-sync
 * - Adds route to host-os for link-local addresses allocated for a vm-interface
 * - If VHOST interface is not configured with IP address, will read IP address
 *   from host-os and update agent
 * - Notifies creation of xapi* interface
 ****************************************************************************/

#define XAPI_INTF_PREFIX "xapi"

namespace local = boost::asio::local;

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
            event_(event), interface_(interface), addr_(addr), plen_(0),
            gw_(0), flags_(0), protocol_(0) {
        }

        // Constructor for interface add/delete/change notification
        Event(Type event, const std::string &interface, uint32_t flags) :
            event_(event), interface_(interface), addr_(0), plen_(0),
            gw_(0), flags_(flags), protocol_(0) {
        }

        // Constructor for interface address add/delete/change notification 
        Event(Type event, const std::string &interface, const Ip4Address &addr,
              uint8_t plen, uint32_t flags) :
            event_(event), interface_(interface), addr_(addr), plen_(plen),
            gw_(0), flags_(flags), protocol_(0) {
        }

        // Constructor for route add/delete/change notification 
        Event(Type event, const Ip4Address &addr, uint8_t plen, 
              const std::string &interface, const Ip4Address &gw,
              uint8_t protocol, uint32_t flags) :
            event_(event), interface_(interface), addr_(addr), plen_(plen),
            flags_(flags), protocol_(protocol) {
        }

        Type event_;
        std::string interface_;
        Ip4Address addr_;
        uint8_t plen_;
        Ip4Address gw_;
        uint32_t flags_;
        uint8_t protocol_;
    };

    struct HostInterfaceEntry {
        HostInterfaceEntry() : 
            addr_(0), plen_(0), link_up_(false), oper_seen_(false),
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

private:
    struct State : public DBState {
        State(Ip4Address addr) : addr_(addr) { }
        virtual ~State() { }
        Ip4Address addr_;
    };

protected:
    typedef std::map<string, HostInterfaceEntry *> HostInterfaceTable;
    typedef std::set<Ip4Address> LinkLocalAddressTable;

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

protected:
    friend class TestVnswIf;
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);

// Pure firtuals to be implemented by derivative class
    virtual int CreateSocket() = 0;
    virtual void SyncCurrentState() = 0;
    virtual void RegisterAsyncReadHandler() = 0;
    virtual void UpdateLinkLocalRoute(const Ip4Address &addr, bool del_rt) = 0;

    bool ProcessEvent(Event *re);

    void UpdateLinkLocalRouteAndCount(const Ip4Address &addr, bool del_rt);
    void LinkLocalRouteFromLinkLocalEvent(Event *event);
    void LinkLocalRouteFromRouteEvent(Event *event);
    void AddLinkLocalRoutes();
    void DelLinkLocalRoutes();

    void SetSeen(const std::string &name, bool oper);
    void ResetSeen(const std::string &name, bool oper);
    void Activate(const std::string &name, uint32_t os_id);
    void DeActivate(const std::string &name, uint32_t os_id);
    void SetLinkState(const std::string &name, bool link_up);
    bool IsInterfaceActive(const HostInterfaceEntry *entry);
    void HandleInterfaceEvent(const Event *event);

    void SetAddress(const Event *event);
    void ResetAddress(const Event *event);
    void HandleAddressEvent(const Event *event);

    Agent *agent_;
    uint8_t *read_buf_;
    uint8_t tx_buf_[kMaxBufferSize];

    int sock_fd_;
    local::datagram_protocol::socket sock_;
    DBTableBase::ListenerId intf_listener_id_;
    int seqno_;
    bool vhost_intf_up_;

    LinkLocalAddressTable ll_addr_table_;
    HostInterfaceTable host_interface_table_;
    WorkQueue<Event *> *revent_queue_;

    uint32_t vhost_update_count_;
    uint32_t ll_add_count_;
    uint32_t ll_del_count_;

public:
    uint32_t ll_add_count() const { return ll_add_count_; }
    uint32_t ll_del_count() const { return ll_del_count_; }


    DISALLOW_COPY_AND_ASSIGN(VnswInterfaceListenerBase);
};

#if defined(__linux__)
#include <ksync/linux_vnswif_listener.h>
typedef VnswInterfaceListenerLinux VnswInterfaceListener;
#elif defined(__FreeBSD__)
#include <ksync/freebsd_vnswif_listener.h>
typedef VnswInterfaceListenerFreeBSD VnswInterfaceListener;
#else
#error "Unsupported platform"
#endif

#endif
