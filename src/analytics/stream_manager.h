/*
 *  Copyright (c) 2014 Codilime
 */

#ifndef SRC_STREAM_MANAGER_H_
#define SRC_STREAM_MANAGER_H_
#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <tbb/reader_writer_lock.h>
#include <sandesh/sandesh_message_builder.h>
#include <pugixml/pugixml.hpp>

#include "analytics/stream_handler.h"
#include "ifmap/ifmap_config_listener.h"

namespace analytics {
    class StreamHandlerFactory;
    typedef boost::ptr_vector<OutputStreamHandler> HandlerContainer;

    // We want just the data from SandeshXMLMessage, without the rest of
    // implementation and associated helper variables.
    struct SandeshStreamData {
        SandeshStreamData(const SandeshXMLMessage &);

        SandeshHeader header;
        pugi::xml_document xml_doc;
    };

    class OutputStreamManager {
     public:
      class ConfigListener {
       protected:
          // not meant to be instantiated
          ConfigListener(OutputStreamManager *target)
          : target_(target) {}
          virtual ~ConfigListener() {};

          OutputStreamManager *target_;
      };

      class INIConfigReader : public ConfigListener {
       public:
          INIConfigReader(OutputStreamManager *owner)
          : ConfigListener(owner) {}

          // INI doesn't support duplication of section names,
          // so using separate config file for every output
          // stream seems to be a good solution.
          bool Configure(std::string directory_path);

       protected:
          // Creates an appropriate OutputStreamHandler, configures it, and
          // calls owner's addHandler().
          bool Configure(OutputStreamHandler::StreamHandlerConfig &);
      };

      // Will create a shared_ptr and add it to internal workqueues of output
      // handlers.
      // Takes ownership of passed pointer.
      void append(const SandeshXMLMessage *);

      const HandlerContainer *output_handlers() {
            return &output_handlers_;
      }

     protected:
      void addHandler(OutputStreamHandler *);

      HandlerContainer output_handlers_;
    };
}

#endif
