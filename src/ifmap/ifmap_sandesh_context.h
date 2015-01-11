/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef IFMAP__IFMAP_SANDESH_CONTEXT_H__
#define IFMAP__IFMAP_SANDESH_CONTEXT_H__

#include <sandesh/sandesh.h>

class IFMapServer;

class IFMapSandeshContext : public SandeshContext {
public:
    IFMapSandeshContext(IFMapServer *ifmap_server)
        : ifmap_server_(ifmap_server) {
    }

    IFMapServer *ifmap_server() { return ifmap_server_; }

private:
    IFMapServer *ifmap_server_;
};

#endif  // IFMAP__IFMAP_SANDESH_CONTEXT_H__
