#include <sandesh/common/flow_types.h>

bool SessionIpPortProtocol::operator < (const SessionIpPortProtocol &rhs) const {
    if (!(ip < rhs.ip)) {
        return false;
    }
    if (!(port < rhs.port)) {
        return false;
    }
    if (!(protocol < rhs.protocol)) {
        return false;
    }
    return true;
}

bool SessionIpPort::operator < (const SessionIpPort &rhs) const {
   if (!(ip < rhs.ip)) {
        return false;
    }
    if (!(port < rhs.port)) {
        return false;
    }
    return true;
}
