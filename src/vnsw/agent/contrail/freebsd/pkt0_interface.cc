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
#include <sys/sockio.h>
#include <ifaddrs.h>
#include <net/ethernet.h>

#include "base/logging.h"
#include "cmn/agent_cmn.h"
#include "sandesh/sandesh_trace.h"
#include "pkt/pkt_types.h"
#include "pkt/pkt_init.h"
#include "../pkt0_interface.h"

#define TAP_TRACE(obj, ...)                                              \
do {                                                                     \
    Tap##obj::TraceMsg(PacketTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while (false)                                                          \

///////////////////////////////////////////////////////////////////////////////

static bool InterfaceExists(std::string if_name) {
    struct ifaddrs *ifaddrs, *ifa;

    if (getifaddrs(&ifaddrs) < 0)
        return false;

    for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && strcmp(ifa->ifa_name, if_name.c_str()) == 0) {
            freeifaddrs(ifaddrs);
            return true;
        }
    }

    freeifaddrs(ifaddrs);
    return false;
}

void Pkt0Interface::InitControlInterface() {
    unsigned int flags;
    pkt_handler()->agent()->set_pkt_interface_name(name_);

    // Create a socket for the TAP device
    int socket_fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        LOG(ERROR, "Error creating socket for a TAP device, errno: " <<
            errno << ": " << strerror(errno));
        assert(0);
    }

    // XXX: If interface of the name name_ exists, delete it first. It is
    // necessary on FreeBSD as we have to create a new one to obtain path
    // to the device file (/dev/tapX) used for receiving/sending the actual
    // data. Should find a way to attach to the existing interface instead.
    struct ifreq ifr;
    if (InterfaceExists(name_)) {
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
        if (ioctl(socket_fd, SIOCIFDESTROY, &ifr) < 0) {
            LOG(ERROR, "Error destroying the existing " << name_ <<
            " device, errno: " << errno << ": " << strerror(errno));
            assert(0);
        }
    }

    // Create a device with 'tap' name as required by the FreeBSD. Later it
    // can be renamed to pkt0 for example.
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "tap", IF_NAMESIZE);
    if (ioctl(socket_fd, SIOCIFCREATE2, &ifr) < 0) {
        LOG(ERROR, "Error creating the TAP device, errno: " << errno <<
            ": " << strerror(errno));
        assert(0);
    }

    // Set interface name and save the /dev/tapX path for later use
    std::string dev_name = std::string("/dev/") +
        std::string(ifr.ifr_name);
    ifr.ifr_data = (caddr_t) name_.c_str();
    if (ioctl(socket_fd, SIOCSIFNAME, &ifr) < 0) {
        LOG(ERROR, "Can not change interface name to " << name_ <<
            "with error: " << errno << ": " << strerror(errno));
        assert(0);
    }

    // Save mac address
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    if (ioctl(socket_fd, SIOCGIFADDR, &ifr)< 0) {
        LOG(ERROR, "Can not get mac address of the TAP device, errno: "
            << errno << ": " << strerror(errno));
        assert(0);
    }
    memcpy(mac_address_, ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);
    close(socket_fd);

    // Set the interface up
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        LOG(ERROR, "Can not open socket for " << name_ << ", errno: " <<
            errno << ": " << strerror(errno));
        assert(0);
    }
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    if (ioctl(socket_fd, SIOCGIFFLAGS, &ifr) < 0) {
        LOG(ERROR, "Can not get socket flags, errno: " << errno << ": " <<
            strerror(errno));
        assert(0);
    }

    flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
    flags |= (IFF_UP|IFF_PPROMISC);
    ifr.ifr_flags = flags & 0xffff;
    ifr.ifr_flagshigh = flags >> 16;

    if (ioctl(socket_fd, SIOCSIFFLAGS, &ifr) < 0) {
        LOG(ERROR, "Can not set socket flags, errno: " << errno << ": " <<
            strerror(errno));
        assert(0);
    }
    close(socket_fd);

    // Open a device file for the tap interface
    tap_fd_ = open(dev_name.c_str(), O_RDWR);
    if (tap_fd_ < 0) {
        LOG(ERROR, "Can not open the device " << dev_name << " errno: "
            << errno << ": " << strerror(errno));
        assert(0);
    }

    // We dont want the fd to be inherited by child process such as
    // virsh etc... So, close tap fd on fork.
    if (fcntl(tap_fd_, F_SETFD, FD_CLOEXEC) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> setting fcntl on " << name_ );
        assert(0);
    }

    boost::system::error_code ec;
    input_.assign(tap_fd_, ec);
    assert(ec == 0);

    VrouterControlInterface::InitControlInterface();
    AsyncRead();
}

void Pkt0Interface::SendImpl(uint8_t *buff, uint16_t buff_len, const PacketBufferPtr &pkt,
                             buffer_list& buff_list) {
    input_.async_write_some(buff_list,
                            boost::bind(&Pkt0Interface::WriteHandler, this,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred, buff));
}
