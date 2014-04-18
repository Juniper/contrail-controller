#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Client test program, similar to nova-compute populating the port
# and using the Instance Services
#

import socket
import sys
import argparse

from thrift.transport import TTransport, TSocket
from thrift.protocol import TBinaryProtocol, TProtocol
import InstanceService
import ttypes

def main(args_str = None):
    socket = TSocket.TSocket("localhost", 9090)
    transport = TTransport.TFramedTransport(socket)
    transport.open()
    protocol = TBinaryProtocol.TBinaryProtocol(transport)

    service = InstanceService.Client(protocol)
    subnet1 = ttypes.Subnet("20.30.40.0", 24)
    subnet2 = ttypes.Subnet("21.31.41.0", 24)
    subnet3 = ttypes.Subnet("10.11.12.0", 24)
    subnet_list_1 = [ subnet1, subnet2 ]
    route_list_1 = [ ]
    subnet_list_2 = [ subnet3 ]
    route_list_2 = [ ttypes.Subnet("0.0.0.0", 0), ttypes.Subnet("8.8.8.0", 24), ttypes.Subnet("7.7.7.0", 24) ]
    gw1 = ttypes.VirtualGatewayRequest("gw_test1",  "default-domain:admin:test1:test1", subnet_list_1, route_list_1)
    gw2 = ttypes.VirtualGatewayRequest("gw_test2",  "default-domain:admin:test2:test2", subnet_list_2, route_list_2)
    gw_list = [ gw1, gw2 ] 
    gw_interface_list = [ "gw_test1", "gw_test2" ]

    try:
        #import pdb; pdb.set_trace()
        argc = len(sys.argv)
        if argc < 2:
            print "Usage : " + sys.argv[0] + " add / del"
            sys.exit()
        if sys.argv[1] != "del":
            service.AddVirtualGateway(gw_list)
        else:
            service.DeleteVirtualGateway(gw_interface_list)
    finally:
        transport.close()
#end main

if __name__ == "__main__":
    main()
