/*
 *  Copyright (c) 2014 Codilime
 */

#include "analytics/stream_handler.h"

#include <string>
#include <tbb/mutex.h>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <sandesh/sandesh_message_builder.h>

#include "base/queue_task.h"

using analytics::OutputStreamHandler;
using analytics::SandeshStreamData;

OutputStreamHandler::OutputStreamHandler(EventManager *evm, std::string unique_name)
: unique_name_(unique_name),
  task_id_(
   TaskScheduler::GetInstance()->GetTaskId(unique_name_.c_str())),
  output_workqueue_(task_id_, -1,
                   boost::bind(&OutputStreamHandler::Dequeue, this, _1))
{
    // The callback we add here decides whether a series of dequeue() calls
    // should be made after output_workqueue_.Enqueue().
    output_workqueue_.SetStartRunnerFunc(
        boost::bind(&OutputStreamHandler::isReadyForDequeue, this));
}

void OutputStreamHandler::Enqueue(
    boost::shared_ptr<const struct SandeshStreamData> msg) {
    output_workqueue_.Enqueue(msg);
}

bool OutputStreamHandler::Dequeue(
    boost::shared_ptr<const struct SandeshStreamData> msg) {
    return ProcessMessage(*msg);
}
