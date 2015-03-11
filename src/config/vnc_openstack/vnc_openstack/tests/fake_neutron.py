#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import sys

from flexmock import flexmock


# Mocking neutron.common.constants
mock_neutron_pkg = flexmock(name='mock_neutron')
mock_common_pkg = flexmock(name='mock_common')
mock_neutron_pkg.common = mock_common_pkg
mock_constants_mod = flexmock(name='mock_constant')
mod_attrs = {
             'PROTO_NUM_TCP' : 6,
             'PROTO_NAME_TCP' : 'tcp',
             'PROTO_NUM_UDP' : 17,
             'PROTO_NAME_UDP' : 'udp',
             'PROTO_NUM_ICMP' : 1,
             'PROTO_NAME_ICMP' : 'icmp',
             'NET_STATUS_ACTIVE' : 'ACTIVE',
             'NET_STATUS_DOWN' : 'DOWN',
             'PORT_STATUS_ACTIVE' : 'ACTIVE',
             'PORT_STATUS_DOWN' : 'DOWN',
             'DEVICE_OWNER_ROUTER_GW' : 'network:router_gateway',
             'DEVICE_OWNER_ROUTER_INTF' : 'network:router_interface',
             }
for attr, value in mod_attrs.items():
    mock_constants_mod.__setattr__(attr, value)
mock_common_pkg.constants = mock_constants_mod
sys.modules['neutron'] = mock_neutron_pkg
sys.modules['neutron.common'] = mock_common_pkg

# Mocking neutron.common.exceptions
class MockNetworkNotFound(Exception):
    pass


mock_exceptions_mod = flexmock(name='mock_exceptions')
mock_exceptions_mod.NetworkNotFound = MockNetworkNotFound
mock_common_pkg.exceptions = mock_exceptions_mod

#Mocking neutron.api.v2.attributes
mock_api_pkg = flexmock(name='mock_api')
mock_neutron_pkg.api = mock_api_pkg
mock_v2_pkg = flexmock(name='mock_v2')
mock_api_pkg.v2 = mock_v2_pkg
mock_attributes_mod = flexmock(name='mock_attributes')
mock_attributes_mod.__setattr__('ATTR_NOT_SPECIFIED', object())
mock_v2_pkg.attributes = mock_attributes_mod
sys.modules['neutron.api'] = mock_api_pkg
sys.modules['neutron.api.v2'] = mock_v2_pkg
