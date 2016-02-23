//
// Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
//

#ifndef ZOOKEEPER_ZOOKEEPER_CLIENT_H_
#define ZOOKEEPER_ZOOKEEPER_CLIENT_H_

class ZookeeperClientTest;

namespace zookeeper {
namespace client {

// Forward declarations
namespace impl {
class ZookeeperClientImpl;
} // namespace impl

//
// Blocking, synchronous, non-thread safe Zookeeper client
//
class ZookeeperClient {
 public:
    ZookeeperClient(const char *hostname, const char *servers);
    virtual ~ZookeeperClient();

 private:
    ZookeeperClient(impl::ZookeeperClientImpl *impl);

    friend class ZookeeperLock;
    friend class ::ZookeeperClientTest;

    std::auto_ptr<impl::ZookeeperClientImpl> impl_;
};

//
// Usage is to first create a ZookeeperClient, and then ZookeeperLock
// for distributed synchronization
//
class ZookeeperLock {
 public:
    ZookeeperLock(ZookeeperClient *client, const char *path);
    virtual ~ZookeeperLock();

    bool Lock();
    bool Release();

 private:
    class ZookeeperLockImpl;
    friend class ::ZookeeperClientTest;

    std::string Id() const;

    std::auto_ptr<ZookeeperLockImpl> impl_;
};

} // namespace client
} // namespace zookeeper

#endif // ZOOKEEPER_ZOOKEEPER_CLIENT_H_
