"""
This file contains sanity test for activate maintenance mode workflow
"""
from __future__ import absolute_import
from builtins import str
from builtins import object
import sys
from . import config

class FabricAnsibleModule(object):
    pass

sys.path.append('../..')
sys.modules['ansible.module_utils.fabric_utils'] = __import__('sanity_test_hitless_upgrade')
from .sanity_base import SanityBase

class SanityTestHitless(SanityBase):

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_activate_maintenance_mode')
        self.fabric = cfg['fabric']
        self.advanced_params = cfg['advanced_params']
        self.device_name = cfg['mm_device']
        self.mode = cfg['mm_mode']

    def test_maintenance_mode_activate(self):
        try:
            device_fq_name = ['default-global-system-config', self.device_name]
            device_uuid = self._api.physical_router_read(
                fq_name=device_fq_name).uuid
            fabric_fq_name = ['default-global-system-config', self.fabric]
            fabric = self._api.fabric_read(fq_name=fabric_fq_name)

            self.activate_maintenance_mode(device_uuid, self.mode,
                                           fabric, self.advanced_params,
                                           [self.device_name])
        except Exception as ex:
            self._exit_with_error(
                "Activate maintenance mode failed due to unexpected error: %s"
                % str(ex))

if __name__ == "__main__":
    SanityTestHitless(config.load('sanity/config/test_config.yml',
                             'sanity/config/image_config.yml')).test_maintenance_mode_activate()

