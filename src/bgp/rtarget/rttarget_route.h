/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rttarget_route_h
#define ctrlplane_rttarget_route_h

#include "route/route.h"

class RtTargetRoute : public Route {
public:
    virtual int CompareTo(const Route &rhs);
private:
    DISALLOW_COPY_AND_ASSIGN(RtTargetRoute);
};

#endif
