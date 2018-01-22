/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_task_timer_map_h
#define vnsw_agent_task_timer_map_h

class TimerMap {
public:
    TimerMap() {}
    ~TimerMap() {}

    std::string timer_name_;
    void *timer_;
    Timer *agent_timer_;
};

#endif /* vnsw_agent_task_timer_map_h */
