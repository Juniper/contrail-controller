/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/common/flow_types.h>

bool SessionIpPortProtocol::operator < (const SessionIpPortProtocol &rhs) const {
    if (port < rhs.port) {
        return true;
    } else if (port == rhs.port && protocol < rhs.protocol) {
        return true;
    } else if (port == rhs.port && protocol == rhs.protocol && ip < rhs.ip) {
        return true;
    } else {
        return false;
    }
}

bool SessionIpPort::operator < (const SessionIpPort &rhs) const {
    if (port < rhs.port) {
        return true;
    } else if (port == rhs.port && ip < rhs.ip) {
        return true;
    } else {
        return false;
    }
}
