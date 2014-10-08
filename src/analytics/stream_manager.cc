/*
 *  Copyright (c) 2014 Codilime
 */

#include "analytics/stream_manager.h"
#include "analytics/stream_handler.h"

#include <vector>
#include <tbb/reader_writer_lock.h>
#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>
#include <sandesh/sandesh_message_builder.h>

using analytics::OutputStreamManager;
using analytics::OutputStreamHandler;
using analytics::SandeshStreamData;

SandeshStreamData::SandeshStreamData(const SandeshXMLMessage &msg)
: header(msg.GetHeader()) {
      // make a deep copy of the document
      xml_doc.append_copy(msg.GetMessageNode());
}

void OutputStreamManager::addHandler(OutputStreamHandler *ptr) {
    tbb::reader_writer_lock::scoped_lock writelock(output_handlers_rwlock_);
    output_handlers_.push_back(ptr);
}

void OutputStreamManager::append(const SandeshXMLMessage *msg) {
    // This lock is a no-op as long as addHandler doesn't block, which doesn't
    // happen frequently.
    tbb::reader_writer_lock::scoped_lock_read readlock(output_handlers_rwlock_);
    boost::shared_ptr<const struct SandeshStreamData> shptr(
        new struct SandeshStreamData(*msg));
    BOOST_FOREACH(OutputStreamHandler *phandler, output_handlers_) {
        phandler->Enqueue(shptr);
    }
}
