#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import gevent
import uuid
sys.path.append("../config/common/tests")
from testtools.matchers import Equals, Contains, Not
from test_utils import *
import test_common
import test_case

from vnc_api.vnc_api import *
from vnc_api.gen.resource_xsd import *
from cfgm_common.exceptions import *
from gevent import sleep

import discoveryclient.client as client

EP_DELIM=','
PUBSUB_DELIM=' '

def parse_pubsub_ep(pubsub_str):
    r = pubsub_str.split(EP_DELIM)
    if len(r) < 4:
        for i in range(4-len(r)):
            r.append('')
    return r

# '1.1.1.1/24' or '1.1.1.1'
def prefix_str_to_obj(prefix_str):
    if '/' not in prefix_str:
        prefix_str += '/32'
    x = prefix_str.split('/')
    if len(x) != 2:
        return None
    return SubnetType(x[0], int(x[1]))

def build_dsa_rule_entry(rule_str):
    r = parse_pubsub_ep(rule_str)
    r = rule_str.split(PUBSUB_DELIM) if rule_str else []
    if len(r) < 2:
        return None

    # [0] is publisher-spec, [1] is subscriber-spec
    pubspec = parse_pubsub_ep(r[0])
    subspec = parse_pubsub_ep(r[1])

    pfx_pub = prefix_str_to_obj(pubspec[0])
    pfx_sub = prefix_str_to_obj(subspec[0])
    if pfx_sub is None or pfx_sub is None:
        return None

    publisher = DiscoveryPubSubEndPointType(ep_prefix = pfx_pub,
                    ep_type = pubspec[1], ep_id = pubspec[2],
                    ep_version = pubspec[3])
    subscriber = [DiscoveryPubSubEndPointType(ep_prefix = pfx_sub,
                     ep_type = subspec[1], ep_id = subspec[2],
                     ep_version = subspec[3])]
    dsa_rule_entry = DiscoveryServiceAssignmentType(publisher, subscriber)
    return dsa_rule_entry

def info_callback(info):
    print 'In subscribe callback handler'
    print '%s' % (info)
    pass

