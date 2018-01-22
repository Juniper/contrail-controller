/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_task_map_h
#define vnsw_agent_task_map_h

#include "cmn/agent_cmn.h"

typedef struct task_ task;

class TaskMap {
public:
    TaskMap(Agent *agent, const std::string &name, int instance,
                                    boost::asio::io_service &io);
    ~TaskMap();

    Agent *agent_;
    const std::string &name_;
    int instance_;
    boost::asio::io_service &io_;
    task *task_;
};

class TaskMapManager {
public:
    static TaskMap *CreateTaskMap(Agent *agent, const std::string &name,
                    int instance, boost::asio::io_service &io);
    static bool DeleteTaskMap(TaskMap *task_map);

    friend class TaskMap;
};

#endif /* vnsw_agent_task_map_h */
