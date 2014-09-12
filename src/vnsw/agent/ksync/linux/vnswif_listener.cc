/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <ifaddrs.h>

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

extern void RouterIdDepInit(Agent *agent);

VnswInterfaceListenerLinux::VnswInterfaceListenerLinux(Agent *agent) :
    VnswInterfaceListenerBase(agent) {
}

VnswInterfaceListenerLinux::~VnswInterfaceListenerLinux() {
    if (read_buf_){
        delete [] read_buf_;
        read_buf_ = NULL;
    }
    delete revent_queue_;
}

int VnswInterfaceListenerLinux::CreateSocket() {
    int s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);

    if (s < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                "> creating socket");
        assert(0);
    }

    /* Bind to netlink socket */
    struct sockaddr_nl addr;
    memset (&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = (RTMGRP_IPV4_ROUTE | RTMGRP_LINK | RTMGRP_IPV4_IFADDR);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                       "> binding to netlink address family");
        assert(0);
    }

    return s;
}

void VnswInterfaceListenerLinux::SyncCurrentState()
{
    /* Fetch Links from kernel syncronously, to allow dump request for routes
     * to go through fine
     */
    InitNetlinkScan(RTM_GETLINK, ++seqno_);

    /* Fetch routes from kernel asyncronously and update the gateway-id */
    InitNetlinkScan(RTM_GETADDR, ++seqno_);

    /* Fetch routes from kernel asyncronously and update the gateway-id */
    InitNetlinkScan(RTM_GETROUTE, ++seqno_);
}

// Initiate netlink scan based on type and flags
void
VnswInterfaceListenerLinux::InitNetlinkScan(uint32_t type, uint32_t seqno)
{
    struct nlmsghdr *nlh;
    const uint32_t buf_size = VnswInterfaceListenerLinux::kMaxBufferSize;

    memset(tx_buf_, 0, buf_size);
    nlh = (struct nlmsghdr *)tx_buf_;

    /* Fill in the nlmsg header */
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
    nlh->nlmsg_type = type;
    // The message is a request for dump.
    nlh->nlmsg_flags = (NLM_F_DUMP | NLM_F_REQUEST);
    nlh->nlmsg_seq = seqno;

    struct rtgenmsg *rt_gen = (struct rtgenmsg *) NLMSG_DATA (nlh);
    rt_gen->rtgen_family = AF_PACKET;

    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(nlh,nlh->nlmsg_len), 0, ec);
    assert(ec.value() == 0);

    uint8_t read_buf[buf_size];

    /*
     * Wait/Read the response for dump request, linux kernel doesn't handle
     * dump request if response for previous dump request is not complete.
     */
    int end = 0;
    while (end == 0) {
        memset(read_buf, 0, buf_size);
        std::size_t len = sock_.receive(boost::asio::buffer(read_buf,
                                                            buf_size), 0, ec);
        assert(ec.value() == 0);
        struct nlmsghdr *nl = (struct nlmsghdr *)read_buf;
        end = NlMsgDecode(nl, len, seqno);
    }
}


