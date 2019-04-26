#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Swift upload image file.

This file contains implementation of gett
swift download URL for the uploaded image file
"""


DOCUMENTATION = '''
---

module: Swift file util
author: Juniper Networks
short_description: Private module to get swift download url of the image file
description:
    - Pass the required swift config info get the download url of image file.
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
            - Swift username
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
            - Temp url key 2
        required: true
    connection_retry_count:
        description:
            - Connection retry count
        type: int
        required: false
        default: 5
    chosen_temp_url_key:
        description:
            - Chosen Temp url key
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

'''

EXAMPLES = '''
'''

RETURN = '''
url:
  description:
    - An image file url used to download the file without authentication.
  returned: on success always
  type: str
error_msg:
  description:
    - Its an error message that is returned if there is any exception or error.
  returned: on failure
  type: str
'''

import logging
import re
from threading import RLock
import sys
import time
from urlparse import urlparse

from ansible.module_utils.fabric_utils import FabricAnsibleModule
import requests
import swiftclient
import swiftclient.utils

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
sys.path.append('../fabric-ansible/ansible-playbooks/module_utils') # unit test
from fabric_utils import FabricAnsibleModule

connection_lock = RLock()


class FileSvcUtil(object):  # pragma: no cover
    def __init__(self, authtoken, authurl, user, key, tenant_name,
                 auth_version, container_name, temp_url_key,
                 temp_url_key2, connection_retry_count, chosen_temp_url_key):
        """Init routine."""
        self.requests = requests
        self.authurl = authurl
        self.preauthtoken = authtoken
        self.user = user
        self.key = key
        self.auth_version = auth_version
        self.container_name = container_name
        self.temp_url_key = temp_url_key
        self.temp_url_key_2 = temp_url_key2
        self.connection_retry_count = connection_retry_count
        self.chosen_temp_url_key = chosen_temp_url_key
        self.conn_timeout_sec = 10
        self.tenant_name = tenant_name
        self.generateToken()
        self.updateAccount()

    def generateToken(self):
        retry_count = 0
        incr_sleep = 10
        while retry_count <= self.connection_retry_count:
            try:
                acquired = connection_lock.acquire()
                swiftconn = swiftclient.client.Connection(
                    authurl=self.authurl,
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
                if retry_count == self.connection_retry_count:
                    raise Exception(
                        "Connection failed with swift server: " +
                        str(err_msg))
                logging.error(
                    "Connection failed with swift server, retrying..")
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
            raise Exception(
                "Update account failed with swift file server: " +
                str(err))

    def getobjectFileUri(self, filename):
        return self.getFileObjUri(self.container_name, filename)

    def getFileObjUri(self, container_name, fileobj_name):
        return urlparse('/%s/%s' % (container_name, fileobj_name)).path

    def getObjUrl(self, filename):
        image_path = self.getobjectFileUri(filename)
        try:
            image_url = self.getPublicDownloadUrl(image_path)
            return image_url
        except Exception as e:
            logging.error(str(e))
            raise Exception(
                "Get object url failed with swift file server: " + str(e))

    def getPublicDownloadUrl(self, image_path):
        return '%s/%s' % (
            re.sub(r'([^/])/*$', r'\1', self.storageurl),
            re.sub(r'^/*([^/])', r'\1', image_path))

    def close(self):
        if self.swift_conn:
            self.swift_conn.close()


def main():
    module = FabricAnsibleModule(
        argument_spec=dict(
            authtoken=dict(required=True),
            authurl=dict(required=True),
            user=dict(required=True),
            key=dict(required=True),
            tenant_name=dict(required=False, default="admin"),
            auth_version=dict(required=False, default='3.0'),
            temp_url_key=dict(required=True),
            temp_url_key_2=dict(required=True),
            chosen_temp_url_key=dict(required=False,
                                     default="temp_url_key"),
            container_name=dict(required=True),
            filename=dict(required=True),
            connection_retry_count=dict(required=False,
                                        default=5, type='int')),
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
    connection_retry_count = m_args['connection_retry_count']

    url = None
    error_msg = ''
    try:
        fileutil = FileSvcUtil(
            authtoken,
            authurl,
            user,
            key,
            tenant_name,
            auth_version,
            container_name,
            temp_url_key,
            temp_url_key_2,
            connection_retry_count,
            chosen_temp_url_key)

        url = fileutil.getObjUrl(filename)

        fileutil.close()

    except Exception as e:
        error_msg = "Exception occurred in swift_fileutil: " + str(e)

    results = {}
    results['url'] = url
    results['error_msg'] = error_msg

    module.exit_json(**results)


if __name__ == '__main__':
    main()
