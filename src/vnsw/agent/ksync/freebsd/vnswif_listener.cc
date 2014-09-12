/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <net/route.h>
/* FreeBSD #defines Free macro in net/route that brakes compilation. */
#undef Free
#include <net/if_dl.h>
#include <ifaddrs.h>
#include <strings.h>

#include <base/logging.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>
#include <cmn/agent_param.h>
#include <cfg/cfg_init.h>
#include <oper/route_common.h>
#include <oper/mirror_table.h>
#include <ksync/ksync_index.h>
#include <ksync/interface_ksync.h>
#include "vnswif_listener.h"

/* Pointers are ordered in the same way as address flags (RTA_*)
 * from route.h */
struct rt_addresses {
    struct sockaddr *dst;
    struct sockaddr *gw;
    struct sockaddr *netmask;
    struct sockaddr *genmask;
    struct sockaddr *ifp;
    struct sockaddr *ifa;
    struct sockaddr *author;
    struct sockaddr *brd;
};

struct rt_msghdr_common {
    u_short rtmc_msglen;
    u_char  rtmc_version;
    u_char  rtmc_type;
};

#define RTM_ADDR_MAX ((int)(sizeof(struct rt_addresses)/ \
                      sizeof(struct sockaddr *)))

int VnswInterfaceListenerFreeBSD::NetmaskLen(int mask)
{
    if (mask == 0)
        return 0;

    return 33 - ffs(mask);
}

unsigned int
VnswInterfaceListenerFreeBSD::RTMGetAddresses(
    const char *in, size_t *size, unsigned int af,
    struct rt_addresses *rta)
{
    struct sockaddr **out = (struct sockaddr **)rta;
    int i = 0;
    size_t wsize = *size;
    unsigned int oaf = af;

    if (!(af & ((unsigned int)-1 >>  (32 - RTM_ADDR_MAX))))
        return (af & (unsigned int)-1 << RTM_ADDR_MAX);

    while (i < (int)RTM_ADDR_MAX && af != 0 && wsize != 0) {
        if (wsize < SA_SIZE(in)) {
            break;
        }

        if (af & 1) {
            *out = (struct sockaddr *)in;
            wsize -= SA_SIZE(in);
            in += SA_SIZE(in);
        }
        i++;
        out++;
        af >>= 1;
    }
    oaf &= (unsigned int)-1 << i;

    *size -= wsize;

    return oaf;
}


const std::string
VnswInterfaceListenerFreeBSD::RTMTypeToString(int type)
{
    static const char *types[] = {
        "RTM_ADD",
        "RTM_DELETE",
        "RTM_CHANGE",
        "RTM_GET",
        "RTM_LOSING",
        "RTM_REDIRECT",
        "RTM_MISS",
        "RTM_LOCK",
        "RTM_OLDADD",
        "RTM_OLDDEL",
        "RTM_RESOLVE",
        "RTM_NEWADDR",
        "RTM_DELADDR",
        "RTM_IFINFO",
        "RTM_NEWMADDR",
        "RTM_DELMADDR",
        "RTM_IFANNOUNCE",
        "RTM_IEEE80211"
    };

    if (type > 0 && (unsigned long)type < sizeof(types)/sizeof(types[0]))
        return types[type-1];

    std::stringstream str;
    str << "UNHANDLED <" << type << ">";
    return str.str();
}

