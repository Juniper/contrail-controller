/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>

#if defined(__linux__)
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#elif defined(__FreeBSD__)
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include <boost/asio.hpp>

#include "base/logging.h"
#include "cmn/agent_cmn.h"
#include "tap_interface.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "pkt/pkt_types.h"
#include "pkt/pkt_handler.h"
#include "pkt/pkt_init.h"

#define TUN_INTF_CLONE_DEV "/dev/net/tun"

#define TAP_TRACE(obj, ...)                                              \
do {                                                                     \
    Tap##obj::TraceMsg(PacketTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while (false)                                                          \

///////////////////////////////////////////////////////////////////////////////

TapInterface::TapInterface(Agent *agent,
                           const std::string &name,
                           boost::asio::io_service &io,
                           PktReadCallback cb)
                         : tap_fd_(-1), agent_(agent), name_(name),
                           read_buf_(NULL), pkt_handler_(cb), input_(io)
{
}

TapInterface::~TapInterface() {
}

void TapInterface::Init() {
    SetupTap();
    boost::system::error_code ec;
    input_.assign(tap_fd_, ec);
    assert(ec == 0);

    AsyncRead();
}

void TapInterface::IoShutdown() {
    if (read_buf_) {
        delete [] read_buf_;
    }
    close(tap_fd_);
    tap_fd_ = -1;
}

#if defined(__linux__)
void TapInterface::SetupTap() {
    if (name_ == agent_->pkt_interface_name()) {
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
            LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) <<
                "> retrieving MAC address of the tap interface");
            assert(0);
        }
        mac_address_ = ifr.ifr_hwaddr;

        int raw_;
        if ((raw_ = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) << "> creating socket");
            assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name_.data(), IF_NAMESIZE);
        if (ioctl(raw_, SIOCGIFINDEX, (void *)&ifr) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) << "> getting ifindex of the tap interface");
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

        close(raw_);
    }
}
#elif defined(__FreeBSD__)

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

void TapInterface::SetupTap() {
    unsigned int flags;

    if (name_ == agent_->pkt_interface_name()) {
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
                LOG(ERROR, "Error destroying the existed " << name_ <<
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
        mac_address_ = ifr.ifr_addr;
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
    }
}
#endif // defined(__linux__)

void TapInterface::Encode(uint8_t *buff, const AgentHdr &hdr) {
    bzero(buff, sizeof(agent_hdr));

    // Add outer ethernet header
    struct ether_header *eth = (ether_header *)buff;
    eth->ether_shost[ETHER_ADDR_LEN - 1] = 1;
    eth->ether_dhost[ETHER_ADDR_LEN - 1] = 2;
    eth->ether_type = htons(ETHERTYPE_IP);

    // Fill agent_hdr
    agent_hdr *vr_agent_hdr = (agent_hdr *) (eth + 1);

    vr_agent_hdr->hdr_ifindex = htons(hdr.ifindex);
    vr_agent_hdr->hdr_vrf = htons(hdr.vrf);
    vr_agent_hdr->hdr_cmd = htons(hdr.cmd);
}

void TapInterface::WritePacketBufferHandler
    (const boost::system::error_code &error, std::size_t length,
     PacketBufferPtr pkt, uint8_t *buff) {
    if (error)
        TAP_TRACE(Err,
                  "Packet Tap Error <" + error.message() + "> sending packet");

    delete [] buff;
}

void TapInterface::Send(const std::vector<boost::asio::const_buffer> &buff_list,
                        PacketBufferPtr pkt, uint8_t *agent_hdr_buff) {
    input_.async_write_some(buff_list,
                            boost::bind(&TapInterface::WritePacketBufferHandler,
                                        this,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred,
                                        pkt, agent_hdr_buff));
}

void TapInterface::Send(const AgentHdr &hdr, PacketBufferPtr pkt) {
    std::vector<boost::asio::const_buffer> buff_list;
    uint8_t *agent_hdr_buff = new uint8_t [EncapHeaderLen()];

    Encode(agent_hdr_buff, hdr);
    buff_list.push_back(boost::asio::buffer(agent_hdr_buff, EncapHeaderLen()));
    buff_list.push_back(boost::asio::buffer(pkt->data(), pkt->data_len()));
    Send(buff_list, pkt, agent_hdr_buff);
}

void TapInterface::ReadHandler(const boost::system::error_code &error,
                              std::size_t length) {
    if (!error) {
        pkt_handler_(read_buf_, length, kMaxPacketSize);
    } else  {
        TAP_TRACE(Err,
                  "Packet Tap Error <" + error.message() + "> reading packet");
        if (error == boost::system::errc::operation_canceled) {
            return;
        }
    }

    AsyncRead();
}

void TapInterface::AsyncRead() {
    read_buf_ = new uint8_t[kMaxPacketSize];
    input_.async_read_some(
            boost::asio::buffer(read_buf_, kMaxPacketSize),
            boost::bind(&TapInterface::ReadHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
}

///////////////////////////////////////////////////////////////////////////////
