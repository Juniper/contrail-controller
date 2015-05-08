import unittest
import uuid

import bottle
import netaddr
from vnc_api import vnc_api
from vnc_openstack.tests.vnc_mock import MockVnc

INVALID_UUID = '00000000000000000000000000000000'


class TestBase(unittest.TestCase):
    def setUp(self):
        self._test_vnc_lib = MockVnc()

        self.domain_obj = vnc_api.Domain()
        self._test_vnc_lib.domain_create(self.domain_obj)

        self.proj_obj = self._project_create()

    def tearDown(self):
        pass

    def _project_create(self, name=None):
        proj_obj = vnc_api.Project(parent_obj=self.domain_obj, name=name)
        self._test_vnc_lib.project_create(proj_obj)
        return proj_obj

    def _generated(self):
        return 0xFF

    def _uuid_to_str(self, uuid_str):
        return str(uuid_str).replace('-', '')

    def _is_mac(self, mac_str):
        try:
            netaddr.EUI(mac_str)
        except Exception:
            return False
        return True

    def _is_uuid_or_mac(self, str):
        try:
            uuid.UUID(str)
        except ValueError:
            # check if its a mac address
            return self._is_mac(str)
        return True

    def _compare(self, verify, against):
        print(" -- Checking %s *** against *** %s" % (verify, against))
        if (isinstance(verify, dict) or isinstance(verify, list)) and (
                type(verify) != type(against)):
            return False

        if isinstance(verify, dict):
            return self._compare_dict(verify, against)
        elif isinstance(verify, list):
            return self._compare_list(verify, against)
        else:
            res = verify in [against, self._generated()]
            if not res:
                # just check if verify is uuid and against is 0xFF
                if (against == self._generated() and
                        self._is_uuid_or_mac(verify)):
                    res = True
            return res

    def _compare_list(self, verify, against):
        _against = list(against)
        if len(verify) != len(against):
            return False

        for v in verify:
            res = True
            for index, a in enumerate(_against):
                res = self._compare(v, a)
                if res:
                    break
            if not res:
                return False

            if index < len(_against):
                _against.pop(index)
            else:
                return False

        return True

    def _compare_dict(self, verify, against):
        if not verify and not against:
            return True

        for k, v in against.iteritems():
            if (k in verify):
                if not self._compare(verify[k], v):
                    return False
            else:
                return False
        return True

    def _test_check(self, _handler_method, test_entries):
        for entry in test_entries:
            if type(entry['output']) == type and (
                    issubclass(entry['output'], Exception)):
                with self.assertRaises(entry['output']):
                    _handler_method(entry['input'])
            else:
                ret = _handler_method(entry['input'])
                self.assertTrue(self._compare(ret, entry['output']))

    def _test_failures_on_create_invalid_id(self, create_dict,
                                            invalidid_param):
        _q = create_dict.copy()
        _q[invalidid_param] = INVALID_UUID

        with self.assertRaises(bottle.HTTPError):
            self._handler.resource_create(_q)

    def _test_failures_on_create(self, null_entry=False,
                                 invalid_tenant=False):
        if null_entry:
            # test with empty dict
            with self.assertRaises(bottle.HTTPError):
                self._handler.resource_create({})

        if invalid_tenant:
            self._test_failures_on_create_invalid_id({}, 'tenant_id')

    def _test_check_create(self, entries):
        def _pre_handler(inp):
            return self._handler.resource_create(**inp)
        return self._test_check(_pre_handler, entries)

    def _test_check_update(self, entries):
        def _pre_handler(inp):
            return self._handler.resource_update(**inp)
        return self._test_check(_pre_handler, entries)

    def _test_failures_on_list(self, invalid_tenant=False):
        filters = {'tenant_id': '00000000000000000000000000000000'}
        ret = self._handler.resource_list(context=None, filters=filters)
        self.assertEqual(ret, [])

    def _test_check_list(self, entries):
        def _pre_handler(inp):
            return self._handler.resource_list(**inp)
        return self._test_check(_pre_handler, entries)

    def _test_check_count(self, entries):
        def _pre_handler(inp):
            return self._handler.resource_count(**inp)
        return self._test_check(_pre_handler, entries)

    def _test_failures_on_get(self):
        # input a invalid uuid and check for exception
        with self.assertRaises(bottle.HTTPError):
            self._handler.resource_get(
                str(uuid.UUID('00000000000000000000000000000000')))

    def _test_failures_on_update(self):
        # input a invalid uuid and check for exception
        with self.assertRaises(bottle.HTTPError):
            self._handler.resource_update(
                str(uuid.UUID('00000000000000000000000000000000')), {})

    def _test_failures_on_delete(self, id=None):
        # input a invalid uuid and check for exception
        id = (str(uuid.UUID('00000000000000000000000000000000'))
              if not id else id)
        with self.assertRaises(bottle.HTTPError):
            self._handler.resource_delete(id)

    def _test_check_get(self, entries):
        def _pre_handler(inp):
            if type(inp) != dict:
                return self._handler.resource_get(inp)
            else:
                return self._handler.resource_get(**inp)
        return self._test_check(_pre_handler, entries)

    def _test_check_delete(self, entries):
        def _pre_handler(inp):
            return self._handler.resource_delete(**inp)
        return self._test_check(_pre_handler, entries)