VnswInterfaceListener::Event *
VnswInterfaceListenerFreeBSD::RTMProcess(const struct rt_msghdr *rtm,
    size_t size)
{
    struct rt_addresses rta = { 0 };
    size_t s = size - sizeof(*rtm);
    char name[IFNAMSIZ] = { 0 };
    Event::Type type;
    unsigned int mask_len = 0;
    struct sockaddr_in *t;

    /* This actually is an error */
    if (sizeof(*rtm) > size) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message too short");
        return NULL;
    }

    /* Not interested in self generated messages */
    if (rtm->rtm_pid == pid_)
        return NULL;

    /* Special case: RTM_DELETE may come without gateway and interface
       index. Do not serve that, event requires if name na gateway
       that will not be provided */
    if (rtm->rtm_type == RTM_DELETE &&
        !(rtm->rtm_addrs & RTA_GATEWAY))
        return NULL;

    if (RTMGetAddresses((char *)(rtm + 1) , &s,
        rtm->rtm_addrs, &rta) != 0) {
        LOG(ERROR, __PRETTY_FUNCTION__
            << RTMTypeToString(rtm->rtm_type)
            << "IP address extraction failed.");
        return NULL;
    }

    if (rtm->rtm_addrs & RTA_NETMASK)
    {
        mask_len = NetmaskLen(
            ntohl(((struct sockaddr_in *)rta.netmask)->sin_addr.s_addr)
        );
    }

    /* Only routes to host are supported, which means routes that
       either have RTF_HOST flags or netmask narrowed to one host
       (unicast, all 1s) */
    if (!(rtm->rtm_flags & RTF_HOST || mask_len == 32))
    {
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": "
            << RTMTypeToString(rtm->rtm_type)
            << " misses requirements for processing");

        return NULL;
    }

    /* There might be no interface name within address block, but there
       is always interface index that can be used to get the name */
    if (rtm->rtm_addrs & RTA_IFP && rta.ifp != NULL) {
        struct sockaddr_dl *sd = (struct sockaddr_dl *)rta.ifp;
        strncpy(name, sd->sdl_data, sd->sdl_nlen);
    } else {
        if_indextoname(rtm->rtm_index, name);
    }

    t = (struct sockaddr_in *)rta.dst;
    Ip4Address dst_addr((unsigned long)ntohl(t->sin_addr.s_addr));
    t = (struct sockaddr_in *)rta.gw;
    Ip4Address gw_addr((unsigned long)ntohl(t->sin_addr.s_addr));

    if (rtm->rtm_type == RTM_DELETE) {
        type = Event::DEL_ROUTE;
    } else {
        /* Handles also RTM_GET message, which is similar in structure
           but is used for reporting current state */
        type = Event::ADD_ROUTE;
    }

    /* "Protocol" the route has been set with */
    char proto = rtm->rtm_flags & (RTF_STATIC | RTF_DYNAMIC);

    LOG(DEBUG, __PRETTY_FUNCTION__ << ": "
        << RTMTypeToString(rtm->rtm_type)
        << " : " << dst_addr.to_string() << "/" << mask_len
        << " Interface " << name << " GW " << gw_addr.to_string());

    return new Event(type, dst_addr, mask_len, name, gw_addr,
                     proto, rtm->rtm_flags);
}

VnswInterfaceListener::Event *
VnswInterfaceListenerFreeBSD::RTMProcess(const struct ifa_msghdr *rtm,
    size_t size)
{
    struct rt_addresses rta = { 0 };
    size_t s = size;
    char name[IFNAMSIZ] = { 0 };
    struct sockaddr_in *t;
    Event::Type type;

    /* This actually is an error */
    if (sizeof(*rtm) > size) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message too short");
        return NULL;
    }

    if (RTMGetAddresses((char *)(rtm + 1) , &s,
        rtm->ifam_addrs, &rta) != 0) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message "
            << RTMTypeToString(rtm->ifam_type)
            << "IP address extraction failed.");
        return NULL;
    }

    assert(rta.ifa != NULL);

    if (rtm->ifam_addrs & RTA_IFP && rta.ifp != NULL) {
        struct sockaddr_dl *sd = (struct sockaddr_dl *)rta.ifp;
        strncpy(name, sd->sdl_data, sd->sdl_nlen);
    } else {
        if_indextoname(rtm->ifam_index, name);
    }

    if (rtm->ifam_type == RTM_DELADDR) {
        type = Event::DEL_ADDR;
    } else {
        /* RTM_NEWADDR is only other command */
        type = Event::ADD_ADDR;
    }

    t = (struct sockaddr_in *)rta.ifa;
    Ip4Address addr((unsigned long)ntohl(t->sin_addr.s_addr));

    LOG(DEBUG, __PRETTY_FUNCTION__ << ": message "
        << RTMTypeToString(rtm->ifam_type)
        << " Interface " << name << " IP " << addr.to_string());

    return new Event(type, name, addr);
}

