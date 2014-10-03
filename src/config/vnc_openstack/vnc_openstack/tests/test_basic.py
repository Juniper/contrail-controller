import sys
import json
sys.path.append('../common/tests')
from testtools.matchers import Equals, Contains

from test_utils import *
import test_common
import test_case


class NBTestExtraFieldsPresenceCodeDefault(test_case.NeutronBackendTestCase):
    def test_extra_fields_on_network(self):
        test_obj = self._create_test_object()
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': test_obj.uuid}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/network', body)
        net_dict = json.loads(resp.text)
        self.assertIn('contrail:fq_name', net_dict)
    # end test_extra_fields_on_network
# end class NBTestExtraFieldsPresenceCodeDefault

class NBTestExtraFieldsPresenceByKnob(test_case.NeutronBackendTestCase):
    def __init__(self, *args, **kwargs):
        super(NBTestExtraFieldsPresenceByKnob, self).__init__(*args, **kwargs)
        self._config_knobs.append(('NEUTRON', 'contrail_extensions_enabled', True))
    # end __init__

    def test_extra_fields_on_network(self):
        test_obj = self._create_test_object()
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': test_obj.uuid}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/network', body)
        net_dict = json.loads(resp.text)
        self.assertIn('contrail:fq_name', net_dict)
    # end test_extra_fields_on_network
# end class NBTestExtraFieldsPresenceByKnob

class NBTestExtraFieldsAbsenceByKnob(test_case.NeutronBackendTestCase):
    def __init__(self, *args, **kwargs):
        super(NBTestExtraFieldsAbsenceByKnob, self).__init__(*args, **kwargs)
        self._config_knobs.append(('NEUTRON', 'contrail_extensions_enabled', False))
    # end __init__

    def test_no_extra_fields_on_network(self):
        test_obj = self._create_test_object()
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': test_obj.uuid}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/network', body)
        net_dict = json.loads(resp.text)
        self.assertNotIn('contrail:fq_name', net_dict)
    # end test_extra_fields_on_network
# end class NBTestExtraFieldsAbsenceByKnob
