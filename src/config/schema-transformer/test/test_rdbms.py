from test_case import STTestCaseRDBMS

import test_bgp
import test_policy
import test_route_table
import test_route_target
import test_security_group

class TestBgp(STTestCaseRDBMS, test_bgp.TestBgp):
    pass

class TestPolicy(STTestCaseRDBMS, test_policy.TestPolicy):
    pass

class TestRouteTable(STTestCaseRDBMS, test_route_table.TestRouteTable):
    pass

class TestRouteTarget(STTestCaseRDBMS, test_route_target.TestRouteTarget):
    pass

class TestSecurityGroup(STTestCaseRDBMS, test_security_group.TestSecurityGroup):
    pass