import bottle
from vnc_openstack import sg_res_handler as sg_handler
from vnc_openstack import sgrule_res_handler as sgrule_handler
from vnc_openstack.tests import test_common


class TestSecurityGroupRuleHandlers(test_common.TestBase):
    def setUp(self):
        super(TestSecurityGroupRuleHandlers, self).setUp()
        self._handler = sgrule_handler.SecurityGroupRuleHandler(
            self._test_vnc_lib)

    def _create_test_sg(self, name, proj):
        sg_q = {
            'tenant_id': self._uuid_to_str(proj.uuid),
            'name': name,
        }

        res = sg_handler.SecurityGroupHandler(
            self._test_vnc_lib).resource_create(sg_q)
        return res['id']

    def _create_sgrule(self, sg_uuid, protocol, port_range,
                       remote_group_id, remote_ip_prefix,
                       direction, ethertype='IPv4'):
        sgr_q = {
            'protocol': protocol,
            'port_range_min': port_range[0],
            'port_range_max': port_range[1],
            'remote_group_id': remote_group_id,
            'remote_ip_prefix': remote_ip_prefix,
            'direction': direction,
            'ethertype': ethertype,
            'security_group_id': sg_uuid
        }
        res = self._handler.resource_create(sgr_q)
        return res['id']

    def test_create(self):
        sg_uuid = self._create_test_sg('test-sg', self.proj_obj)
        remote_sg_uuid = self._create_test_sg('test-sg-1', self.proj_obj)
        entries = [{
            'input': {
                'sgr_q': {
                    'protocol': 'INVALID',
                }
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'sgr_q': {
                    'protocol': 256
                }
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'sgr_q': {
                    'protocol': 'icmp',
                    'port_range_min': 256,
                    'port_range_max': 256
                }
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'sgr_q': {
                    'protocol': 'icmp',
                    'port_range_max': 25,
                    'port_range_min': None
                }
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'sgr_q': {
                    'protocol': 'udp',
                    'port_range_min': 250,
                    'port_range_max': 25
                }
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'sgr_q': {
                    'protocol': None,
                    'port_range_min': 20,
                    'port_range_max': 25
                }
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'sgr_q': {
                    'protocol': 'tcp',
                    'port_range_min': 20,
                    'port_range_max': 20,
                    'remote_ip_prefix': '192.168.1.0/24',
                    'ethertype': 'IPv4',
                    'direction': 'ingress',
                    'security_group_id': test_common.INVALID_UUID
                },
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'sgr_q': {
                    'protocol': 'tcp',
                    'port_range_min': None,
                    'port_range_max': None,
                    'remote_ip_prefix': '192.168.1.0/24',
                    'ethertype': 'IPv4',
                    'direction': 'ingress',
                    'security_group_id': sg_uuid
                },
            },
            'output': {
                'remote_group_id': None,
                'direction': 'ingress',
                'remote_ip_prefix': '192.168.1.0/24',
                'protocol': 'tcp',
                'ethertype': 'IPv4',
                'port_range_max': 65535,
                'security_group_id': sg_uuid,
                'port_range_min': 0
            }
        }, {
            'input': {
                'sgr_q': {
                    'protocol': 17,
                    'port_range_min': None,
                    'port_range_max': None,
                    'remote_ip_prefix': None,
                    'remote_group_id': remote_sg_uuid,
                    'ethertype': 'IPv4',
                    'direction': 'egress',
                    'security_group_id': sg_uuid
                },
            },
            'output': {
                'remote_group_id': remote_sg_uuid,
                'direction': 'egress',
                'remote_ip_prefix': None,
                'protocol': 'udp',
                'ethertype': 'IPv4',
                'port_range_max': 65535,
                'security_group_id': sg_uuid,
                'port_range_min': 0
            }
        }, {
            'input': {
                'sgr_q': {
                    'protocol': None,
                    'port_range_min': None,
                    'port_range_max': None,
                    'remote_ip_prefix': None,
                    'remote_group_id': None,
                    'ethertype': None,
                    'direction': None,
                    'security_group_id': sg_uuid
                },
            },
            'output': {
                'remote_group_id': None,
                'direction': 'egress',
                'remote_ip_prefix': None,
                'protocol': 'any',
                'ethertype': 'IPv4',
                'port_range_max': 65535,
                'security_group_id': sg_uuid,
                'port_range_min': 0
            }
        }]
        self._test_check_create(entries)

    def test_delete(self):
        sg_uuid = self._create_test_sg('test-sg', self.proj_obj)
        sgrule_uuid = self._create_sgrule(sg_uuid, protocol='any',
                                          port_range=(0, 0),
                                          remote_group_id=None,
                                          remote_ip_prefix=None,
                                          direction=None)
        entries = [{
            'input': {
                'context': {
                    'tenant_id': self.proj_obj.uuid,
                    'is_admin': False
                },
                'sgr_id': test_common.INVALID_UUID
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'context': {
                    'is_admin': True
                },
                'sgr_id': sgrule_uuid
            },
            'output': None
        }]
        self._test_check_delete(entries)

    def test_list(self):
        sg_uuid = self._create_test_sg('test-sg', self.proj_obj)
        sgrule_uuid = self._create_sgrule(sg_uuid, protocol='any',
                                          port_range=(0, 0),
                                          remote_group_id=None,
                                          remote_ip_prefix=None,
                                          direction=None)

        entries = [{
            'input': {
                'context': None,
                'filters': {'tenant_id': [self.proj_obj.uuid]}
            },
            'output': [{
                'id': sgrule_uuid,
                'remote_ip_prefix': None
            }, {
                'id': self._generated(),
                'remote_ip_prefix': '0.0.0.0/0'
            }]
        }, {
            'input': {
                'context': {
                    'tenant': self._uuid_to_str(self.proj_obj.uuid),
                    'is_admin': False
                },
                'filters': {
                    'id': [test_common.INVALID_UUID]
                }
            },
            'output': []
        }]
        self._test_check_list(entries)

    def test_get(self):
        sg_uuid = self._create_test_sg('test-sg', self.proj_obj)
        sgrule_uuid = self._create_sgrule(sg_uuid, protocol='any',
                                          port_range=(0, 0),
                                          remote_group_id=None,
                                          remote_ip_prefix=None,
                                          direction=None)

        entries = [{
            'input': {
                'context': {
                    'is_admin': False,
                    'tenant_id': self.proj_obj.uuid
                },
                'sgr_id': test_common.INVALID_UUID
            },
            'output': bottle.HTTPError
        }, {
            'input': {
                'context': {
                    'is_admin': False,
                    'tenant_id': self.proj_obj.uuid
                },
                'sgr_id': sgrule_uuid
            },
            'output': {
                'id': sgrule_uuid
            }
        }]
        self._test_check_get(entries)
