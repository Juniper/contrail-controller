#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of getting swift temp URL for image upload
"""

DOCUMENTATION = '''
---

module: Swift file util
author: Juniper Networks
short_description: Private module to get swift temp url of the file
description:
    - Pass the required swift config info and get the temp url of the file
requirements:
    -
options:
    authtoken:
        description:
            - authentication token string
        required: true
    authurl:
        description:
            - authentication url string
        required: true
    user:
        description:
            - Swift Username
        type: string
        required: true
    key:
        description:
            - Swift password
        type: string
        required: true
    tenant_name:
        description:
            - Tenant name.
        type: string
        required: false
        default: 'admin'
    auth_version:
        description:
            - Keystone Auth version.
        required: false
        default: '3.0'
    temp_url_key:
        description:
            - Temp url key
        required: true
    temp_url_key_2:
        description:
            - Temp ulr key 2
        required: true
    chosen_temp_url_key:
        description:
            - Temp ulr key 2
        required: false
        default: 'temp_url_key'
    container_name:
        description:
            - Name of the container
        required: true
    filename:
        description:
            - Name of the image file
        required: true
    expirytime:
        description:
            - Integer. Expiry time in seconds
        type: int
        required: true

'''

EXAMPLES = '''
'''

RETURN = '''
url:
  description:
    - An image file url that can be downloaded without authentication for
      the specified time period.
  returned: on success always
  type: str
error_msg:
  description:
    - Its an error message that is returned if there is any exception or error.
  returned: on failure
  type: str
'''

import logging
import requests
import re
import time
from urlparse import urlparse
import swiftclient
import swiftclient.utils
from ansible.module_utils.basic import AnsibleModule
from threading import RLock

connection_lock = RLock()


class FileSvcUtil(object):  # pragma: no cover
    def __init__(self, authtoken, authurl, user, key, tenant_name,
                 auth_version, container_name, temp_url_key,
                 temp_url_key2, chosen_temp_url_key):
        self.requests = requests
        self.authurl = authurl
        self.preauthtoken = authtoken
        self.user = user
        self.key = key
        self.auth_version = auth_version
        self.container_name = container_name
        self.chunk_size = 65336
        self.temp_url_key = temp_url_key
        self.temp_url_key_2 = temp_url_key2
        self.chosen_temp_url_key = chosen_temp_url_key
        self.conn_timeout_sec = 10
        self.tenant_name = tenant_name

        self.generateToken()
        self.updateAccount()

    def generateToken(self):
        max_retry = 5
        retry_count = 0
        incr_sleep = 10
        while retry_count <= max_retry:
            try:
                acquired = connection_lock.acquire()
                swiftconn = swiftclient.client.Connection(authurl=self.authurl,
                                                          user=self.user,
                                                          key=self.key,
                                                          preauthtoken=self.preauthtoken,
                                                          tenant_name=self.tenant_name,
                                                          auth_version=self.auth_version,
                                                          timeout=self.conn_timeout_sec,
                                                          insecure=True)
                self.swift_conn = swiftconn
                swiftconn.get_account()
                self.storageurl = swiftconn.url
                break
            except Exception as e:
                retry_count += 1
                err_msg = e.message
                logging.error(err_msg)
                if retry_count == max_retry:
                    raise Exception("Connection failed with swift file server: " + str(err_msg))
                logging.error("Connection failed with swift file server, retrying to connect")
                incr_sleep *= 2
                time.sleep(incr_sleep)
            finally:
                if acquired:
                    connection_lock.release()

    def updateAccount(self):
        headers = {'Temp-URL-Key': self.temp_url_key}
        if self.temp_url_key_2 is not None:
            headers['Temp-URL-Key-2'] = self.temp_url_key_2
        try:
            self.swift_conn.post_account(headers)
        except Exception as err:
            logging.error(str(err))
            raise Exception("Update account failed with swift file server: " + str(err))

    def getobjectFileUri(self, filename):
        return self.getFileObjUri(self.storageurl, self.container_name, filename)

    def getFileObjUri(self, accountpath, container_name, fileobj_name):
        return urlparse('%s/%s/%s' % (accountpath, container_name, fileobj_name)).path

    def getObjTempUrl(self, filename, expires):
        objectfile_uri = self.getobjectFileUri(filename)
        try:
            image_path = swiftclient.utils.generate_temp_url(
                objectfile_uri, expires,
                getattr(self, self.chosen_temp_url_key),
                'GET')
            image_url = self.getPublicDownloadUrl(image_path)
            return image_url
        except Exception as e:
            logging.error(str(e))
            raise Exception("Get object temp url failed with swift file server: " + str(e))

    def getPublicDownloadUrl(self, image_path):

        path = urlparse(self.storageurl).path
        public_url = self.storageurl.split(path)[0]
        return '%s/%s' % (
            re.sub(r'([^/])/*$', r'\1', public_url),
            re.sub(r'^/*([^/])', r'\1', image_path))

    def close(self):
        if self.swift_conn:
            self.swift_conn.close()

def main():
    module = AnsibleModule(
        argument_spec=dict(
            authtoken=dict(required=True),
            authurl=dict(required=True),
            user=dict(required=True),
            key=dict(required=True),
            tenant_name=dict(required=False, default="admin"),
            auth_version=dict(required=False, default='3.0'),
            temp_url_key=dict(required=True),
            temp_url_key_2=dict(required=True),
            chosen_temp_url_key=dict(required=False, default="temp_url_key"),
            container_name=dict(required=True),
            filename=dict(required=True),
            expirytime=dict(required=True, type='int')),
        supports_check_mode=False)

    m_args = module.params
    authtoken = m_args['authtoken']
    authurl = m_args['authurl']
    user = m_args['user']
    key = m_args['key']
    tenant_name = m_args['tenant_name']
    auth_version = m_args['auth_version']
    temp_url_key = m_args['temp_url_key']
    temp_url_key_2 = m_args['temp_url_key_2']
    chosen_temp_url_key = m_args['chosen_temp_url_key']

    container_name = m_args['container_name']
    filename = m_args['filename']
    expirytime = m_args['expirytime']

    url = None
    error_msg = ''
    try:
        fileutil = FileSvcUtil(authtoken, authurl, user, key, tenant_name,
                               auth_version, container_name, temp_url_key,
                               temp_url_key_2, chosen_temp_url_key)

        url = fileutil.getObjTempUrl(filename, int(expirytime))

        fileutil.close()

    except Exception as e:
        error_msg = "Exception occurred in swift_fileutil: " + str(e)

    results = {}
    results['url'] = url
    results['error_msg'] = error_msg

    module.exit_json(**results)


if __name__ == '__main__':
    main()

