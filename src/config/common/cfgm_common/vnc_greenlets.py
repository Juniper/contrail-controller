from __future__ import unicode_literals
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import socket
from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from cfgm_common.uve.greenlets import ttypes as sandesh
import gc
import gevent
import traceback
from greenlet import greenlet
from gevent import Greenlet


class VncGreenlet(Greenlet):
    def __init__(self, module_name, handler):
        Greenlet.__init__(self, self.super_greenlet_handler,
                          handler, module_name)
        self.start()
    # end __init_

    @staticmethod
    def register_sandesh_handler():
        sandesh.GreenletObjectReq.handle_request = \
            VncGreenlet.sandesh_greenlet_object_handle_request

    def super_greenlet_handler(self, greenlet_handler, module_name):
        # Create a flag/variable local to the greenlet so that
        # it can be identified as a "well known" greenlet.
        gevent.getcurrent().greenlet_name = module_name
        if callable(greenlet_handler):
            greenlet_handler()

    @staticmethod
    def build_greenlet_sandesh(name, traces):
        sandesh_greenlet = sandesh.GreenletObject()
        sandesh_greenlet.greenlet_name = name
        sandesh_greenlet.greenlet_traces = traces
        return sandesh_greenlet

    @classmethod
    def sandesh_greenlet_object_handle_request(cls, req):
        greenlet_resp = sandesh.GreenletObjectListResp(greenlets=[])
        anonymous_cnt = 0
        for obj in gc.get_objects():
            if obj is None:
                continue
            if not isinstance(obj, greenlet):
                continue

            # If the GC object is an instance of greenlet,
            # but does not have the flag/variable 'greenlet_name'
            # then try to find the run function.
            if not hasattr(obj, 'greenlet_name'):
                try:
                    name = obj._run.__name__
                except AttributeError:
                    name = 'Anonymous'
            else:
                name = obj.greenlet_name

            if (req.greenlet_name is None or
               req.greenlet_name == name):
                sandesh_greenlet = VncGreenlet.build_greenlet_sandesh(
                                 name,
                                 ''.join(traceback.format_stack(obj.gr_frame)))
                greenlet_resp.greenlets.append(sandesh_greenlet)

        greenlet_resp.response(req.context())
    # end sandesh_greenlet_object_handle_request
# end class VncGreenlet
