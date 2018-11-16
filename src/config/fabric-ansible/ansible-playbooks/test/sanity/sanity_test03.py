"""
This file contains sanity test for image upgrade workflow
"""
import sys
import hashlib
import config
import re

from sanity_base import SanityBase
import config


class SanityTest03(SanityBase):

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test03')

    def test_maintenance_mode(self):
        image = self.create_image('image1', 'abc_uri', '17.5', 'junos',
                                  'juniper')

        self.image_upgrade_maintenance_mode(image,'1c45310e-bff3-49e8-925f-389fa9edaf0a')



if __name__ == "__main__":
    SanityTest03(config.load('config/test_config.yml')).test_maintenance_mode()

