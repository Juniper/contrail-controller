/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "vnswif_listener.h"

VnswInterfaceListenerWindows::VnswInterfaceListenerWindows(Agent *agent) : VnswInterfaceListenerBase(agent) {
}

int VnswInterfaceListenerWindows::CreateSocket() {
    return -EACCES;
}
void VnswInterfaceListenerWindows::SyncCurrentState() {
    assert(0);
}
void VnswInterfaceListenerWindows::RegisterAsyncReadHandler() {
    assert(0);
}
void VnswInterfaceListenerWindows::ReadHandler(const boost::system::error_code &, std::size_t length) {
    assert(0);
}
void VnswInterfaceListenerWindows::UpdateLinkLocalRoute(const Ip4Address &addr, uint8_t plen, bool del_rt) {
    assert(0);
}
