/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_controller_sandesh_h_
#define vnsw_controller_sandesh_h_

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

/////////////////////////////////////////////////////////////////////////////
// To handle Sandesh for Connection to Controller
/////////////////////////////////////////////////////////////////////////////
class ControllerSandesh {
public:
    static const uint8_t entries_per_sandesh = 20;
    ControllerSandesh();
    virtual ~ControllerSandesh() {}

private:
    void SandeshDone();
    SandeshResponse *resp_;
    DISALLOW_COPY_AND_ASSIGN(ControllerSandesh);
};

#endif // vnsw_controller_sandesh_h_
