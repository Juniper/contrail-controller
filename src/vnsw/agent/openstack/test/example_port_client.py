#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# 
# Client test program, similar to nova-compute populating the port
# and using the Instance Services
#

import socket
import sys
import threading

from thrift.transport import TTransport, TSocket
from thrift.protocol import TBinaryProtocol, TProtocol
import InstanceService
import ttypes

port = ttypes.Port([0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xe,0xf],
                   [0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xf,0xe],
                   "/dev/vnet0",
                   "20.20.20.20",
                   [0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xf,0xe,0xf],
                   "00:00:00:00:00:02")

socket = TSocket.TSocket("localhost", 9090)
transport = TTransport.TFramedTransport(socket)
transport.open()
protocol = TBinaryProtocol.TBinaryProtocol(transport)

service = InstanceService.Client(protocol)


def KeepAlive():
    service.AddPort(port)
    
try:
    #import pdb; pdb.set_trace()
    #service.AddPort(port)
    t = threading.Timer(10, KeepAlive)
    t.start()
    #service.DeletePort([0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xe,0xf])
    #service.DeletePort([0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xe,0xf])
finally:
    transport.close()
