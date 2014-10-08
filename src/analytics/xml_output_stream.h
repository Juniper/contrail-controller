/*
 * Copyright (c) 2014 Codilime.
 */

#ifndef SRC_XML_OUTPUT_STREAM_
#define SRC_XML_OUTPUT_STREAM_

#include "analytics/tcp_output_stream.h"

namespace analytics {

class XMLOutputStream : public TCPOutputStream {
 public:
    static const char *handler_name() { return "XMLOutputStream"; }

    XMLOutputStream(EventManager *evm, std::string unique_name)
    : TCPOutputStream(evm, unique_name) {}

    virtual bool ProcessMessage(const SandeshStreamData &);
};

}

#endif