VnswInterfaceListener::Event *
VnswInterfaceListenerFreeBSD::RTMProcess(
    const struct if_msghdr *rtm, size_t size)
{
    struct rt_addresses rta = { 0 };
    size_t s = size;
    char name[IFNAMSIZ] = { 0 };
    Event::Type type;

    /* This actually is an error */
    if (sizeof(*rtm) > size) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message too short");
        return NULL;
    }

    if (RTMGetAddresses((char *)(rtm + 1) , &s,
        rtm->ifm_addrs, &rta) != 0) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": message "
            << RTMTypeToString(rtm->ifm_type)
            << "IP address extraction failed.");
        return NULL;
    }

    if (rtm->ifm_addrs & RTA_IFP && rta.ifp != NULL) {
        struct sockaddr_dl *sd = (struct sockaddr_dl *)rta.ifp;
        strncpy(name, sd->sdl_data, sd->sdl_nlen);
    } else {
        if_indextoname(rtm->ifm_index, name);
    }

    if (rtm->ifm_flags & RTF_UP) {
        type = Event::ADD_INTERFACE;
    } else {
        type = Event::DEL_INTERFACE;
    }

    LOG(DEBUG, __PRETTY_FUNCTION__ << ": message "
        << RTMTypeToString(rtm->ifm_type)
        << " Interface " << name);

    return new Event(type, name, rtm->ifm_flags);
}

int VnswInterfaceListenerFreeBSD::RTMDecode(
    const struct rt_msghdr_common *rtm, size_t len, uint32_t seq_no)
{
    Event *event = NULL;
    /* Segfault protection */
    if (len < sizeof(*rtm)) {
        return 1;
    }

    switch (rtm->rtmc_type) {
    case RTM_GET:
    case RTM_ADD:
    case RTM_DELETE:
        event = RTMProcess((const struct rt_msghdr *)rtm, len);
        break;
    case RTM_NEWADDR:
    case RTM_DELADDR:
        event = RTMProcess((const struct ifa_msghdr *)rtm, len);
        break;
    case RTM_IFINFO:
        event = RTMProcess((const struct if_msghdr *)rtm, len);
        break;
    default:
        LOG(DEBUG, __PRETTY_FUNCTION__ << ": message "
                << RTMTypeToString(rtm->rtmc_type));
        break;
    }

    if (event != NULL) {
        revent_queue_->Enqueue(event);
    }
    return 0;
}

int VnswInterfaceListenerFreeBSD::RTMProcessBuffer(const void *buffer,
    size_t size)
{
    struct rt_msghdr_common *p = (struct rt_msghdr_common *)buffer;
    int ret = 0;

    while (size != 0 && size >= p->rtmc_msglen && p->rtmc_msglen > 0) {
        ret = RTMDecode(p, size, 0);

        if (ret)
            break;

        size -= p->rtmc_msglen;
        p = (struct rt_msghdr_common *)((char *)p + p->rtmc_msglen);
    }

    /* This is not an error since size of buffer, instead of size of
       all messages in that buffer could be given. */
    if (size > 0)
        LOG(DEBUG, __PRETTY_FUNCTION__
            << ": message data left unprocessed(" << size << " bytes)");

    return ret;
}

int VnswInterfaceListenerFreeBSD::Getfib()
{
    int fibnum;
    size_t size = sizeof(fibnum);

    if (fib_ != -1)
        return fib_;

    if (sysctlbyname("net.fibs", &fibnum, &size, NULL, 0) != 0)
        return -1;

    size = sizeof(fibnum);

    if (fibnum >= 0 &&
        sysctlbyname("net.my_fibnum", &fibnum, &size, NULL, 0) != -1)
    {
        fib_ = fibnum;
        return fibnum;
    }
    return -2;
}

