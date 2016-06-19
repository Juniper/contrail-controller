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
    DbHandlerMock(EventManager *evm, const TtlMap& ttl_map) :
        DbHandler(evm,  boost::bind(&DbHandlerMock::StartDbifReinit, this),
            std::vector<std::string>(1, "127.0.0.1"),
            std::vector<int>(1, 9160), "localhost", ttl_map, "", "",
            "", false)
    {
    }
    void StartDbifReinit() {
        UnInit(-1);
    }
    MOCK_METHOD2(MessageTableInsert, void(const VizMsg *vmsgp,
        GenDb::GenDbIf::DbAddColumnCb db_cb));
};
#endif//__DH_HANDLER_MOCK_H__
