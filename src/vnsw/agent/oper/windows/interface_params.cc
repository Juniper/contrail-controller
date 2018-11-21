/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface.h>
#include <oper/vm_interface.h>
#include <oper/physical_interface.h>
#include <oper/packet_interface.h>
#include <oper/windows/windows_net_interface.h>

namespace WNI = WindowsNetworkInterface;

void Interface::ObtainOsSpecificParams(const std::string &name) {
    LOG(ERROR, "Error: unsupported Interface type name=" << name << ", type = " << type_);
}

void VmInterface::ObtainOsSpecificParams(const std::string &name) {
    boost::optional<NET_LUID> luid;

    if (vmi_type_ == VmInterface::INSTANCE) {
        luid = WNI::GetVmInterfaceLuidFromName(name);
    } else if (vmi_type_ == VmInterface::VHOST) {
        luid = WNI::GetPhysicalInterfaceLuidFromName(name);
    } else {
        LOG(ERROR, "Error: unsupported VmInterface type name=" << name
            << ", vmi_type = " << vmi_type_);
        return;
    }

    auto optional_os_params = WNI::GetRealIdsFromOptionalLuid(luid);
    if (optional_os_params) {
        os_params_ = *optional_os_params;
    }
}

void PhysicalInterface::ObtainOsSpecificParams(const std::string &name) {
    auto luid = WNI::GetPhysicalInterfaceLuidFromName(name);
    auto optional_os_params = WNI::GetRealIdsFromOptionalLuid(luid);
    if (optional_os_params) {
        os_params_ = *optional_os_params;
    }
}

void PacketInterface::ObtainOsSpecificParams(const std::string &) {
    /* Fake values since pkt0 on Windows is not an interface but a named pipe */
    os_params_.os_oper_state_ = true;
    os_params_.os_index_ = 0;
}
