#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

import grpc.experimental.gevent as grpc_gevent
grpc_gevent.init_gevent()
