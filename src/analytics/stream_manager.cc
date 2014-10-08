/*
 *  Copyright (c) 2014 Codilime
 */

#include "analytics/stream_manager.h"
#include "analytics/stream_handler.h"

#include <sys/types.h>
#include <dirent.h>
#include <vector>
#include <tbb/reader_writer_lock.h>
#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <sandesh/sandesh_message_builder.h>
#include "analytics/stream_handler_factory.h"

using analytics::OutputStreamManager;
using analytics::OutputStreamHandler;
using analytics::SandeshStreamData;

SandeshStreamData::SandeshStreamData(const SandeshXMLMessage &msg)
: header(msg.GetHeader()) {
      pugi::xml_node message_xmlnode = xml_doc.append_child();
      message_xmlnode.set_name("sandesh");
      // make a deep copy of the document
      message_xmlnode.append_copy(msg.GetMessageNode());
}

void OutputStreamManager::addHandler(OutputStreamHandler *ptr) {
    output_handlers_.push_back(ptr);
}

void OutputStreamManager::append(const SandeshXMLMessage *msg) {
    boost::shared_ptr<const struct SandeshStreamData> shptr(
        new struct SandeshStreamData(*msg));
    BOOST_FOREACH(OutputStreamHandler &phandler, output_handlers_) {
        phandler.Enqueue(shptr);
    }
}

bool OutputStreamManager::INIConfigReader::Configure(std::string config_dir_path) {
    // originally written w/ boost-filesystem, had to rewrite because
    // it was exception-reliant
    DIR *config_directory = opendir(config_dir_path.c_str());
    struct dirent *entry;

    bool completed_successfully = true;
    if (config_directory != NULL) {
        while ((entry = readdir(config_directory))) {
            const char *extension = entry->d_name;
            for (const char *next = extension;
                (next = strchr(extension, '.')) != NULL;
                extension = next+1);
            if (strcmp("conf", extension))
                continue;

            boost::property_tree::ptree pt;
            boost::property_tree::ini_parser::read_ini(entry->d_name, pt);
            if (!Configure(pt))
                completed_successfully = false;
        }
    } else {
        return false;
    }
    return completed_successfully;
}

bool OutputStreamManager::INIConfigReader::Configure(
        OutputStreamHandler::StreamHandlerConfig &config) {
    // Implementation for boost::ptree. See StreamHandlerConfig's TODO.
    using boost::property_tree::ptree;
    ptree &config_tree = config;

    // Every output stream/plugin has to define its type (implementation) and
    // unique name.
    std::string stream_type = config_tree.get<std::string>("stream.type");
    std::string stream_name = config_tree.get<std::string>("stream.unique_name");
    // The rest is up to particular implementation.
    OutputStreamHandler *handler =
        StreamHandlerFactory::Create(stream_type, stream_name);
    if (handler == NULL)
        return false;
    if (!handler->Configure(config)) {
        delete handler;
        return false;
    }
    target_->addHandler(handler);
    return true;
}
