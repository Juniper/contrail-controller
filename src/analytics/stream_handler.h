/*
 *  Copyright (c) 2014 Codilime
 */

#ifndef SRC_STREAM_HANDLER_H_
#define SRC_STREAM_HANDLER_H_
#include <tbb/mutex.h>
#include <tbb/reader_writer_lock.h>
#include <boost/ptr_container/ptr_vector.hpp>
#include <pugixml/pugixml.hpp>
#include "base/queue_task.h"

namespace analytics {
  class OutputStreamHandler {
   public:
    // The handler should be asynchronous.
    virtual bool appendMessage(const pugi::xml_node &) = 0;
    virtual ~OutputStreamHandler();
  };

  class OutputStreamManager {
   public:
    // initialize WorkQueue
    OutputStreamManager(const char *task_name);
    ~OutputStreamManager();

    // a dummy for Contrail WorkQueue we use
    bool isInitialized() { return true; }
    void addHandler(OutputStreamHandler *);

    // will copy the value and add it to internal workqueue
    void append(const pugi::xml_node &);

   private:
    // WorkQueue implementation creates separate Task for every
    // dequeue.
    bool dequeue(const pugi::xml_node &);

    const char *task_name_;

    int output_wq_task_id_;
    WorkQueue<pugi::xml_node> output_workqueue_;

    boost::ptr_vector<OutputStreamHandler> output_handlers_;
    tbb::interface5::reader_writer_lock output_handlers_rwlock_;
  };
}

#endif
