/*
 *  Copyright (c) 2014 Codilime
 */

#ifndef SRC_STREAM_HANDLER_H_
#define SRC_STREAM_HANDLER_H_

#include <tbb/mutex.h>
#include <boost/shared_ptr.hpp>
#include <sandesh/sandesh_message_builder.h>
#include <boost/property_tree/ptree.hpp>

#include "base/queue_task.h"

class EventManager;

namespace analytics {
  class OutputStreamHandler {
   public:
    // TODO It might be better to replace property_tree with an autogen::
    // config type. Especially that we're planning to configure this part
    // with IF-MAP. An XML schema for it would need to be defined.
    typedef boost::property_tree::ptree StreamHandlerConfig;

    // Used by StreamHandlerFactory. It's a convention needed for .so plugin
    // system implementation.
    static const char *handler_name() { return "OutputStreamHandler"; }

    // These arguments are supplied by appropriate factory.
    // Configuration update implementation (in case of, ie. IF-MAP database)
    // relies on unique_name. Without it we would need to delete plugin-local
    // data every configuration change, which isn't a good solution.
    OutputStreamHandler(EventManager *evm, std::string unique_name);
    virtual ~OutputStreamHandler() {};

    void Enqueue(boost::shared_ptr<const struct SandeshStreamData>);

    // May be called multiple times to reconfigure after configuration via
    // IF-MAP is implemented.
    virtual bool Configure(StreamHandlerConfig &) = 0;
    virtual bool ProcessMessage(const struct SandeshStreamData &) = 0;
    virtual bool isReadyForDequeue() { return true; }

   protected:
    typedef WorkQueue<boost::shared_ptr<const struct SandeshStreamData> >
        OutputWorkqueue;

    // WorkQueue implementation creates separate Task for every
    // dequeue.
    bool Dequeue(boost::shared_ptr<const struct SandeshStreamData>);

    std::string unique_name_;
    int task_id_;
    OutputWorkqueue output_workqueue_;
    // used by implementations of this abstract class 
    EventManager *evm_;
  };
}

#endif
