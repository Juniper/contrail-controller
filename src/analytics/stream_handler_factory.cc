/*
 * Copyright (c) 2014 Codilime
 */

#include <boost/assign/list_of.hpp>
#include "base/factory.h"
#include "analytics/stream_handler_factory.h"
#include "analytics/xml_output_stream.h"

using analytics::OutputStreamHandler;
using analytics::StreamHandlerFactory;
using analytics::XMLOutputStream;

template <>
StreamHandlerFactory *Factory<StreamHandlerFactory>::singleton_ = NULL;

template<class T>
StreamHandlerFactory::CreateFuncMap::value_type
            StreamHandlerFactory::create_pair() {
    return StreamHandlerFactory::CreateFuncMap::value_type(
        std::string(T::handler_name()),
        (StreamHandlerFactory::CreateFunc)(&StreamHandlerFactory::Create<T>));
}

FACTORY_STATIC_REGISTER(StreamHandlerFactory, XMLOutputStream, XMLOutputStream);

/* 
 * We've got to support old boost versions. It can be uncommented after
 * dropping support for 12.04.
 *
 *   StreamHandlerFactory::CreateFuncMap StreamHandlerFactory::create_map_ =
 *       boost::assign::list_of<StreamHandlerFactory::CreateFuncMap::value_type>
 *           (create_pair<XMLOutputStream>());
 *
 * Meanwhile...
 */
StreamHandlerFactory::CreateFuncMap StreamHandlerFactory::create_map_;
void StreamHandlerFactory::create_map_init() {
    static bool done = false;
    if (!done) {
        done = true;
        create_map_.insert(create_pair<XMLOutputStream>());
    }
}

EventManager *StreamHandlerFactory::evm_ = NULL;

OutputStreamHandler *StreamHandlerFactory::Create(std::string type_name,
                                                std::string unique_id) {
    create_map_init();

    CreateFuncMap::iterator create_pair = create_map_.find(type_name);
    if (create_pair == create_map_.end())
        return NULL;
    return (create_pair->second)(evm_, unique_id);
}