int VnswInterfaceListenerFreeBSD::CreateSocket()
{
    /* Get routing table */
    int fib = Getfib();
    int s = -1;

    if (fib < 0) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": Failed to get fib. "
            << "Expected >= 0, obtained " << fib);
        assert(fib >= 0);
    }

    /* Interested in link layer and inet messages, so can not
       specify one family. Filtering will have to be done by
       hand. */
    s = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);

    if (s == -1) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": failed to create "
            << "socket with error: " << errno);
        assert(0);
    }

    if (setsockopt(s, SOL_SOCKET, SO_SETFIB, (void *)&fib,
        sizeof(fib)) == -1)
    {
        close(s);
        LOG(ERROR, __PRETTY_FUNCTION__ << " failed to setup "
            << "socket with error: " << errno);
        assert(0);
    }

    return s;
}

void VnswInterfaceListenerFreeBSD::SyncCurrentState()
{
    /* Get current system settings */
    RTInitIfAndAddr();

    RTInitRoutes(fib_);
}

void *VnswInterfaceListenerFreeBSD::SysctlDump(int *mib, int mib_len,
    size_t *ret_len, int *ret_code)
{
    size_t size = 0;
    void *dump_buf = NULL;
    int ret = 0;

    if (sysctl(mib, mib_len, NULL, &size, NULL, 0)) {
        ret = 1;
        goto exit_here;
    }

    dump_buf = calloc(size, 1);
    if (dump_buf == NULL) {
        ret = 2;
        goto exit_here;
    }

    if (sysctl(mib, mib_len, dump_buf, &size, NULL, 0)) {
        ret = 3;
        free(dump_buf);
        size = 0;
        dump_buf = NULL;
        goto exit_here;
    }

exit_here:
    *ret_code = ret;
    *ret_len = size;
    return dump_buf;
}

int VnswInterfaceListenerFreeBSD::RTInitRoutes(int fib)
{
    int mib[] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0, fib};
    int mib_len = sizeof(mib)/sizeof(int);
    size_t size;
    int ret;
    void *p;

    /* If fib is -1 then we will force default fib, this is equivalent
       to providing mib with five ints only (as 6th is None and
       7th is fib number - or 0 if not provided). See man 3 sysctl,
       PF_ROUTE. */
    if (fib < 0)
        mib_len -= 2;

    p = SysctlDump(mib, mib_len, &size, &ret);

    if (p != NULL) {
        ret = RTMProcessBuffer(p, size);
        free(p);
    }

    return ret;
}

int VnswInterfaceListenerFreeBSD::RTInitIfAndAddr()
{
    int mib[] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_IFLIST, 0};
    int mib_len = sizeof(mib)/sizeof(int);
    size_t size;
    int ret;
    void *p;

    p = SysctlDump(mib, mib_len, &size, &ret);

    if (p != NULL) {
        ret = RTMProcessBuffer(p, size);
        free(p);
    }

    return ret;
}

VnswInterfaceListenerFreeBSD::VnswInterfaceListenerFreeBSD(Agent *agent) :
    VnswInterfaceListenerBase(agent), fib_(-1) {
}

VnswInterfaceListenerFreeBSD::~VnswInterfaceListenerFreeBSD() {
}

void VnswInterfaceListenerFreeBSD::ReadHandler(const boost::system::error_code &error,
                                 std::size_t len) {
    if (error == 0) {
        RTMDecode((struct rt_msghdr_common *)read_buf_, len, -1);
    } else {
        LOG(ERROR, "Error < : " << error.message() <<
                   "> reading packet on PF_ROUTE sock");
    }

    if (read_buf_) {
        delete [] read_buf_;
        read_buf_ = NULL;
    }
    RegisterAsyncReadHandler();
}

