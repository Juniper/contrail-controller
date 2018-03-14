/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface.h>
#include <oper/vm_interface.h>
#include <oper/physical_interface.h>
#include <oper/packet_interface.h>

void Interface::ObtainKernelspaceIdentifiers(const std::string &name) {
    int idx = if_nametoindex(name.c_str());
    if (idx)
        os_index_ = idx;
}

void VmInterface::ObtainKernelspaceIdentifiers(const std::string &name) {
    Interface::ObtainKernelspaceIdentifiers(name);
}

void PhysicalInterface::ObtainKernelspaceIdentifiers(const std::string &name) {
    Interface::ObtainKernelspaceIdentifiers(name);
}

void PacketInterface::ObtainKernelspaceIdentifiers(const std::string &name) {
    Interface::ObtainKernelspaceIdentifiers(name);
}

void Interface::ObtainUserspaceIdentifiers(const std::string &name) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name.c_str(), IF_NAMESIZE);
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);
    if (ioctl(fd, SIOCGIFHWADDR, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> querying mac-address for interface <" << name << "> " <<
            "Agent-index <" << id_ << ">");
        os_oper_state_ = false;
        close(fd);
        return;
    }

    if (ioctl(fd, SIOCGIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Error <" << errno << ": " << strerror(errno) <<
            "> querying flags for interface <" << name << "> " <<
            "Agent-index <" << id_ << ">");
        os_oper_state_ = false;
        close(fd);
        return;
    }

    os_oper_state_ = false;
    if ((ifr.ifr_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING)) {
        os_oper_state_ = true;
    }
    close(fd);
#if defined(__linux__)
    mac_ = ifr.ifr_hwaddr;
#elif defined(__FreeBSD__)
    mac_ = ifr.ifr_addr;
#endif
}

void PacketInterface::ObtainUserspaceIdentifiers(const std::string &name) {
    Interface::ObtainUserspaceIdentifiers(name);
}
