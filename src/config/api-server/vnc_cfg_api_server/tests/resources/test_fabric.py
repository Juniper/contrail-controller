#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#

import logging

from vnc_api.gen.resource_client import Fabric

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestFabric(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFabric, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFabric, cls).tearDownClass(*args, **kwargs)

    def _create_fabric(self, fabric_name):
        # create test fabric object
        fabric = Fabric(
            name=fabric_name,
            fq_name=["default-global-system-config", fabric_name],
            parent_type='global-system-config',
            fabric_credentials={
                'device_credential': [
                    {
                        'credential': {
                            'username': "root",
                            'password': "Embe1mpls"
                        }
                    }
                ]
            }
        )
        fabric_uuid = self._vnc_lib.fabric_create(fabric)
        fabric_obj = self._vnc_lib.fabric_read(id=fabric_uuid)
        return fabric_obj

    def test_create_fabric_encrypted_password(self):
        fabric_obj = self._create_fabric("fab-test")

        dev_cred = \
            fabric_obj.get_fabric_credentials().get_device_credential()[0]
        self.assertIsNotNone(dev_cred.credential.password)
        self.assertEqual(dev_cred.credential.password,
                         'P0Sy94TU0CJqMhM2Y/YrEpxZ4w6MIiS/LkzEIL60iDk=')
