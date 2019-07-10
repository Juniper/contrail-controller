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
from six.moves.urllib import parse as urlparse


class OpenContrailAPIFailed(Exception):
    pass


class Client(object):
    """Opencontrail Base Statistics REST API Client."""

    def __init__(self, analytics_api_ip, analytics_api_port,
                 data={}, analytics_api_ssl_params=None):
        self.data = data
        # As per requirements, IPs can be space or comma seperated
        self.analytics_api_servers = analytics_api_ip.replace(',', ' ').split()
        if len(self.analytics_api_servers) == 0:
            raise IndexError("No analytics API IP provided")
        self.analytics_api_port = analytics_api_port
        self._analytics_ssl_params = analytics_api_ssl_params
        self.index = -1

    def round_robin(self):
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

        # get the starting index
        analytics_server_index = self.round_robin()

        client_count = len(self.analytics_api_servers)
        # once you have a starting index, we need to traverse through
        # entire list in case of failure.
        for index in range(client_count):
            try:
                analytics_ip = self.analytics_api_servers[
                    analytics_server_index]

                resp = None
                if (self._analytics_ssl_params and
                        self._analytics_ssl_params['ssl_enable']):
                    endpoint = "https://%s:%s" % (analytics_ip,
                                                  self.analytics_api_port)
                    certfile = self._analytics_ssl_params['certfile']
                    keyfile = self._analytics_ssl_params['keyfile']
                    self._cert = (certfile, keyfile)
                    url = urlparse.urljoin(endpoint, path + fqdn_uuid)
                    if (self._analytics_ssl_params['insecure_enable']):
                        resp = requests.get(url, cert=self._cert,
                                            verify=False, **req_params)
                    else:
                        resp = requests.get(
                                  url, cert=self._cert,
                                  verify=self._analytics_ssl_params['ca_cert'],
                                  **req_params)
                else:
                    endpoint = "http://%s:%s" % (analytics_ip,
                                                 self.analytics_api_port)
                    url = urlparse.urljoin(endpoint, path + fqdn_uuid)
                    resp = requests.get(url, **req_params)
                if resp.status_code != 200:
                    raise OpenContrailAPIFailed(
                        ('Opencontrail API returned %(status)s %(reason)s') %
                        {'status': resp.status_code, 'reason': resp.reason})
                return resp.json()

            except Exception as e:
                # In case of failure, continue till we check all other
                # available servers
                analytics_server_index = self.round_robin()
                continue

        # If we reach here, raise the last encountered exception
        raise e

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
