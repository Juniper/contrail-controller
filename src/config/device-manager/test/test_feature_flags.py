#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.tests import test_common
import sys
from vnc_api.vnc_api import FeatureFlag
from device_manager.db import FeatureFlagDM
from .test_dm_common import TestCommonDM, VerifyCommonDM

#
# All Networks related DM test cases should go here
#
class TestFeatureFlagsDM(TestCommonDM, VerifyCommonDM):
    _test_cb_dict = {}

    def __init__(self, *args, **kwargs):
        super(TestFeatureFlagsDM, self).__init__(*args, **kwargs)

    @classmethod
    def cb_f0(cls, *args):
        cls._test_cb_dict['cb_f0'] = ','.join(args)
        return

    @classmethod
    def cb_f1(cls, *args):
        cls._test_cb_dict['cb_f1'] = ','.join(args)
        return

    def test_feature_flag_config(self):
        # Create feature flags
        f0_str = 'featureflag0'
        r2002 = 'r2002'
        f0_id = '__test_feature_1__'
        FeatureFlagDM.set_version(r2002)
        args_12 = ['arg1', 'arg2']
        FeatureFlagDM.register_callback(f0_id, 'virtual-network',
                                        TestFeatureFlagsDM.cb_f0, 
                                        args_12)

        args_34 = ['arg3', 'arg4']
        FeatureFlagDM.register_callback(f0_id, 'security-group',
                                        TestFeatureFlagsDM.cb_f1, 
                                        args_34)

        fflag0 = FeatureFlag(name=f0_str+'_'+r2002, feature_description=f0_str,
                             feature_id=f0_id, 
                             feature_flag_version=r2002,
                             feature_release=r2002, enable_feature=True)
        fflag0_uuid = self._vnc_lib.feature_flag_create(fflag0)
        self.wait_to_get_object(FeatureFlagDM, fflag0_uuid)

        # Check for both flag enabled, and callbacks got executed
        # remove callback execution traces by poping stored values
        self.assertTrue(FeatureFlagDM.is_feature_enabled(f0_id))
        self.assertTrue(self._test_cb_dict['cb_f0'] == ','.join(args_12))
        self.assertTrue(self._test_cb_dict['cb_f1'] == ','.join(args_34))
        self._test_cb_dict.pop('cb_f0')
        self._test_cb_dict.pop('cb_f1')
        dm_flags = FeatureFlagDM._ff_dict['feature_flags']
        self.assertTrue(r2002 in dm_flags)
        self.assertTrue(dm_flags[r2002][f0_id]['enable_feature'])

        # Change release to r2003 and check f0_id enablement
        # It should be false, the flag with version r2002
        r2003 = 'r2003'
        FeatureFlagDM.set_version(r2003)
        self.assertFalse(FeatureFlagDM.is_feature_enabled(f0_id))

        # add new feature flag
        f1_str = 'featureflag1'
        f1_id = '__test_feature_2__'
        fflag1 = FeatureFlag(name=f1_str, feature_description=f1_str,
                             feature_id=f1_id, 
                             feature_flag_version=r2002,
                             feature_release=r2002, enable_feature=True)
        fflag1_uuid = self._vnc_lib.feature_flag_create(fflag1)
        self.wait_to_get_object(FeatureFlagDM, fflag1_uuid)
        dm_flags = FeatureFlagDM._ff_dict['feature_flags']
        self.assertTrue(r2002 in dm_flags)
        self.assertTrue(dm_flags[r2002][f1_id]['enable_feature'])
        # feature won't be enabled because, system is set with r2003
        # if we change to r2002, it will be show as enabled.
        self.assertFalse(FeatureFlagDM.is_feature_enabled(f1_id))
        FeatureFlagDM.set_version(r2002)
        self.assertTrue(FeatureFlagDM.is_feature_enabled(f1_id))

        # change system to r2003 version
        FeatureFlagDM.set_version(r2003)
        f2_str = 'featureflag2'
        f1_id = '__test_feature_2__'
        fflag2 = FeatureFlag(name=f2_str, feature_description=f2_str,
                             feature_id=f1_id, 
                             feature_flag_version=r2003,
                             feature_release=r2003, enable_feature=True)
        fflag2_uuid = self._vnc_lib.feature_flag_create(fflag2)
        self.wait_to_get_object(FeatureFlagDM, fflag2_uuid)
        self.assertTrue(FeatureFlagDM.is_feature_enabled(f1_id))

# end TestFeatureFlagsDM

