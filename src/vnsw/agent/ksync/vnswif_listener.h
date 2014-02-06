/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_router_id_h
#define vnsw_agent_router_id_h

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>

#define XAPI_INTF_PREFIX "xapi"

namespace local = boost::asio::local;

class VnswRouteEvent {
public:
    enum Event {
        ADD_REQ,
        DEL_REQ,
        ADD_RESP,
        DEL_RESP,
        INTF_UP,
        INTF_DOWN
    };

    VnswRouteEvent(Ip4Address addr, Event event) 
        : addr_(addr), event_(event) {}

    Ip4Address addr_;
    Event event_;
};

class VnswInterfaceListener {
public:
    static const int kVnswRtmProto = 109;
    enum { max_buf_size = 4096 };

    struct VnswInterface {
        string name_;
        Ip4Address address_;
        uint8_t prefix_len_;
        Ip4Address gw_address_;
        VnswInterface(const std::string &name) : name_(name) {
        }
        VnswInterface(const std::string &name, const Ip4Address &ip, 
                      uint8_t plen) : name_(name), address_(ip), 
                                      prefix_len_(plen), gw_address_(0) {
        }
    };
    typedef boost::shared_ptr<VnswInterface> VnswInterfacePtr;

    class VnswInterfaceCmp {
        public:
            bool operator()(const VnswInterfacePtr &lhs, 
                            const VnswInterfacePtr &rhs) const {
                return lhs.get()->name_.compare(rhs.get()->name_);
            }
    };
    typedef std::set<VnswInterfacePtr, VnswInterfaceCmp> InterfaceSet;


    VnswInterfaceListener(Agent *agent);
    virtual ~VnswInterfaceListener();
    
    void Init();
    void Shutdown();
private:
    typedef std::set<Ip4Address> Ip4HostTableType;
    class VnswIntfState : public DBState {
    public:
        VnswIntfState(Ip4Address addr) : addr_(addr) { }
        virtual ~VnswIntfState() { }
        Ip4Address addr() { return addr_; }
    private:
        Ip4Address addr_;
    };

    void IntfNotify(DBTablePartBase *part, DBEntryBase *e);
    void InitRouterId();
    void ReadHandler(const boost::system::error_code &, std::size_t length);
    void CreateVhostRoutes(const std::string &name, Ip4Address &host_ip, 
                           uint8_t plen);
    void DeleteVhostRoutes(const std::string &name, Ip4Address &host_ip, 
                           uint8_t plen);
    void SetupSocket();
    void RegisterAsyncHandler();
    void CreateSocket();
    uint32_t NetmaskToPrefix(uint32_t netmask);
    uint32_t FetchVhostAddress(bool netmask);
    void InitFetchLinks();
    void InitFetchRoutes();
    void KUpdateLinkLocalRoute(const Ip4Address &addr, bool del_rt);
    bool RouteEventProcess(VnswRouteEvent *re);
    void RouteHandler(struct nlmsghdr *nlh);
    void InterfaceHandler(struct nlmsghdr *nlh);
    void IfaddrHandler(struct nlmsghdr *nlh);
    int AddAttr(int type, void *data, int alen);
    int NlMsgDecode(struct nlmsghdr *nl, std::size_t len, uint32_t seq_no);
    VnswInterface *GetVnswInterface(const std::string& name);

    Agent *agent_;
    uint8_t *read_buf_;
    uint8_t tx_buf_[max_buf_size];

    int sock_fd_;
    local::datagram_protocol::socket sock_;
    bool ifaddr_listen_;
    DBTableBase::ListenerId intf_listener_id_;
    Ip4HostTableType    ll_addr_table_;
    WorkQueue<VnswRouteEvent *> *revent_queue_;
    int seqno_;
    bool vhost_intf_up_;
    InterfaceSet interface_table_;
};

#endif
