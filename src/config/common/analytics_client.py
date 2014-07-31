# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright (c) 2014 Cloudwatt
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# @author: Sylvain Afchain, eNovance.

import requests
import six
from six.moves.urllib import parse as urlparse


class OpenContrailAPIFailed(Exception):
    pass


class Client(object):
    """Opencontrail Base Statistics REST API Client."""
    #TODO: use a pool of servers

    def __init__(self, endpoint, data={}):
        self.endpoint = endpoint
        self.data = data

    def request(self, path, fqdn_uuid, data=None):
        req_data = dict(self.data)
        if data:
            req_data.update(data)

        req_params = self._get_req_params(data=req_data)

        url = urlparse.urljoin(self.endpoint, path + fqdn_uuid)
        resp = requests.get(url, **req_params)

        if resp.status_code != 200:
            raise OpenContrailAPIFailed(
                ('Opencontrail API returned %(status)s %(reason)s') %
                {'status': resp.status_code, 'reason': resp.reason})

        return resp.json()

    def _get_req_params(self, data=None):
        req_params = {
            'headers': {
                'Accept': 'application/json'
            },
            'data': data,
            'allow_redirects': False,
        }

        return req_params
