/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <fstream>
#include <tbb/task.h>
#include <base/task.h>
#include <base/logging.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <base/sandesh/task_types.h>

using namespace std;
void SandeshTaskRequest::HandleRequest() const {
    SandeshTaskScheduler *resp = new SandeshTaskScheduler;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->GetSandeshData(resp);
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}
