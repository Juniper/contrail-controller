/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <port_ipc/port_ipc_handler.h>
#include <oper/windows/windows_net_interface.h>

const std::string PortIpcHandler::kPortsDir = "C:\\ProgramData\\Contrail\\var\\lib\\contrail\\ports";

bool PortIpcHandler::InterfaceExists(const std::string &name) const {
    namespace WNI = WindowsNetworkInterface;

    auto luid = WNI::GetVmInterfaceLuidFromName(name, 0);
    if (!luid) {
        return false;
    }

    auto idx = WNI::GetInterfaceIndexFromLuid(*luid);
    if (!idx) {
        return false;
    }

    return *idx != 0;
}
