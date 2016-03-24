/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/task.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <base/sandesh/task_types.h>

using std::string;

static void HandleRequestCommon(const string &context, bool summary) {
    SandeshTaskScheduler *resp = new SandeshTaskScheduler;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->GetSandeshData(resp, summary);
    resp->set_context(context);
    resp->set_more(false);
    resp->Response();
}

void SandeshTaskRequest::HandleRequest() const {
    HandleRequestCommon(context(), false);
}

void SandeshTaskSummaryRequest::HandleRequest() const {
    HandleRequestCommon(context(), true);
}