void VnswInterfaceListenerLinux::RegisterAsyncReadHandler() {
    read_buf_ = new uint8_t[kMaxBufferSize];
    sock_.async_receive(boost::asio::buffer(read_buf_, kMaxBufferSize),
        boost::bind(&VnswInterfaceListenerLinux::ReadHandler, this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
}


void
VnswInterfaceListenerLinux::ReadHandler(
    const boost::system::error_code &error, std::size_t len)
{
    struct nlmsghdr *nlh;

    if (error == 0) {
        nlh = (struct nlmsghdr *)read_buf_;
        NlMsgDecode(nlh, len, -1);
    } else {
        LOG(ERROR, "Error < : " << error.message() <<
                   "> reading packet on netlink sock");
    }

    if (read_buf_) {
        delete [] read_buf_;
        read_buf_ = NULL;
    }
    RegisterAsyncReadHandler();
}


/****************************************************************************
 * Link Local route event handler
 ****************************************************************************/
int VnswInterfaceListenerLinux::AddAttr(uint8_t *buff, int type, void *data, int alen) {
    struct nlmsghdr *n = (struct nlmsghdr *)buff;
    int len = RTA_LENGTH(alen);

    if (NLMSG_ALIGN(n->nlmsg_len) + len > VnswInterfaceListenerLinux::kMaxBufferSize)
        return -1;

    struct rtattr *rta = (struct rtattr*)(((char*)n)+NLMSG_ALIGN(n->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = len;
    memcpy(RTA_DATA(rta), data, alen);
    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;
    return 0;
}

void VnswInterfaceListenerLinux::UpdateLinkLocalRoute(const Ip4Address &addr,
                                                 bool del_rt) {
    struct nlmsghdr *nlh;
    struct rtmsg *rtm;
    uint32_t ipaddr;

    memset(tx_buf_, 0, kMaxBufferSize);

    nlh = (struct nlmsghdr *) tx_buf_;
    rtm = (struct rtmsg *) NLMSG_DATA (nlh);

    /* Fill in the nlmsg header*/
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    if (del_rt) {
        nlh->nlmsg_type = RTM_DELROUTE;
        nlh->nlmsg_flags = NLM_F_REQUEST;
    } else {
        nlh->nlmsg_type = RTM_NEWROUTE;
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
    }
    nlh->nlmsg_seq = ++seqno_;
    rtm = (struct rtmsg *) NLMSG_DATA (nlh);
    rtm->rtm_table = RT_TABLE_MAIN;
    rtm->rtm_family = AF_INET;
    rtm->rtm_type = RTN_UNICAST;
    rtm->rtm_protocol = kVnswRtmProto;
    rtm->rtm_scope = RT_SCOPE_LINK;
    rtm->rtm_dst_len = 32;
    ipaddr = RT_TABLE_MAIN;
    AddAttr(tx_buf_, RTA_TABLE, (void *) &ipaddr, 4);
    ipaddr = htonl(addr.to_ulong());
    AddAttr(tx_buf_, RTA_DST, (void *) &ipaddr, 4);
    int if_index = if_nametoindex(agent_->vhost_interface_name().c_str());
    AddAttr(tx_buf_, RTA_OIF, (void *) &if_index, 4);

    boost::system::error_code ec;
    sock_.send(boost::asio::buffer(nlh,nlh->nlmsg_len), 0, ec);
    assert(ec.value() == 0);
}

/****************************************************************************
 * Netlink message handlers
 * Decodes netlink messages and enqueues events to revent_queue_
 ****************************************************************************/
string VnswInterfaceListenerLinux::NetlinkTypeToString(uint32_t type) {
    switch (type) {
    case NLMSG_DONE:
        return "NLMSG_DONE";
    case RTM_NEWADDR:
        return "RTM_NEWADDR";
    case RTM_DELADDR:
        return "RTM_DELADDR";
    case RTM_NEWROUTE:
        return "RTM_NEWROUTE";
    case RTM_DELROUTE:
        return "RTM_DELROUTE";
    case RTM_NEWLINK:
        return "RTM_NEWLINK";
    case RTM_DELLINK:
        return "RTM_DELLINK";
    default:
        break;
    }
    std::stringstream str;
    str << "UNHANDLED <" << type << ">";
    return str.str();
}

VnswInterfaceListenerBase::Event *
VnswInterfaceListenerLinux::HandleNetlinkRouteMsg(struct nlmsghdr *nlh)
{
    struct rtmsg *rtm = (struct rtmsg *) NLMSG_DATA (nlh);

    if (rtm->rtm_family != AF_INET || rtm->rtm_table != RT_TABLE_MAIN
        || rtm->rtm_type != RTN_UNICAST || rtm->rtm_scope != RT_SCOPE_LINK) {
        LOG(DEBUG, "Ignoring Netlink route with family "
            << (uint32_t)rtm->rtm_family
            << " table " << (uint32_t)rtm->rtm_table
            << " type " << (uint32_t)rtm->rtm_type
            << " scope " << (uint32_t)rtm->rtm_family);
        return NULL;
    }

    int oif = -1;
    uint32_t dst_ip = 0;
    uint32_t gw_ip = 0;

    /* Get the route atttibutes len */
    int rtl = RTM_PAYLOAD(nlh);

    /* Loop through all attributes */
    for (struct rtattr *rth = (struct rtattr *) RTM_RTA(rtm);
         RTA_OK(rth, rtl); rth = RTA_NEXT(rth, rtl)) {
        /* Get the gateway (Next hop) */
        if (rth->rta_type == RTA_DST) {
            dst_ip = *((int *)RTA_DATA(rth));
        }
        if (rth->rta_type == RTA_GATEWAY) {
             gw_ip = *((int *)RTA_DATA(rth));
        }
        if (rth->rta_type == RTA_OIF) {
             oif = *((int *)RTA_DATA(rth));
        }
    }

    if (oif == -1) {
        return NULL;
    }

    char name[IFNAMSIZ];
    if_indextoname(oif, name);
    Ip4Address dst_addr((unsigned long)ntohl(dst_ip));
    Ip4Address gw_addr((unsigned long)ntohl(gw_ip));
    LOG(DEBUG, "Handle netlink route message "
        << NetlinkTypeToString(nlh->nlmsg_type)
        << " : " << dst_addr.to_string() << "/" << rtm->rtm_dst_len
        << " Interface " << name << " GW " << gw_addr.to_string());

    Event::Type type;
    if (nlh->nlmsg_type == RTM_DELROUTE) {
        type = Event::DEL_ROUTE;
    } else {
        type = Event::ADD_ROUTE;
    }

    return new Event(type, dst_addr, rtm->rtm_dst_len, name, gw_addr,
                     rtm->rtm_protocol, rtm->rtm_flags);
}

VnswInterfaceListenerBase::Event *
VnswInterfaceListenerLinux::HandleNetlinkIntfMsg(struct nlmsghdr *nlh){
    /* Get the atttibutes len */
    int rtl = RTM_PAYLOAD(nlh);

    const char *port_name = NULL;
    struct ifinfomsg *ifi = (struct ifinfomsg *) NLMSG_DATA (nlh);
    /* Loop through all attributes */
    for (struct rtattr *rth = IFLA_RTA(ifi); RTA_OK(rth, rtl);
         rth = RTA_NEXT(rth, rtl)) {
        /* Get the interface name */
        if (rth->rta_type == IFLA_IFNAME) {
            port_name = (char *) RTA_DATA(rth);
        }
    }

    assert(port_name != NULL);
    LOG(DEBUG, "Handle netlink interface message "
        << NetlinkTypeToString(nlh->nlmsg_type)
        << " for interface " << port_name);

    Event::Type type;
    if (nlh->nlmsg_type == RTM_DELLINK) {
        type = Event::DEL_INTERFACE;
    } else {
        type = Event::ADD_INTERFACE;
    }
    return new Event(type, port_name, ifi->ifi_flags);
}

VnswInterfaceListenerBase::Event *
VnswInterfaceListenerLinux::HandleNetlinkAddrMsg(struct nlmsghdr *nlh){
    struct ifaddrmsg *ifa = (struct ifaddrmsg *) NLMSG_DATA (nlh);

    // Get interface name from os-index
    char name[IFNAMSIZ];
    if_indextoname(ifa->ifa_index, name);

    LOG(DEBUG, "Handle netlink address message "
        << NetlinkTypeToString(nlh->nlmsg_type) << " for interface " << name);

    uint32_t ipaddr = 0;
    int rtl = IFA_PAYLOAD(nlh);
    for (struct rtattr *rth = IFA_RTA(ifa); rtl && RTA_OK(rth, rtl);
         rth = RTA_NEXT(rth,rtl)) {
        if (rth->rta_type != IFA_LOCAL) {
            continue;
        }

        ipaddr = ntohl(* ((uint32_t *)RTA_DATA(rth)));
    }

    if (ipaddr == 0)
        return NULL;

    assert(ipaddr != 0);
    Event::Type type;
    if (nlh->nlmsg_type == RTM_DELADDR) {
        type = Event::DEL_ADDR;
    } else {
        type = Event::ADD_ADDR;
    }
    return new Event(type, name, Ip4Address(ipaddr),
                                            ifa->ifa_prefixlen, ifa->ifa_flags);
}

int
VnswInterfaceListenerLinux::NlMsgDecode(struct nlmsghdr *nl,
    std::size_t len, uint32_t seq_no)
{
    Event *event = NULL;
    struct nlmsghdr *nlh = nl;
    for (; (NLMSG_OK(nlh, len)); nlh = NLMSG_NEXT(nlh, len)) {

        switch (nlh->nlmsg_type) {
        case NLMSG_DONE:
            if (nlh->nlmsg_seq == seq_no) {
                return 1;
            }
            return 0;
        case RTM_NEWADDR:
        case RTM_DELADDR:
            event = HandleNetlinkAddrMsg(nlh);
            break;
        case RTM_NEWROUTE:
        case RTM_DELROUTE:
            event = HandleNetlinkRouteMsg(nlh);
            break;
        case RTM_NEWLINK:
        case RTM_DELLINK:
            event = HandleNetlinkIntfMsg(nlh);
            break;
        default:
            LOG(DEBUG, "VnswInterfaceListenerLinux got message : "
                << NetlinkTypeToString(nlh->nlmsg_type));
            break;
        }

        if (event) {
            revent_queue_->Enqueue(event);
        }
    }

    return 0;
}
