/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef linux_vnsw_agent_router_id_h
#define linux_vnsw_agent_router_id_h

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

class VnswInterfaceListenerLinux : public VnswInterfaceListenerBase 
{
public:
    VnswInterfaceListenerLinux(Agent *agent);
    virtual ~VnswInterfaceListenerLinux();
    
private:
    friend class TestVnswIf;

    int CreateSocket();
    virtual void SyncCurrentState();
    virtual void RegisterAsyncReadHandler();
    void ReadHandler(const boost::system::error_code &, std::size_t length);
    void UpdateLinkLocalRoute(const Ip4Address &addr, bool del_rt);

private:
    void InitNetlinkScan(uint32_t type, uint32_t seqno);
    int NlMsgDecode(struct nlmsghdr *nl, std::size_t len, uint32_t seq_no);
    bool ProcessEvent(Event *re);

    int AddAttr(uint8_t *, int , void *, int );
    string NetlinkTypeToString(uint32_t);
    Event *HandleNetlinkRouteMsg(struct nlmsghdr *);
    Event *HandleNetlinkIntfMsg(struct nlmsghdr *);
    Event *HandleNetlinkAddrMsg(struct nlmsghdr *);

    DISALLOW_COPY_AND_ASSIGN(VnswInterfaceListenerLinux);
};

#endif
