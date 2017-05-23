/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_BFD_CLIENT_H_
#define SRC_BFD_CLIENT_H_

#include "bfd/bfd_common.h"

namespace BFD {

class Connection;
class Session;
class SessionConfig;

class Client {
public:
    Client(Connection *cm, ClientId client_id = 0);
    virtual ~Client();
    void AddConnection(const SessionKey &key, const SessionConfig &config);
    void DeleteConnection(const SessionKey &key);
    void DeleteClientConnections();
    bool Up(const SessionKey &key) const;
    Session *GetSession(const SessionKey &key) const;

private:
    void Notify(const SessionKey &key, const BFD::BFDState &new_state);

    ClientId id_;
    Connection *cm_;
};

}  // namespace BFD

#endif  // SRC_BFD_CLIENT_H_
