import requests
import six
from six.moves.urllib import parse as urlparse


class OpencontrailAPIFailed(Exception):
    pass


class Client(object):
    """Opencontrail Base Statistics REST API Client."""
    #TODO: use a pool of servers

    def __init__(self, endpoint, data):
        self.endpoint = endpoint
        self.data = data or {}

    def request(self, path, fqdn_uuid, data=None):
        req_data = self.data
        if data:
            req_data.update(data)

        req_params = self._get_req_params(data=req_data)

        url = urlparse.urljoin(self.endpoint, path + fqdn_uuid)
        self._log_req(url, req_params)
        resp = requests.get(url, **req_params)
        self._log_res(resp)

        if resp.status_code != 200:
            raise OpencontrailAPIFailed(
                _('Opencontrail API returned %(status)s %(reason)s') %
                {'status': resp.status_code, 'reason': resp.reason})

        return resp

    def _get_req_params(self, data=None):
        req_params = {
            'headers': {
                'Accept': 'application/json'
            },
            'data': data,
            'allow_redirects': False,
        }

        return req_params
