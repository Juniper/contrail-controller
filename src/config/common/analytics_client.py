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

    def __init__(self, analytics_api_ip, analytics_api_port, data={}):
        self.data = data
        #As per requirements, IPs can be space or comma seperated
        self.analytics_api_servers = analytics_api_ip.replace(',', ' ').split(' ')
        self.analytics_api_port = analytics_api_port
        self.index = -1

    def roundRobin(self):
        self.index += 1
        if self.index >= len(self.analytics_api_servers):
            self.index = 0
        return self.index
 
    def request(self, path, fqdn_uuid, user_token=None,
                data=None):
        req_data = dict(self.data)
        if data:
            req_data.update(data)

        req_params = self._get_req_params(user_token, data=req_data)

        #get the starting index
        starting_client_server_index = self.roundRobin() 

        client_count = len(self.analytics_api_servers)
        #once you have a starting index, we need to traverse through 
        #entire list in case of failure.
        for index in range(client_count):
            try:
                analytics_ip = self.analytics_api_servers[(index+starting_client_server_index)%client_count]
                endpoint = "http://%s:%s" % (analytics_ip,
                                     self.analytics_api_port)

                url = urlparse.urljoin(endpoint, path + fqdn_uuid)
                resp = requests.get(url, **req_params)
                if resp.status_code != 200:
                    raise OpenContrailAPIFailed(
                        ('Opencontrail API returned %(status)s %(reason)s') %
                        {'status': resp.status_code, 'reason': resp.reason})
                return resp.json()

            except ConnectionError:
                continue

        raise ConnectionError

    def _get_req_params(self, user_token, data=None):
        req_params = {
            'headers': {
                'Accept': 'application/json'
            },
            'data': data,
            'allow_redirects': False,
        }
        if user_token:
            req_params['headers']['X-AUTH-TOKEN'] = user_token

        return req_params
