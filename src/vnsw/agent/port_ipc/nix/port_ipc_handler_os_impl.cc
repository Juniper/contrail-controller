/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <port_ipc/port_ipc_handler.h>

const std::string PortIpcHandler::kPortsDir = "/var/lib/contrail/ports";

bool PortIpcHandler::InterfaceExists(const std::string &name) const {
    int idx = if_nametoindex(name.c_str());
    return idx != 0;
}
