#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#import pdb; pdb.set_trace()

import contextlib
import datetime
import errno
import functools
import hashlib
import inspect
import os
import pyclbr
import random
import re
import shlex
import shutil
import signal
import socket
import struct
import sys
import tempfile
import time
import uuid
import weakref
from xml.sax import saxutils
import threading

from eventlet import event
from eventlet.green import subprocess
from eventlet import greenthread
from eventlet import semaphore
import netaddr

#from nova.common import deprecated
#from nova import exception
#from nova import flags
#from nova.openstack.common import cfg
#from nova.openstack.common import excutils
#from nova.openstack.common import importutils
#from nova.openstack.common import log as logging
#from nova.openstack.common import timeutils

import InstanceService
import ttypes

class LoopingCallDone(Exception):
    """Exception to break out and stop a LoopingCall.

    The poll-function passed to LoopingCall can raise this exception to
    break out of the loop normally. This is somewhat analogous to
    StopIteration.

    An optional return-value can be included as the argument to the exception;
    this return-value will be returned by LoopingCall.wait()

    """

    def __init__(self, retvalue=True):
        """:param retvalue: Value that LoopingCall.wait() should return."""
        self.retvalue = retvalue


class LoopingCall(object):
    def __init__(self, f=None, *args, **kw):
        self.args = args
        self.kw = kw
        self.f = f
        self._running = False

    def start(self, interval, initial_delay=None):
        self._running = True
        done = event.Event()

        def _inner():
            if initial_delay:
                greenthread.sleep(initial_delay)

            try:
                while self._running:
                    self.f(*self.args, **self.kw)
                    if not self._running:
                        break
                    greenthread.sleep(interval)
            except LoopingCallDone, e:
                self.stop()
                done.send(e.retvalue)
            except Exception:
                LOG.exception(_('in looping call'))
                done.send_exception(*sys.exc_info())
                return
            else:
                done.send(True)

        self.done = done

        greenthread.spawn(_inner)
        return self.done

    def stop(self):
        self._running = False

    def wait(self):
        return self.done.wait()


port1 = ttypes.Port([0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xe,0xf],
                   [0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xf,0xe],
                   "/dev/vnet0",
                   "20.20.20.20",
                   [0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xf,0xe,0xf],
                   "00:00:00:00:00:01")

port2 = ttypes.Port([0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0x1,0x2],
                   [0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0x2,0x1],
                   "/dev/vnet1",
                   "20.20.20.30",
                   [0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xf,0xe,0xf],
                   "00:00:00:00:00:02")

port3 = ttypes.Port([0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0x1,0x2],
                   [0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0x2,0x1],
                   "/dev/vnet1",
                   "20.20.20.30",
                   [0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xf,0xe,0xf],
                   "00:00:00:00:00:03")

#import pdb; pdb.set_trace()

port_dict = {}
#port_dict[port1.port_id] = port1
#port_dict[port2.port_id] = port2
#port_dict[port3.port_id] = port3
#port_list = [v for k, v in port_dic.iteritems()]
port_list = [port1, port2, port3]

def agent_conn_open():
    import socket
    import sys
    from thrift.transport import TTransport, TSocket
    from thrift.protocol import TBinaryProtocol, TProtocol
    from thrift.transport.TTransport import TTransportException
    try:
        socket = TSocket.TSocket("localhost", 9090)
        transport = TTransport.TFramedTransport(socket)
        transport.open()
        protocol = TBinaryProtocol.TBinaryProtocol(transport)
        return protocol
    except TTransportException:
            return None
#end agent_conn_open

transport = None
protocol = agent_conn_open()
result = False

def keep_alive():
    try:
        global result
        global protocol
        print("keep alive", result)
        if result == False:
            #import pdb; pdb.set_trace()
            protocol = agent_conn_open()
            if protocol == None:
                return

        service = InstanceService.Client(protocol)
        result_l = service.KeepAliveCheck()
        if result == False and result_l == True:
            print("Send the list")
            global port_list
            service.AddPort(port_list)
            result = True
            return

        if result == True and result_l == False:
            print("Agent not available")
            result = False
            return

    except:
        result = False
        print("Exception", result)
#end keep_alive

try:
    timer = LoopingCall(keep_alive)
    timer.start(interval=5).wait()

finally:
    if transport:
        transport.close()
