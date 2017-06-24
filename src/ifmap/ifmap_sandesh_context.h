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
        : ifmap_server_(ifmap_server), page_limit_(0), iter_limit_(0) {
    }
    uint32_t page_limit() const { return page_limit_; }
    void set_page_limit(uint32_t page_limit) { page_limit_ = page_limit; }
    uint32_t iter_limit() const { return iter_limit_; }
    void set_iter_limit(uint32_t iter_limit) { iter_limit_ = iter_limit; }

    IFMapServer *ifmap_server() { return ifmap_server_; }

private:
    IFMapServer *ifmap_server_;
    uint32_t page_limit_;
    uint32_t iter_limit_;
};

#endif  // IFMAP__IFMAP_SANDESH_CONTEXT_H__
