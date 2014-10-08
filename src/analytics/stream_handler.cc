/*
 *  Copyright (c) 2014 Codilime
 */

#include "analytics/stream_handler.h"

#include <tbb/mutex.h>
#include <tbb/reader_writer_lock.h>
#include <boost/bind.hpp>
// replace with std::vector later
#include <boost/ptr_container/ptr_vector.hpp>
#include <pugixml/pugixml.hpp>

#include "base/queue_task.h"

using analytics::OutputStreamManager;
using analytics::OutputStreamHandler;

OutputStreamManager::OutputStreamManager(const char *task_name)
: task_name_(task_name),
  output_wq_task_id_(
   TaskScheduler::GetInstance()->GetTaskId(task_name_)),
  output_workqueue_(output_wq_task_id_, -1,
                   boost::bind(&OutputStreamManager::dequeue, this, _1))
{
    // The callback we add here decides whether a series of dequeue() calls
    // should be made after output_workqueue_.Enqueue().
    output_workqueue_.SetStartRunnerFunc(
      boost::bind(&OutputStreamManager::isInitialized, this));
}

OutputStreamManager::~OutputStreamManager() {
    // notify handlers
}

void OutputStreamManager::addHandler(OutputStreamHandler *ptr) {
    output_handlers_rwlock_.lock();
    // By designing the API this way we'll be able to import handlers from
    // .so's and implement a plugin system.
    output_handlers_.push_back(ptr);
    // Notify any Tasks that might have waited for us in dequeue().
    output_handlers_rwlock_.unlock();
}

void OutputStreamManager::append(const pugi::xml_node &msg) {
    static tbb::mutex mutex;
    tbb::mutex::scoped_lock lock(mutex);
    // WorkQueue doesn't seem to be thread-safe.
    output_workqueue_.Enqueue(msg);
}

bool OutputStreamManager::dequeue(const pugi::xml_node &msg) {
    // Protect from iterator invalidation in addHandler().
    output_handlers_rwlock_.lock_read();
    boost::ptr_vector<OutputStreamHandler>::iterator it =
        output_handlers_.begin();
    for (; it != output_handlers_.end(); it++) {
	// the handler must be thread-safe
        it->appendMessage(msg);
    }
    output_handlers_rwlock_.unlock();
    return true;
}
