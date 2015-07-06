import sys
sys.path.append("../common/tests")
from test_utils import *
import test_common

class ApiServerTestCase(test_common.TestCase):
    def setUp(self, extra_mocks = None, extra_config_knobs = None):
        super(ApiServerTestCase, self).setUp(extra_mocks, extra_config_knobs)
        self.ignore_err_in_log = False

    def tearDown(self):
        try:
            if self.ignore_err_in_log:
                return

            with open('api_server_%s.log' %(self.id())) as f:
                lines = f.read()
                self.assertIsNone(
                    re.search('SYS_ERR', lines), 'Error in log file')
        except IOError:
            # vnc_openstack.err not created, No errors.
            pass
        finally:
            super(ApiServerTestCase, self).tearDown()
# end class ApiServerTestCase
