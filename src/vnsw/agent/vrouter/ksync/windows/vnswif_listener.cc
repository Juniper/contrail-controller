/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "vnswif_listener.h"

VnswInterfaceListenerWindows::VnswInterfaceListenerWindows(Agent *agent) : VnswInterfaceListenerBase(agent) {
}

int VnswInterfaceListenerWindows::CreateSocket() {
    VNSWIF_TRACE("VnswInterfaceListenerWindows::CreateSocket -- NOOP");
    return -EACCES;
}
void VnswInterfaceListenerWindows::SyncCurrentState() {
    VNSWIF_TRACE("VnswInterfaceListenerWindows::SyncCurrentState -- NOOP");
}
void VnswInterfaceListenerWindows::RegisterAsyncReadHandler() {
    VNSWIF_TRACE("VnswInterfaceListenerWindows::RegisterAsyncReadHandler -- NOOP");
}
void VnswInterfaceListenerWindows::ReadHandler(const boost::system::error_code &, std::size_t length) {
    VNSWIF_TRACE("VnswInterfaceListenerWindows::ReadHandler -- NOOP");
}
void VnswInterfaceListenerWindows::UpdateLinkLocalRoute(const Ip4Address &addr, uint8_t plen, bool del_rt) {
    VNSWIF_TRACE("VnswInterfaceListenerWindows::UpdateLinkLocalRoute -- NOOP");
}
