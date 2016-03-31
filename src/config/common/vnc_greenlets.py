#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import socket
from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from cfgm_common.uve.greenlets.ttypes import (Sandesh_GreenletObjectReq,
                                              Sandesh_GreenletObjectListResp,
                                              Sandesh_GreenletObject)

import gc
import gevent
import traceback
from greenlet import greenlet

def launch_greenlet(greenlet_handler, module_name):
    return VncGreenlet(greenlet_handler, module_name)._greenlet_handle

class VncGreenlet(object):
    def __init__(self, module_name, handler):

        Sandesh_GreenletObjectReq.handle_request = \
            staticmethod (self.sandesh_greenlet_object_handle_request)

        # spawn a Greenlet object with the passed handler fn.
        self._greenlet_handle = gevent.spawn(self.super_greenlet_handler, handler, module_name)
    # end __init_

    def super_greenlet_handler(self, greenlet_handler, module_name):
        # Create a flag/variable name local to the greenlet so that 
        # it can be identified as a "well known" greenlet.
        gevent.getcurrent().greenlet_name = module_name
        if callable(greenlet_handler):
            greenlet_handler()

    def build_greenlet_sandesh(self, name, traces, count = 1):
        sandesh_greenlet = Sandesh_GreenletObject()
        sandesh_greenlet.greenlet_name = name
        sandesh_greenlet.count = count
        sandesh_greenlet.greenlet_traces = traces
        return sandesh_greenlet 

    def sandesh_greenlet_object_handle_request(self, req):
        greenlet_resp = Sandesh_GreenletObjectListResp(greenlets=[])
        anonymous_cnt = 0
        for obj in gc.get_objects():
            if not obj:
                continue
            if not isinstance(obj, greenlet):
                continue

            # If the GC object is an instance of greenlet,
            # but does not have the flag/variable 'greenlet_nbame'
            # then it is not well known, hence count it as anonymous.
            if not hasattr(obj, 'greenlet_name'):
                anonymous_cnt = anonymous_cnt + 1
                continue
            
            sandesh_greenlet = self.build_greenlet_sandesh(obj.greenlet_name,\
                                 ''.join(traceback.format_stack(obj.gr_frame)))
            greenlet_resp.greenlets.append(sandesh_greenlet)

        if anonymous_cnt > 0:
            greenlet_resp.greenlets.append(self.build_greenlet_sandesh('Anonymous',\
                                           'Not Applicable', anonymous_cnt))
        greenlet_resp.response(req.context())

# end class VncGreenlet
