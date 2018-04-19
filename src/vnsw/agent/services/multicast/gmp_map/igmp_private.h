/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_igmp_private_hpp
#define vnsw_agent_igmp_private_hpp

#define IGMP_ROBUST_COUNT       2
#define IGMP_QUERY_INTERVAL     (125 * MSECS_PER_SEC)
#define IGMP_QUERY_RESPONSE_INTERVAL    (10 * MSECS_PER_SEC)
#define IGMP_QUERY_LASTMEMBER_INTERVAL  (1   * MSECS_PER_SEC)

#define IGMP_VERSION_1      1
#define IGMP_VERSION_2      2
#define IGMP_VERSION_3      3

#endif /* vnsw_agent_igmp_private_hpp */
