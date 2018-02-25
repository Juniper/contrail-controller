/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "task_map.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "task_int.h"
#include "task_timer_api.h"
#ifdef __cplusplus
}
#endif

TaskMap::TaskMap(Agent *agent, const std::string &name, int instance,
                        boost::asio::io_service &io) :
                        agent_(agent), name_(name), instance_(instance),
                        io_(io) {

    task_ = NULL;
}

TaskMap::~TaskMap() {
}

task *task_create(void *task_map)
{
    task *new_task;
    new_task = (task *)malloc(sizeof(task));
    if (!new_task) {
        return NULL;
    }
    new_task->agent_task = task_map;
    return new_task;
}

TaskMap *TaskMapManager::CreateTaskMap(Agent *agent, const std::string &name,
            int instance, boost::asio::io_service &io) {

    TaskMap *task_map = new TaskMap(agent, name, instance, io);
    if (!task_map) {
        return NULL;
    }

    task_map->task_ = task_create(task_map);
    if (!task_map->task_) {
        delete task_map;
        return NULL;
    }

    task_timer_init(task_map->task_);

    return task_map;
}

bool TaskMapManager::DeleteTaskMap(TaskMap *task_map) {
    if (!task_map) {
        return false;
    }

    free(task_map->task_);

    delete task_map;

    return true;
}

