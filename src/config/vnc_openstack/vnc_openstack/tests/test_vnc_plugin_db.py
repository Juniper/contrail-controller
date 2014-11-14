import unittest
from flexmock import flexmock

from vnc_openstack import neutron_plugin_db as db

class MockDbInterface(db.DBInterface):
    def __init__(self):
        pass

class TestDbInterface(unittest.TestCase):
    _tenant_ids = ['tenant_id_1',
                  'tenant_id_2']

    def _list_resource(self, resource, ret_count=0):
        def _list_others(parent_id, count):
            self.assertEqual(count, True)
            self.assertTrue(parent_id in self._tenant_ids)
            self.assertTrue(resource in ['virtual_networks',
                                'virtual_machine_interfaces',
                                'logical_routers',
                                'network_policys',
                                'network_ipams',
                                'route_tables'])
            r = resource.replace("_", "-")
            return {r: {'count': ret_count}}

        def _list_fip(back_ref_id, count):
            self.assertEqual(count, True)
            self.assertTrue(back_ref_id in self._tenant_ids)

            r = resource.replace("_", "-")
            return {r: {'count': ret_count}}

        if resource == "floating_ips":
            return _list_fip

        return _list_others

    def _test_for(self, resource):
        dbi = MockDbInterface()

        kwargs={"operational": True,
                resource + "_list": self._list_resource(resource, 1),
        }
        dbi._vnc_lib = flexmock(**kwargs)

        ret = dbi._resource_count_optimized(resource,
                                            filters={'tenant_id': self._tenant_ids[0]})
        self.assertEqual(ret, 1)

        ret = dbi._resource_count_optimized(resource,
                                            filters={'tenant_id': self._tenant_ids})
        self.assertEqual(ret, 2)

    def test_resource_count_optimized(self):
        dbi = MockDbInterface()

        ret = dbi._resource_count_optimized('virtual-networks',
                                            filters={'f': 'some-filter'})
        self.assertEqual(ret, None)

        ret = dbi._resource_count_optimized('virtual-networks',
                                            filters={'tenant_id': 'some-id',
                                                     'f': 'some_filter'})
        self.assertEqual(ret, None)

        self._test_for("virtual_networks")
        self._test_for("virtual_machine_interfaces")
        self._test_for("floating_ips")
        self._test_for("logical_routers")
        self._test_for("network_policys")
        self._test_for("network_ipams")
        self._test_for("route_tables")
