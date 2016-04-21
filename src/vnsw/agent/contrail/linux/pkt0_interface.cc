/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
 
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>

#include "base/logging.h"
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "pkt/pkt_types.h"
#include "pkt/pkt_init.h"
#include "../pkt0_interface.h"

#define TUN_INTF_CLONE_DEV "/dev/net/tun"

#define TAP_TRACE(obj, ...)                                              \
do {                                                                     \
    Tap##obj::TraceMsg(PacketTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while (false)                                                          \

///////////////////////////////////////////////////////////////////////////////


void Pkt0Interface::InitControlInterface() {
    pkt_handler()->agent()->set_pkt_interface_name(name_);

    if ((tap_fd_ = open(TUN_INTF_CLONE_DEV, O_RDWR)) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> opening tap-device");
        assert(0);
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    if (ioctl(tap_fd_, TUNSETIFF, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> creating " << name_ << "tap-device");
        assert(0);
    }

    // We dont want the fd to be inherited by child process such as
    // virsh etc... So, close tap fd on fork.
    if (fcntl(tap_fd_, F_SETFD, FD_CLOEXEC) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> setting fcntl on " << name_ );
        assert(0);
    }

    if (ioctl(tap_fd_, TUNSETPERSIST, 0) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> making tap interface non-persistent");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    if (ioctl(tap_fd_, SIOCGIFHWADDR, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " << strerror(errno) <<
            "> retrieving MAC address of the tap interface");
        assert(0);
    }
    memcpy(mac_address_, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);

    int raw;
    if ((raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> creating socket");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.data(), IF_NAMESIZE);
    if (ioctl(raw, SIOCGIFINDEX, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> getting ifindex of the tap interface");
        assert(0);
    }

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(struct sockaddr_ll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(raw, (struct sockaddr *)&sll,
             sizeof(struct sockaddr_ll)) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> binding the socket to the tap interface");
        assert(0);
    }

    // Set tx-buffer count
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    if (ioctl(raw, SIOCGIFTXQLEN, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " << strerror(errno) <<
            "> getting tx-buffer size");
        assert(0);
    }
    
    uint32_t qlen = pkt_handler()->agent()->params()->pkt0_tx_buffer_count();
    if (ifr.ifr_qlen < (int)qlen) {
        ifr.ifr_qlen = qlen;
        if (ioctl(raw, SIOCSIFTXQLEN, (void *)&ifr) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": "
                << strerror(errno) << "> setting tx-buffer size");
            assert(0);
        }
    }
    
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.data(), IF_NAMESIZE);
    if (ioctl(raw, SIOCGIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> getting socket flags");
        assert(0);
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(raw, SIOCSIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> setting socket flags");
        assert(0);
    }
    close(raw);

    boost::system::error_code ec;
    input_.assign(tap_fd_, ec);
    assert(ec == 0);

    VrouterControlInterface::InitControlInterface();
    AsyncRead();
}

void Pkt0RawInterface::InitControlInterface() {
    pkt_handler()->agent()->set_pkt_interface_name(name_);

    int raw_;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    if ((raw_ = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) << "> creating socket");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name,
            pkt_handler()->agent()->pkt_interface_name().c_str(), IF_NAMESIZE);
    if (ioctl(raw_, SIOCGIFINDEX, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) << "> getting ifindex of the " <<
                "expception packet interface");
        assert(0);
    }

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(struct sockaddr_ll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(raw_, (struct sockaddr *)&sll,
                sizeof(struct sockaddr_ll)) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) << "> binding the socket to the tap interface");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.data(), IF_NAMESIZE);
    if (ioctl(raw_, SIOCGIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) << "> getting socket flags");
        assert(0);
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(raw_, SIOCSIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) << "> setting socket flags");
        assert(0);
    }
    tap_fd_ = raw_;

    boost::system::error_code ec;
    input_.assign(tap_fd_, ec);
    assert(ec == 0);

    VrouterControlInterface::InitControlInterface();
    AsyncRead();
}
