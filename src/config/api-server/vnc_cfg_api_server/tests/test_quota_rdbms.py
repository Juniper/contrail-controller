import test_case
import test_quota


class TestQuota(test_quota.TestQuota, test_case.ApiServerRDBMSTestCase):
    pass


class TestGlobalQuota(
        test_quota.TestGlobalQuota,
        test_case.ApiServerRDBMSTestCase):
    pass
