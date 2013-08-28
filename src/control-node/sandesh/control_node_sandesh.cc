/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "control-node/control_node.h"
#include "control-node/sandesh/control_node_types.h"

void ShutdownControlNode::HandleRequest() const {
    ControlNodeShutdown();
}

