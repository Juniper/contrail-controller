//
// Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
//

#ifndef ZOOKEEPER_ZOOKEEPER_INTERFACE_H_
#define ZOOKEEPER_ZOOKEEPER_INTERFACE_H_

#include <zookeeper/zookeeper.h>

namespace zookeeper {
namespace interface {

class ZookeeperInterface {
 public:
    virtual ~ZookeeperInterface() {}
    virtual void ZooSetDebugLevel(ZooLogLevel logLevel) = 0;
    virtual zhandle_t* ZookeeperInit(const char *host, watcher_fn fn,
        int recv_timeout, const clientid_t *clientid, void *context,
        int flags) = 0;
    virtual int ZookeeperClose(zhandle_t *zh) = 0;
    virtual int ZooState(zhandle_t *zh) = 0;
    virtual int ZooCreate(zhandle_t *zh, const char *path, const char *value,
        int valuelen, const struct ACL_vector *acl, int flags,
        char *path_buffer, int path_buffer_len) = 0;
    virtual int ZooDelete(zhandle_t *zh, const char *path, int version) = 0;
    virtual int ZooGet(zhandle_t *zh, const char *path, int watch,
        char *buffer, int* buffer_len, struct Stat *stat) = 0;
};

} // namespace interface
} // namespace zookeeper

#endif // ZOOKEEPER_ZOOKEEPER_INTERFACE_H_
