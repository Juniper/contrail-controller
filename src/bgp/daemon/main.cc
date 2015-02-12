/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_server.h"
#include "io/event_manager.h"

int main(int argc, char *argv[]) {
    EventManager evm;
    BgpServer bgp_server(&evm);
    evm.Run();
}
