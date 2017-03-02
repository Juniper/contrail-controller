/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DH_HANDLER_MOCK_H__
#define __DH_HANDLER_MOCK_H__

#include "sandesh/sandesh.h"
#include "db_handler.h"
#include <boost/bind.hpp>

class DbHandlerMock : public DbHandler {
  public:
    DbHandlerMock(EventManager *evm, const Options::Cassandra cassandra_options) :
        DbHandler(evm,  boost::bind(&DbHandlerMock::StartDbifReinit, this),
            "localhost",
            cassandra_options, "",
            false, false,
            DbWriteOptions(),
            ConfigDBConnection::ApiServerList(),
            VncApiConfig()) {

    }
    void StartDbifReinit() {
        UnInit();
    }
    MOCK_METHOD2(MessageTableInsert, void(const VizMsg *vmsgp,
        GenDb::GenDbIf::DbAddColumnCb db_cb));
};
#endif//__DH_HANDLER_MOCK_H__
