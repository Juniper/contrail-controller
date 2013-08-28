/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VIZ_SANDESH_H_
#define VIZ_SANDESH_H_

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

class VizCollector;

struct VizSandeshContext : public SandeshContext {
    VizSandeshContext(VizCollector *analytics) :
        SandeshContext(),
        analytics_(analytics) {}

    VizCollector *Analytics() { return analytics_; }
    VizCollector *analytics_;
};

#endif /* VIZ_SANDESH_H_ */