void VnswInterfaceListenerFreeBSD::RegisterAsyncReadHandler() {
    read_buf_ = new uint8_t[kMaxBufferSize];
    sock_.async_receive(boost::asio::buffer(read_buf_, kMaxBufferSize),
        boost::bind(&VnswInterfaceListenerFreeBSD::ReadHandler, this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
}

void VnswInterfaceListenerFreeBSD::UpdateLinkLocalRoute(
    const Ip4Address &addr, bool del_rt) {

    if (agent_->test_mode())
        return;

    memset(tx_buf_, 0, kMaxBufferSize);

    size_t size;
    struct rt_msghdr *rtm = (struct rt_msghdr *)tx_buf_;
    struct sockaddr_in *si = (struct sockaddr_in *)(rtm + 1);
    struct sockaddr *gw = NULL;
    struct ifaddrs *ifap, *oifap;
    const char *vhost_name = agent_->vhost_interface_name().c_str();

    /* Need to get the gateway IP, the interface is not enough */
    if (getifaddrs(&ifap) == -1) {
        /* We will not be able to set route if we are not able to
           set gateway. That's some major failure */
        LOG(ERROR, __PRETTY_FUNCTION__
            << ": failed to get list of interfaces");
            assert(0);
    }

    oifap = ifap;

    while (ifap != NULL) {
        if (ifap->ifa_name != NULL &&
            !strncmp(ifap->ifa_name, vhost_name, IFNAMSIZ) &&
            SA_SIZE(ifap->ifa_addr) >= SA_SIZE(NULL) &&
            ifap->ifa_addr->sa_family == AF_INET)
        {
            gw = ifap->ifa_addr;
        }

        ifap = ifap->ifa_next;
    }

    if (gw == NULL) {
        /* We will not be able to set route if we are not able to
           set gateway. That's some major failure */
        LOG(ERROR, __PRETTY_FUNCTION__ << ": failed to get IP for "
            << string(vhost_name));
            assert(gw != NULL);
    }


    /* Destination, Gateway and header */
    size = 2 * sizeof(struct sockaddr_in) + sizeof(struct rt_msghdr);

    if (size > kMaxBufferSize) {
        LOG(ERROR, __PRETTY_FUNCTION__
             << ": tx_buf_ to short, expected size " << size);
        assert(size < kMaxBufferSize);
    }

    rtm->rtm_msglen = size;
    rtm->rtm_version = RTM_VERSION;

    if (!del_rt)
        rtm->rtm_type = RTM_ADD;
    else
        rtm->rtm_type = RTM_DELETE;

    rtm->rtm_flags = RTF_UP|RTF_GATEWAY|RTF_STATIC|RTF_HOST;

    rtm->rtm_index = if_nametoindex(vhost_name);

    if (rtm->rtm_index == 0) {
        LOG(ERROR, __PRETTY_FUNCTION__ << ": Failed to obtain index "
            << "for interface " << vhost_name);
        assert(0);
    }

    rtm->rtm_addrs = RTA_DST|RTA_GATEWAY;
    rtm->rtm_pid = pid_;
    rtm->rtm_seq = seqno_++;

    /* Destination */
    si->sin_len = sizeof(struct sockaddr_in);
    si->sin_family = AF_INET;
    si->sin_port = 0;
    si->sin_addr.s_addr = htonl(addr.to_ulong());

    /* Gateway */
    si = (struct sockaddr_in *)((char *)si + sizeof(struct sockaddr_in));
    /* Very unlikely... but hard to detect if happens */
    if (sizeof(struct sockaddr_in) < SA_SIZE(gw)) {
        LOG(ERROR, __PRETTY_FUNCTION__
            << ": Size of gateway address returned by kernel is "
            << SA_SIZE(gw) << "which far too big for socaddr_in struct");
        assert(sizeof(struct sockaddr_in) >= SA_SIZE(gw));
    }
    memcpy(si, gw, SA_SIZE(gw));

    freeifaddrs(oifap);

    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(tx_buf_, size), 0, ec);
    if (ec.value() != 0) {
        LOG(ERROR, __PRETTY_FUNCTION__
            << ": PF_ROUTE message " << RTMTypeToString(rtm->rtm_type)
            << " send failed");
        assert(ec.value() == 0);
    }
}

