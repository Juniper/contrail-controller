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

#include <sys/sockio.h>
#include <net/if_tap.h>

#include <ifaddrs.h>
#include <net/ethernet.h>
#include <sys/errno.h>

#include "base/os.h"
#include "base/logging.h"

#define TAP_INTF_CLONE_DEV "/dev/tap"

void DeleteTapByName(const char* if_name) {
    int socket_fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        LOG(ERROR, "Error creating socket for a TAP device, errno: " <<
            errno << ": " << strerror(errno));
        assert(0);
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, if_name, IF_NAMESIZE);
    if (ioctl(socket_fd, SIOCIFDESTROY, &ifr) < 0) {
        LOG(ERROR, "Error destroying the existing " << &ifr.ifr_name <<
            " device, errno: " << errno << ": " << strerror(errno));
        assert(0);
    }

    close(socket_fd);
}

void DeleteTap(int fd) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    if(ioctl(fd, TAPGIFNAME, &ifr) < 0) {
        LOG(ERROR, "Error getting name of the device, errno: " <<
            errno << ": " << strerror(errno));
        assert(0);
    }

    DeleteTapByName(ifr.ifr_name);
}

void DeleteTapIntf(const int fd[], int count) {
    for (int i = 0; i < count; i++) {
        DeleteTap(fd[i]);
    }
}

static bool InterfaceExists(const char* if_name) {
    struct ifaddrs *ifaddrs, *ifa;

    if (getifaddrs(&ifaddrs) < 0)
        return false;

    for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && strcmp(ifa->ifa_name, if_name) == 0) {
            freeifaddrs(ifaddrs);
            return true;
        }
    }

    freeifaddrs(ifaddrs);
    return false;
}

int CreateTap(const char *name) {
    struct ifreq ifr;

    // TAP_INTF_CLONE_DEV is a cloning device. open() returns descriptor to the
    // first free tap - /dev/tapX, where X is the lowest possible number,
    // unknown yet. Straightforward creation of a tapwith a custom name is not
    // possible, so we are creating a tapX device, which will be renamed later.
    // Knowing value of X is necessary in a process of changing tap's name.
    int  fd = open(TAP_INTF_CLONE_DEV, O_RDWR);
    if (fd < 0) {
        LOG(ERROR, "Can not open the device " << TAP_INTF_CLONE_DEV <<
             " errno: " << errno << ": " << strerror(errno));
        assert(0);
    }

    // Ask tapX for it's full name.
    memset(&ifr, 0, sizeof(ifr));
    if(ioctl(fd, TAPGIFNAME, &ifr) < 0) {
        LOG(ERROR, "Error getting name of the device, errno: " <<
            errno << ": " << strerror(errno));
        assert(0);
    }

    int socket_fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        LOG(ERROR, "Error creating socket for a TAP device, errno: " <<
            errno << ": " << strerror(errno));
        assert(0);
    }

    // Remove old taps which utilize a new name we want to use.
    if (InterfaceExists(name))
        DeleteTapByName(name);

    // Change name only if the new one is different than the old one.
    if(strcmp(name, ifr.ifr_name) != 0) {
        // Write a new name to the struct containing the old name
        // of tapX. Old name is already in ifr.ifr_name.
        ifr.ifr_data = (caddr_t) name;
        if (ioctl(socket_fd, SIOCSIFNAME, &ifr) < 0) {
            LOG(ERROR, "Can not change interface name to " << name <<
                "with error: " << errno << ": " << strerror(errno));
            assert(0);
        }
    }

    close(socket_fd);

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
    struct ifreq ifr;
    char ifname[IF_NAMESIZE];

    for (int i = 0; i < count; i++) {
        snprintf(ifname, IF_NAMESIZE, "%s%d", name, i);
        fd[i] = CreateTap(ifname);

        int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
        LOG(ERROR, "Can not open socket for " << ifname << ", errno: " <<
            errno << ": " << strerror(errno));
            assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
        if (ioctl(socket_fd, SIOCGIFFLAGS, &ifr) < 0) {
            LOG(ERROR, "Can not get socket flags, errno: " << errno << ": " <<
                strerror(errno));
            assert(0);
        }

        unsigned int flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
        flags |= (IFF_UP|IFF_PPROMISC);
        ifr.ifr_flags = flags & 0xffff;
        ifr.ifr_flagshigh = flags >> 16;

        if (ioctl(socket_fd, SIOCSIFFLAGS, &ifr) < 0) {
            LOG(ERROR, "Can not set socket flags, errno: " << errno << ": " <<
                strerror(errno));
            assert(0);
        }

        close(socket_fd);
    }
}

