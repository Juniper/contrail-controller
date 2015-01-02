/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef freebsd_vnsw_agent_router_id_h
#define freebsd_vnsw_agent_router_id_h

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include "ksync/vnswif_listener_base.h"

class VnswInterfaceListenerFreeBSD : public VnswInterfaceListenerBase {
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

    const std::string RTMTypeToString(int type);
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

typedef VnswInterfaceListenerFreeBSD VnswInterfaceListener;

#endif
