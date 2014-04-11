/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_router_id_h
#define vnsw_agent_router_id_h

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>

#define XAPI_INTF_PREFIX "xapi"

namespace local = boost::asio::local;

class VnswInterfaceListener {
private:
    static const int kVnswRtmProto = 109;
    static const uint32_t kMaxBufferSize = 4096;

    typedef std::set<Ip4Address> LinkLocalAddressTable;

    struct State : public DBState {
        State(Ip4Address addr) : addr_(addr) { }
        virtual ~State() { }
        Ip4Address addr_;
    };

    struct Event {
        enum Type {
            ADD_REQ,
            DEL_REQ,
            ADD_RESP,
            DEL_RESP,
            INTF_UP,
            INTF_DOWN
        };

        Event(Ip4Address addr, Type event) : addr_(addr), event_(event) { }

        Ip4Address addr_;
        Type event_;
    };

public:
    VnswInterfaceListener(Agent *agent);
    virtual ~VnswInterfaceListener();
    
    void Init();
    void Shutdown();
private:
    void InterfaceNotify(DBTablePartBase *part, DBEntryBase *e);
    void InitRouterId();
    void ReadHandler(const boost::system::error_code &, std::size_t length);
    void CreateVhostRoutes(Ip4Address &host_ip, uint8_t plen);
    void DeleteVhostRoutes(Ip4Address &host_ip, uint8_t plen);
    void RegisterAsyncHandler();
    void CreateSocket();
    uint32_t FetchVhostAddress(bool netmask);
    void InitFetchLinks();
    void InitFetchRoutes();
    void KUpdateLinkLocalRoute(const Ip4Address &addr, bool del_rt);
    bool RouteEventProcess(Event *re);
    void RouteHandler(struct nlmsghdr *nlh);
    void InterfaceHandler(struct nlmsghdr *nlh);
    void IfaddrHandler(struct nlmsghdr *nlh);
    int AddAttr(int type, void *data, int alen);
    int NlMsgDecode(struct nlmsghdr *nl, std::size_t len, uint32_t seq_no);

    Agent *agent_;
    uint8_t *read_buf_;
    uint8_t tx_buf_[kMaxBufferSize];

    int sock_fd_;
    local::datagram_protocol::socket sock_;
    bool ifaddr_listen_;
    DBTableBase::ListenerId intf_listener_id_;
    int seqno_;
    bool vhost_intf_up_;

    LinkLocalAddressTable ll_addr_table_;
    WorkQueue<Event *> *revent_queue_;

    DISALLOW_COPY_AND_ASSIGN(VnswInterfaceListener);
};

#endif