class TestDsa(test_case.DsTestCase):

    def test_dsa_config(self):
        # Assign DC1 control nodes to DC1 agents
        rule_entry = build_dsa_rule_entry('1.1.1.0/24,Control-Node 1.1.1.0/24,Vrouter-Agent')
        dsa = DiscoveryServiceAssignment()
        rule_uuid = uuid.uuid4()
        dsa_rule1 = DsaRule(name = str(rule_uuid), parent_obj = dsa, dsa_rule_entry = rule_entry)
        dsa_rule1.set_uuid(str(rule_uuid))
        self._vnc_lib.dsa_rule_create(dsa_rule1)

        # Assign DC2 control nodes to DC1 agents
        rule_entry = build_dsa_rule_entry('2.2.2.0/24,Control-Node 2.2.2.0/24,Vrouter-Agent')
        rule_uuid = uuid.uuid4()
        dsa_rule2 = DsaRule(name = str(rule_uuid), parent_obj = dsa, dsa_rule_entry = rule_entry)
        dsa_rule2.set_uuid(str(rule_uuid))
        self._vnc_lib.dsa_rule_create(dsa_rule2)

        puburl = '/publish'
        service_type = 'Control-Node'

        # publish 4 control nodes - 2 in two data centers each
        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.1", "port" : "1111" },
            'service-type' : '%s' % service_type,
            'service-id' : 'DC1-CN1',
            'remote-addr': '1.1.1.1',
        }
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        payload = {
            '%s' % service_type: { "ip-addr" : "1.1.1.2", "port" : "1112" },
            'service-type' : '%s' % service_type,
            'service-id' : 'DC1-CN2',
            'remote-addr': '1.1.1.2',
        }
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        payload = {
            '%s' % service_type: { "ip-addr" : "2.2.2.1", "port" : "2221" },
            'service-type' : '%s' % service_type,
            'service-id' : 'DC2-CN1',
            'remote-addr': '2.2.2.1',
        }
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        payload = {
            '%s' % service_type: { "ip-addr" : "2.2.2.2", "port" : "2222" },
            'service-type' : '%s' % service_type,
            'service-id' : 'DC2-CN2',
            'remote-addr': '2.2.2.2',
        }
        (code, msg) = self._http_post(puburl, json.dumps(payload))
        self.assertEqual(code, 200)

        # Verify all services are published.
        # Total service count is 4+2 because API server publishes
        # IfmapServer and ApiServer
        (code, msg) = self._http_get('/services.json')
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response['services']), 6)

        # json subscribe request
        suburl = "/subscribe"
        payload = {
            'service'     : '%s' % service_type,
            'client'      : 'DC1-VA1',
            'instances'   : 0,
            'client-type' : 'Vrouter-Agent',
            'remote-addr' : '3.3.3.3',
            'version'     : '2.2',
        }

        # should see all 4 publishers for sub that is not in DC1 or DC2
        (code, msg) = self._http_post(suburl, json.dumps(payload))
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response[service_type]), 4)

        # Sub in DC1 - should see only DC1 services
        payload['remote-addr'] = '1.1.1.3'
        (code, msg) = self._http_post(suburl, json.dumps(payload))
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response[service_type]), 2)

        for svc in response[service_type]:
            self.assertEqual("DC1-CN" in svc['@publisher-id'], True)

        # Sub in DC2 - should see only DC2 services
        payload['remote-addr'] = '2.2.2.3'
        (code, msg) = self._http_post(suburl, json.dumps(payload))
        self.assertEqual(code, 200)
        response = json.loads(msg)
        self.assertEqual(len(response[service_type]), 2)

        for svc in response[service_type]:
            self.assertEqual("DC2-CN" in svc['@publisher-id'], True)

        """
        Subscribe to IfmapServer from DC1, DC2 and DC3. There are no
        assignment rules applicable to IfmapServer. Thus clients from
        all DC should be able to subscribe to singtleton IfmapServer
        """
        service_type = 'IfmapServer'
        payload = {
            'service'     : '%s' % service_type,
            'client'      : 'DC1-VA1',
            'instances'   : 0,
            'client-type' : 'Vrouter-Agent',
        }
        for remote in ['1.1.1.1', '2.2.2.2', '3.3.3.3']:
            payload['remote-addr'] = remote
            (code, msg) = self._http_post(suburl, json.dumps(payload))
            self.assertEqual(code, 200)
            response = json.loads(msg)
            self.assertEqual(len(response[service_type]), 1)

        """
        Delete service assignment rule. 
        Subs from any DC should see all DC1+DC2 services
        """
        self._vnc_lib.dsa_rule_delete(id = dsa_rule1.uuid)
        self._vnc_lib.dsa_rule_delete(id = dsa_rule2.uuid)

        service_type = 'Control-Node'
        payload = {
            'service'     : '%s' % service_type,
            'client'      : 'Dont Care',
            'instances'   : 0,
            'client-type' : 'Vrouter-Agent',
        }

        # Sub in DC1 or DC2 should see DC1+DC2 services
        expectedpub_set = set(["DC1-CN1", "DC1-CN2", "DC2-CN1", "DC2-CN2"])
        for sub_ip in ['1.1.1.3', '2.2.2.3']:
            payload['remote-addr'] = sub_ip
            (code, msg) = self._http_post(suburl, json.dumps(payload))
            self.assertEqual(code, 200)
            response = json.loads(msg)
            self.assertEqual(len(response[service_type]), 4)
            receivedpub_set = set([svc['@publisher-id'] for svc in response[service_type]])
            self.assertEqual(expectedpub_set == receivedpub_set, True)

#end class TestDsa
