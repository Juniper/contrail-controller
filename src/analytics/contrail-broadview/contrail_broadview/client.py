#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import random
import requests, json
from requests.exceptions import ConnectionError

class BroadviewApiClient(object):
    def __init__(self, ol):
        self._objl = ol
        self.client = requests.Session()
        self.init_client()
        self._reqid = random.randint(1, 256)

    def init_client(self):
        a = requests.adapters.HTTPAdapter(pool_connections=100,
                pool_maxsize=100)
        self.client.mount("http://", a)
        self.client.mount("https://", a)

    def update_prouter(self, prouter):
        if prouter.switch_properties() is None:
            self.get_switch_properties(prouter)

    def get_bst_report(self, prouter, asic):
        return self._get_bv_data(prouter, 'get-bst-report',
                {'asic-id': asic, 'params': self._get_all_bst_param()})

    def _get_all_bst_param(self):
        return dict(map(lambda x:('include-' + x, 1),
                    self._objl.get_raw_params()))

    def get_switch_properties(self, prouter):
        prouter.set_switch_properties(self._get_bv_data(prouter,
                    'get-switch-properties'))

    def _get_bv_data(self, prouter, method, params={}):
        uri = self.get_base_uri(prouter)
        req = self.get_base_req()
        req['method'] = method
        req.update(params)
        return self._get_url_json(uri + method, req)

    def get_base_uri(self, prouter):
        return 'http://%s:%d/broadview/' % (prouter.ip(), 
                                            prouter.port())

    def get_base_req(self):
        self._reqid += 1
        d = dict(jsonrpc='2.0', params={}, id=self._reqid)
        d['asic-id'] = '1'
        return d

    def _get_url_json(self, url, req={}):
        if url is None:
            return {}
        h = {'content-type': 'application/json'}
        page = self.client.post(url, data=json.dumps(req), headers=h)
        if page.status_code == 200:
            return json.loads(page.text)
        raise ConnectionError, "bad request " + url

    def _get_list_2_dict(self, j):
        return dict(map(lambda x: (x['name'], x['href']), j))

    def get_base(self):
        for api in self.config.analytics_api():
            try:
                self.base = self._get_list_2_dict(self._get_url_json(
                    'http://%s/analytics' % (api)))
                return
            except:
                self.base = None


