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
}

void DeleteTap(int fd) {
    struct ifreq ifr;
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

    int socket_fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        LOG(ERROR, "Error creating socket for a TAP device, errno: " <<
            errno << ": " << strerror(errno));
        assert(0);
    }

    if (InterfaceExists(name))
        DeleteTapByName(name);

    // We need to create interface named "tap" first.
    // It will be renamed properly later.
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "tap", IF_NAMESIZE);
    if (ioctl(socket_fd, SIOCIFCREATE2, &ifr) < 0) {
        LOG(ERROR, "Error creating the TAP device " << &ifr.ifr_name <<
            ", errno: " << errno << ": " << strerror(errno));
        assert(0);
    }
    
    char ifname[IF_NAMESIZE];
    strncpy(ifname, name, IF_NAMESIZE);
    
    ifr.ifr_data = (caddr_t) ifname;
    if (ioctl(socket_fd, SIOCSIFNAME, &ifr) < 0) {
        LOG(ERROR, "Can not change interface name to " << ifname <<
            "with error: " << errno << ": " << strerror(errno));
        assert(0);
    }
    
    return socket_fd;
}

void CreateTapIntf(const char *name, int count) {
    char ifname[IF_NAMESIZE];

    for (int i = 0; i < count; i++) {
        CreateTap(ifname);
    }
}

void CreateTapInterfaces(const char *name, int count, int *fd) {
    struct ifreq ifr;
    char ifname[IF_NAMESIZE];

    for (int i = 0; i < count; i++) {
        snprintf(ifname, IF_NAMESIZE, "%s%d", name, i);
        fd[i] = CreateTap(ifname);

        close(fd[i]);
        
        fd[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd[i] < 0) {
        LOG(ERROR, "Can not open socket for " << ifname << ", errno: " <<
            errno << ": " << strerror(errno));
            assert(0);
        }
        
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IF_NAMESIZE);
        if (ioctl(fd[i], SIOCGIFFLAGS, &ifr) < 0) {
            LOG(ERROR, "Can not get socket flags, errno: " << errno << ": " <<
                strerror(errno));
            assert(0);
        }
    
        unsigned int flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
        flags |= (IFF_UP|IFF_PPROMISC);
        ifr.ifr_flags = flags & 0xffff;
        ifr.ifr_flagshigh = flags >> 16;
    
        if (ioctl(fd[i], SIOCSIFFLAGS, &ifr) < 0) {
            LOG(ERROR, "Can not set socket flags, errno: " << errno << ": " <<
                strerror(errno));
            assert(0);
        }
        close(fd[i]);
    
        // Open a device file for the tap interface.
        // Devname is: "/dev/" + "tap" + number, and it is always "tap",
        // regardless the device has been renamed 
        // with ioctl(fd, SIOCSIFNAME, &ifr), or not.
        char devname[IF_NAMESIZE];
        snprintf(devname, IF_NAMESIZE, "/dev/%s%d", "tap", i);
        
        fd[i] = open(devname, O_RDWR);
        if (fd[i] < 0) {
            LOG(ERROR, "Can not open the device " << &devname << " errno: "
                << errno << ": " << strerror(errno));
            assert(0);
        }
    }
}
