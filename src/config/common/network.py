#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from quantumclient.quantum import client
from quantumclient.client import HTTPClient
from quantumclient.common import exceptions


class CommonQuantumClient(object):

    def __init__(self, project, user, passwd, api_server_ip):
        AUTH_URL = 'http://%s:5000/v2.0' % (api_server_ip)
        httpclient = HTTPClient(username=user, tenant_name=project,
                                password=passwd, auth_url=AUTH_URL)
        httpclient.authenticate()

        OS_URL = 'http://%s:9696/' % (api_server_ip)
        OS_TOKEN = httpclient.auth_token
        self._quantum = client.Client('2.0', endpoint_url=OS_URL,
                                      token=OS_TOKEN)
    # end __init__

    def common_create_vn(self, vn_name):
        print "Creating network %s" % (vn_name)
        net_req = {'name': '%s' % (vn_name)}
        net_rsp = self._quantum.create_network({'network': net_req})
        net_uuid = net_rsp['network']['id']
        return net_rsp
    # end common_create_vn

    def common_create_subnet(self, net_id, cidr, ipam_fq_name):
        subnet_req = {'network_id': net_id,
                      'cidr': cidr,
                      'ip_version': 4,
                      'contrail:ipam_fq_name': ipam_fq_name}
        subnet_rsp = self._quantum.create_subnet({'subnet': subnet_req})
        print 'Response for create_subnet : ' + repr(subnet_rsp)
    # end _common_create_subnet
# end class CommonQuantumClient
