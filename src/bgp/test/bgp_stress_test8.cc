/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#define __BGP_STRESS_TEST_SUITE__
#include "bgp_stress_test.cc"

// Inject a lot more events
int main(int argc, char **argv) {
    const char *largv[] = {
        __FILE__, "--log-disable",

        "--nevents=-1",
        "--nroutes=100",
        "--nagents=10",
        "--npeers=5",
        "--ninstances=10",
        "--xmpp-auth-enabled",
    };
    return bgp_stress_test_main(sizeof(largv)/sizeof(largv[0]), largv);
}
