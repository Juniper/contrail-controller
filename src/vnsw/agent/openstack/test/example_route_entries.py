#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# 
# Client test program, similar to nova-compute populating the port
# and using the Instance Services
#

import socket
import sys

from thrift.transport import TTransport, TSocket
from thrift.protocol import TBinaryProtocol, TProtocol
import InstanceService
import ttypes

socket = TSocket.TSocket("localhost", 9090)
transport = TTransport.TFramedTransport(socket)
transport.open()
protocol = TBinaryProtocol.TBinaryProtocol(transport)

service = InstanceService.Client(protocol)
try:
    #import pdb; pdb.set_trace()
    #service.AddHostRoute("10.1.2.200", "");
    service.CreateVrf("test");
    #service.AddHostRoute("10.1.2.202", "00000000000000000000000000000001");

    service.AddLocalVmRoute("1.1.1.20", "00010203-0405-0607-0809-0a0b0c0d0e0f", "test", "1000");
   # service.AddLocalVmRoute("1.1.1.20", "00000000000000000000000000000001", 
   #                         "00000000000000000000000000000001", "0");

   # service.AddRemoteVmRoute("1.1.1.20", "10.1.2.220", "", "");
   # service.AddRemoteVmRoute("1.1.1.20", "10.1.2.220",
   #                         "00000000000000000000000000000001", "0");

   # service.TunnelNHEntryAdd("10.1.2.187", "10.1.2.191", "00000000000000000000000000000001");
   # service.RouteEntryAdd("1.1.1.17", "10.1.2.191", "00000000000000000000000000000001", "0");

   # service.CreateVrf("00000000000000000000000000000002", "");
   # service.ConfigFabricVrf("00000000000000000000000000000002");
finally:
    transport.close()
