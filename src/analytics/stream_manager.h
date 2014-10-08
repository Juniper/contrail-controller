/*
 *  Copyright (c) 2014 Codilime
 */

#ifndef SRC_STREAM_MANAGER_H_
#define SRC_STREAM_MANAGER_H_
#include <vector>
#include <boost/shared_ptr.hpp>
#include <tbb/reader_writer_lock.h>
#include <sandesh/sandesh_message_builder.h>
#include <pugixml/pugixml.hpp>

namespace analytics {
    class OutputStreamHandler;

    // We want just the data from SandeshXMLMessage, without the rest of
    // implementation and associated helper variables.
    struct SandeshStreamData {
        SandeshStreamData(const SandeshXMLMessage &);

        SandeshHeader header;
        pugi::xml_document xml_doc;
    };

    class OutputStreamManager {
     public:
      void addHandler(OutputStreamHandler *);

      // Will create a shared_ptr and add it to internal workqueues of output
      // handlers.
      // Takes ownership of passed pointer.
      void append(const SandeshXMLMessage *);

     private:
      typedef std::vector<OutputStreamHandler *> HandlerContainer;

      HandlerContainer output_handlers_;
      tbb::reader_writer_lock output_handlers_rwlock_;
    };
}

#endif
