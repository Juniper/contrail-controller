/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface.h>
#include <oper/vm_interface.h>
#include <oper/physical_interface.h>
#include <oper/packet_interface.h>

void Interface::ObtainOsSpecificParams(const std::string &name) {
    os_params_.os_oper_state_ = false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name.c_str(), IF_NAMESIZE);
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);
    if (ioctl(fd, SIOCGIFHWADDR, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> querying mac-address for interface <" << name << "> " <<
            "Agent-index <" << id_ << ">");
        close(fd);
        return;
    }

    if (ioctl(fd, SIOCGIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> querying flags for interface <" << name << "> " <<
            "Agent-index <" << id_ << ">");
        close(fd);
        return;
    }

    if ((ifr.ifr_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING)) {
        os_params_.os_oper_state_ = true;
    }
    close(fd);
#if defined(__linux__)
    os_params_.mac_ = ifr.ifr_hwaddr;
#elif defined(__FreeBSD__)
    os_params_.mac_ = ifr.ifr_addr;
#endif

    int idx = if_nametoindex(name.c_str());
    if (idx)
        os_params_.os_index_ = idx;
}

void VmInterface::ObtainOsSpecificParams(const std::string &name) {
    Interface::ObtainOsSpecificParams(name);
}

void PhysicalInterface::ObtainOsSpecificParams(const std::string &name) {
    Interface::ObtainOsSpecificParams(name);
}

void PacketInterface::ObtainOsSpecificParams(const std::string &name) {
    Interface::ObtainOsSpecificParams(name);
}
