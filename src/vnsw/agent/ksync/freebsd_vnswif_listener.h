/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef freensd_vnsw_agent_router_id_h
#define freebsd_vnsw_agent_router_id_h

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

class VnswInterfaceListenerFreeBSD : public VnswInterfaceListenerBase
{
public:
    VnswInterfaceListenerFreeBSD(Agent *agent);
    virtual ~VnswInterfaceListenerFreeBSD();

private:
    friend class TestVnswIf;

    virtual int CreateSocket();
    virtual void SyncCurrentState();
    virtual void RegisterAsyncReadHandler();
    void ReadHandler(const boost::system::error_code &, std::size_t length);
    virtual void UpdateLinkLocalRoute(const Ip4Address &addr, bool del_rt);

    const string RTMTypeToString(int type);
    unsigned int
    RTMGetAddresses(const char *in, size_t *size, unsigned int af,
        struct rt_addresses *rta);
    Event *
    RTMProcess(const struct rt_msghdr *rtm, size_t size);
    Event *
    RTMProcess(const struct ifa_msghdr *rtm, size_t size);
    Event *
    RTMProcess(const struct if_msghdr *rtm, size_t size);
    int RTMDecode(const struct rt_msghdr_common *rtm, size_t len,
		uint32_t seq_no);
    int RTMProcessBuffer(const void *buffer, size_t size);
    int RTCreateSocket(int fib);
    int RTInitRoutes(int fib);
    int RTInitIfAndAddr();
    int Getfib();
    void *SysctlDump(int *mib, int mib_len, size_t *ret_len, int *ret_code);
    int NetmaskLen(int mask);

    int pid_;
    int fib_;

    DISALLOW_COPY_AND_ASSIGN(VnswInterfaceListenerFreeBSD);
};

#endif
