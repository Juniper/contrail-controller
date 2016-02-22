import json
import os
import logging
import requests

class Identity():
    '''Identity class to get project-scoped
       tokens for user'''
    def __init__(self, args_dict):
        if not self._parse_args(args_dict):
            raise Exception()

    def _parse_args(self, args_dict):
        if 'keystone_version' not in args_dict:
            args_dict['keystone_version'] = 'v2.0'

        if 'barbican_endpoint' not in args_dict:
            args_dict['barbican_endpoint'] = ''

        if 'domain_name' not in args_dict:
            args_dict['domain_name'] = 'default'

        try:
            self.keystone_ep = args_dict['keystone_endpoint']
            self.barbican_ep = args_dict['barbican_endpoint']
            self.domain_name = args_dict['domain_name']
            self.os_username = args_dict['username']
            self.os_password = args_dict['password']
            self.os_project_name = args_dict['project_name']
            self.identity_version = args_dict['keystone_version']
            self.auth_token = None

            if not self.keystone_ep:
                logging.error("Keystone Endpoint missing in the config file")
                return False

            if not self.identity_version:
                self.identity_version = 'v2.0'

            self.keystone_ep = self.keystone_ep + '/' + self.identity_version

            if self.identity_version == 'v3' and not self.domain_name:
                logging.error("Domain name missing in the config file")
                return False

            if not self.os_username:
                logging.error("Username missing in the config file")
                return False

            if not self.os_password:
                logging.error("Password missing in the config file")
                return False

            if self.identity_version == 'v2.0':
                if not self._get_v2_project_scoped_auth_token():
                    logging.error("Failed to fetch v2.0 token")
                    return False
            elif self.identity_version == 'v3' and self.os_project_name is None:
                if not self._get_domain_scoped_auth_token():
                    logging.error("Failed to fetch domain scoped token")
                    return False
            else:
                if not self._get_v3_project_scoped_auth_token():
                    logging.error("Failed to fetch project scoped token")
                    return False

            return True

        except Exception as e:
            logging.error(str(e))
            return False

    @staticmethod
    def _request(url, headers=None, body=None, request_type=None):
        try:
            if request_type == 'PUT':
                encoded_body = json.dumps(body)
                return requests.put(url, headers=headers, data=encoded_body)
            elif request_type == 'POST':
                encoded_body = json.dumps(body)
                return requests.post(url, headers=headers, data=encoded_body)
            else:
                return requests.get(url, headers=headers)

        except Exception as e:
            logging.error("Failed sending request to keystone")
            return None

    def _get_v3_project_scoped_auth_token(self):
        try:
            headers = {
                "Content-Type": "application/json",
            }
            body = {
                "auth": {
                    "identity": {
                        "methods": ["password"],
                        "password": {
                            "user": {
                                "name": self.os_username,
                                "domain": { "id": "default" },
                                "password": self.os_password
                             }
                        }
                    },
                    "scope": {
                        "project": {
                            "name": self.os_project_name,
                            "domain": { "id": "default" }
                        }
                    }
                }
            }

            url = self.keystone_ep + "/auth/tokens"
            resp = self._request(url, headers, body, 'POST')
            if resp and resp.status_code in range(200, 299):
                headers = resp.headers
                if headers and 'x-subject-token' in headers:
                    self.auth_token = headers['x-subject-token']
                    return True

            return False
        except Exception as e:
            logging.error(str(e))
            return False

    def _get_v2_project_scoped_auth_token(self):
        try:
            headers = {
                "Content-Type": "application/json",
            }
            body = {
                "auth": {
                    "tenantName": self.os_project_name,
                    "passwordCredentials": {
                        "username": self.os_username,
                        "password": self.os_password
                    }
                }
            }

            url = self.keystone_ep + "/tokens"
            resp = self._request(url, headers, body, 'POST')
            if resp and resp.status_code in range(200, 299):
                json_data = json.loads(resp.text)
                self.auth_token = json_data['access']['token']['id']
                return True
            return False
        except Exception as e:
            logging.error(str(e))
            return False

    def _get_domain_scoped_auth_token(self):
        # Not supported by barbican
        return False
