"""
This file contains sanity test for activate maintenance mode workflow
"""
import sys
import config

class FabricAnsibleModule:
    pass

sys.path.append('../..')
from sanity_base import SanityBase

class SanityTestHitless(SanityBase):

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_container_cleanup')

    def test_container_cleanup(self):
        try:
            container_name = "contrail_container"
            fabric_fq_name = ['default-global-system-config', 'default-fabric']
            self.container_cleanup(fabric_fq_name,container_name)

        except Exception as ex:
            self._exit_with_error(
                "Container cleanup failed due to unexpected error: %s"
                % str(ex))

if __name__ == "__main__":
    SanityTestHitless(config.load('config/test_config.yml',
                             'config/image_config.yml')).test_container_cleanup()


