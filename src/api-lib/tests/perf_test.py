#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
from gevent import monkey; monkey.patch_all()
import sys

import vnc_api

conn = vnc_api.Connection('user1', 'password1', 'tenant1')

vpc1_id = conn.vpc_create("vpc1")

def do_vn_create(vn_num):
    conn.vn_create(vpc1_id, "vn%s" %(i))

jobs = [gevent.spawn(do_vn_create, i) for i in range(600)]

gevent.joinall(jobs)
