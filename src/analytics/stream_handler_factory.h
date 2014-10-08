/*
 * Copyright (c) 2014 Codilime.
 */

#ifndef SRC_STREAM_HANDLER_FACTORY_
#define SRC_STREAM_HANDLER_FACTORY_

#include <map>
#include "base/factory.h"
#include "analytics/stream_handler.h"
#include "analytics/xml_output_stream.h"

class EventManager;

namespace analytics {

class StreamHandlerFactory : public Factory<StreamHandlerFactory> {
 public:
    static OutputStreamHandler *Create(std::string type_name,
                                       std::string unique_id);

    static void set_event_manager(EventManager *evm) {
        evm_ = evm;
    }

 private:
    typedef OutputStreamHandler *(*CreateFunc)(EventManager *, std::string);
    typedef std::map<std::string, CreateFunc> CreateFuncMap;

    FACTORY_TYPE_N2(StreamHandlerFactory, XMLOutputStream,
        EventManager *, std::string);

    template <class T>
    static CreateFuncMap::value_type create_pair();

    // see: implementation
    static void create_map_init();

    static CreateFuncMap create_map_;
    static EventManager *evm_;
};

}

#endif
