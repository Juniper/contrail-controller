/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_BFD_CLIENT_H_
#define SRC_BFD_CLIENT_H_

#include "bfd/bfd_common.h"

namespace BFD {

class Connection;
class Session;
struct SessionConfig;

class Client {
public:
    Client(Connection *cm, ClientId client_id = 0);
    virtual ~Client();
    void AddSession(const SessionKey &key, const SessionConfig &config);
    void DeleteSession(const SessionKey &key);
    void DeleteClientSessions();
    bool Up(const SessionKey &key) const;
    Session *GetSession(const SessionKey &key) const;
    Connection *GetConnection() { return cm_; }

private:
    void Notify(const SessionKey &key, const BFD::BFDState &new_state);

    ClientId id_;
    Connection *cm_;
};

}  // namespace BFD

#endif  // SRC_BFD_CLIENT_H_
