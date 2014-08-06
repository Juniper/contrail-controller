import testscenarios
import testtools
from vnc_openstack.neutron_plugin_db import DBInterface

load_tests = testscenarios.load_tests_apply_scenarios

class FakeRouteTable(object):
    def __init__(self, prefix, next_hop):
        self.prefix = prefix
        self.next_hop = next_hop

    def get_prefix(self):
        return self.prefix

    def get_next_hop(self):
        return self.next_hop

    def set_next_hop(self, next_hop):
        self.next_hop = next_hop

_SUBNET_CIDR = '10.0.0.0/24'

single_host_routes = []
single_host_routes.append(FakeRouteTable('16.0.0.0/24', '10.0.0.5'))
single_expected_route_dict = {}
single_expected_route_dict['10.0.0.5'] = ['16.0.0.0/24']

multiple_host_routes = []
multiple_host_routes.append(FakeRouteTable('16.0.0.0/24', '10.0.0.5'))
multiple_host_routes.append(FakeRouteTable('18.0.0.0/24', '10.0.0.13'))
multiple_host_routes.append(FakeRouteTable('20.0.0.0/24', '10.0.0.20'))
multiple_expected_route_dict = {}
multiple_expected_route_dict['10.0.0.5'] = ['16.0.0.0/24']
multiple_expected_route_dict['10.0.0.13'] = ['18.0.0.0/24']
multiple_expected_route_dict['10.0.0.20'] = ['20.0.0.0/24']

null_host_routes = []
null_host_routes.append(FakeRouteTable('16.0.0.0/24', '15.0.0.5'))
null_host_routes.append(FakeRouteTable('16.0.1.0/24', '17.0.0.9'))
null_host_routes.append(FakeRouteTable('18.0.1.0/24', '12.0.0.2'))
null_expected_route_dict = {}

indirect_host_routes = []
indirect_host_routes.append(FakeRouteTable('16.0.0.0/24', '14.0.0.5'))
indirect_host_routes.append(FakeRouteTable('12.0.0.0/24', '21.0.0.5'))
indirect_host_routes.append(FakeRouteTable('14.0.0.0/24', '12.0.0.6'))
indirect_host_routes.append(FakeRouteTable('18.0.0.0/24', '16.0.0.13'))
indirect_host_routes.append(FakeRouteTable('19.0.0.0/24', '11.0.0.5'))

indirect_host_routes.append(FakeRouteTable('21.0.0.0/24', '10.0.0.3'))
indirect_host_routes.append(FakeRouteTable('9.0.0.0/24', '10.0.0.4'))
indirect_host_routes.append(FakeRouteTable('11.0.0.0/24', '9.0.0.10'))
indirect_host_routes.append(FakeRouteTable('15.0.0.0/24', '10.0.0.4'))
indirect_host_routes.append(FakeRouteTable('22.0.0.0/24', '10.0.0.4'))
indirect_expected_route_dict = {}
indirect_expected_route_dict['10.0.0.3'] = ['21.0.0.0/24', '12.0.0.0/24',
                                            '14.0.0.0/24', '16.0.0.0/24',
                                            '18.0.0.0/24']
indirect_expected_route_dict['10.0.0.4'] = ['9.0.0.0/24', '15.0.0.0/24',
                                            '22.0.0.0/24', '11.0.0.0/24',
                                            '19.0.0.0/24']

class TestGetHostPrefixes(testtools.TestCase):
    scenarios = [
        ('empty', dict(hostroutes=[],
                       expected={})),
        ('single',
         dict(hostroutes=single_host_routes,
              expected=single_expected_route_dict)),
        ('multiple',
         dict(hostroutes=multiple_host_routes,
              expected=multiple_expected_route_dict)),
        ('null',
         dict(hostroutes=null_host_routes,
              expected=null_expected_route_dict)),
        ('indirect',
         dict(hostroutes=indirect_host_routes,
              expected=indirect_expected_route_dict)),
    ]
    def setUp(self):
        super(TestGetHostPrefixes, self).setUp()

    def tearDown(self):
        super(TestGetHostPrefixes, self).tearDown()

    def test_port_get_host_prefixes(self):
        def db_fake_init(self, admin_name, admin_password, admin_tenant_name,
                 api_srvr_ip, api_srvr_port, user_info=None,
                 contrail_extensions_enabled=True,
                 apply_subnet_host_routes=False):
            pass

        DBInterface.__init__ = db_fake_init
        dbiface = DBInterface("","","","","")
        host_route_dict = dbiface._port_get_host_prefixes(self.hostroutes,
                                                          _SUBNET_CIDR)
        self.assertEqual(host_route_dict, self.expected)
