/*
 *  Copyright (c) 2014 Codilime
 */

#ifndef SRC_STREAM_HANDLER_H_
#define SRC_STREAM_HANDLER_H_
#include <tbb/mutex.h>
#include <boost/shared_ptr.hpp>
#include <sandesh/sandesh_message_builder.h>
#include "base/queue_task.h"

namespace analytics {
  class OutputStreamHandler {
   public:
    OutputStreamHandler(const char *task_id);
    virtual ~OutputStreamHandler() {};

    virtual bool isReadyForDequeue() { return true; }
    void Enqueue(boost::shared_ptr<const struct SandeshStreamData>);
    virtual bool ProcessMessage(const struct SandeshStreamData &) = 0;

   protected:
    typedef WorkQueue<boost::shared_ptr<const struct SandeshStreamData> >
        OutputWorkqueue;

    // WorkQueue implementation creates separate Task for every
    // dequeue.
    bool Dequeue(boost::shared_ptr<const struct SandeshStreamData>);

    const char *task_name_;

    int task_id_;
    OutputWorkqueue output_workqueue_;
  };
}

#endif
