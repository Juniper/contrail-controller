import requests, json
from requests.exceptions import ConnectionError

class AnalyticApiClient(object):
    def __init__(self, cfg):
        self.config = cfg
        self.client = requests.Session()
        self.init_client()
        self.base = None
        self._uves = None
        self._vrouters = None
        self._prouters = None

    def init_client(self):
        a = requests.adapters.HTTPAdapter(pool_connections=100,
                pool_maxsize=100)
        self.client.mount("http://", a)
        self.client.mount("https://", a)

    def _get_url_json(self, url):
        page = self.client.get(url)
        if page.status_code == 200:
            return json.loads(page.text)
        raise ConnectionError, "bad request " + url

    def _get_list_2_dict(self, j):
        return dict(map(lambda x: (x['name'], x['href']), j))

    def get_base(self):
        for api in self.config.analytics_api():
            self.base = self._get_list_2_dict(self._get_url_json(
                    'http://%s/analytics' % (api)))
            return
        raise ConnectionError, "No contrail-analytics-api to connect"

    def get_uve_url(self):
        if self.base is None:
            self.get_base()
        return self.base['uves']

    def get_uves(self):
        if self._uves is None:
            self._uves = self._get_list_2_dict(self._get_url_json(
                    self.get_uve_url()))
        return self._uves

    def get_vrouters(self, refresh=False):
        if self._vrouters is None or refresh:
            self._vrouters = self._get_list_2_dict(self._get_url_json(
                    self.get_uves()['vrouters']))
        return self._vrouters

    def list_vrouters(self):
        return self._vrouters.keys()

    def get_vrouter(self, vrouter):
        if vrouter in self.list_vrouters():
            return self._get_url_json(self.get_vrouters()[vrouter])

    def get_prouters(self, refresh=False):
        if self._prouters is None or refresh:
            self._prouters = self._get_list_2_dict(self._get_url_json(
                    self.get_uves()['prouters']))
        return self._prouters

    def list_prouters(self):
        return self._prouters.keys()

    def get_prouter(self, prouter):
        if prouter in self.list_prouters():
            return self._get_url_json(self.get_prouters()[prouter])


