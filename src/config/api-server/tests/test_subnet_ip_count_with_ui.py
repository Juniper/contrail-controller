#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from vnc_api.vnc_api import *
import uuid

def all (ip='10.84.9.45', port=8082, domain_name='my-domain',
        proj_name='admin', subnet1='192.168.1.0', subnet2='10.10.1.0',
        prefix1=30, prefix2=29, vn_name='vn1', compute_node='a3s45.contrail.juniper.net'):
    vnc_lib = VncApi(username='admin', password='contrail123',
        tenant_name='admin', api_server_host = ip, api_server_port = port)

    # This test needs VN to be creeated using Horizon and then create
    # instances to get ip address from various IP Block in this VN
    # then copy vn's uunid in the next call. This uuid is for example
    print 'Read Virtual Network object '
    net_obj = vnc_lib.virtual_network_read(id = '58398587-5747-475e-b394-583187eeb930')
    print 'Read no of instance ip for each subnet ["192.168.1.0/30", "10.10.1.0/29"]'
    subnet_list = ["192.168.1.0/30", "10.10.1.0/29"]
    result = vnc_lib.virtual_network_subnet_ip_count(net_obj, subnet_list)
    print result
    # compare result with created instances

if __name__ == '__main__':
    all ()
