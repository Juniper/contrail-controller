/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */


#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <cstring>
#include <string>
#include <net/if.h>
#include <unistd.h>

#include <linux/if_tun.h>
#include <netinet/in.h>
#include <netpacket/packet.h>

#include <ifaddrs.h>
#include <net/ethernet.h>
#include <sys/errno.h>

#include "base/os.h"
#include "base/logging.h"

#define TUN_INTF_CLONE_DEV "/dev/net/tun"

void DeleteTap(int fd) {
    if (ioctl(fd, TUNSETPERSIST, 0) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> making tap interface non-persistent");
        assert(0);
    }
}

void DeleteTapIntf(const int fd[], int count) {
    for (int i = 0; i < count; i++) {
        DeleteTap(fd[i]);
    }
}

int CreateTap(const char *name) {
    int fd;
    struct ifreq ifr;

    if ((fd = open(TUN_INTF_CLONE_DEV, O_RDWR)) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> opening tap-device");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, name, IF_NAMESIZE);
    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> creating " << name << "tap-device");
        assert(0);
    }

    if (ioctl(fd, TUNSETPERSIST, 1) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> making tap interface persistent");
        assert(0);
    }
    return fd;
}

void CreateTapIntf(const char *name, int count) {
    char ifname[IF_NAMESIZE];

    for (int i = 0; i < count; i++) {
        snprintf(ifname, IF_NAMESIZE, "%s%d", name, i);
        CreateTap(ifname);
    }
}

void CreateTapInterfaces(const char *name, int count, int *fd) {
    char ifname[IF_NAMESIZE];
    int raw;
    struct ifreq ifr;

    for (int i = 0; i < count; i++) {
        snprintf(ifname, IF_NAMESIZE, "%s%d", name, i);
        fd[i] = CreateTap(ifname);

        if ((raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                                "> creating socket");
                assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
        if (ioctl(raw, SIOCGIFINDEX, (void *)&ifr) < 0) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                                "> getting ifindex of the tap interface");
                assert(0);
        }

        struct sockaddr_ll sll;
        memset(&sll, 0, sizeof(struct sockaddr_ll));
        sll.sll_family = AF_PACKET;
        sll.sll_ifindex = ifr.ifr_ifindex;
        sll.sll_protocol = htons(ETH_P_ALL);
        if (bind(raw, (struct sockaddr *)&sll, sizeof(struct sockaddr_ll)) < 0) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                                "> binding the socket to the tap interface");
                assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
        if (ioctl(raw, SIOCGIFFLAGS, (void *)&ifr) < 0) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                                "> getting socket flags");
                assert(0);
        }

        ifr.ifr_flags |= IFF_UP;
        if (ioctl(raw, SIOCSIFFLAGS, (void *)&ifr) < 0) {
                LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
                                "> setting socket flags");
                assert(0);
        }
    }
}

