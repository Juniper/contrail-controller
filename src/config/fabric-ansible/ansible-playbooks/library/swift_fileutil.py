#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of getting swift temp URL for image upload
"""

import logging
import os
import re
import requests
from urlparse import urlparse
import swiftclient
import swiftclient.utils
import time
from ansible.module_utils.basic import *
import ConfigParser

from swiftclient.exceptions import ClientException
from threading import RLock

connection_lock = RLock()

class FileSvcUtil(object):  # pragma: no cover
    def __init__(self, authtoken, authurl, user, key, tenant_name,
                 auth_version, container_name, temp_url_key,
                 temp_url_key2, chosen_temp_url_key):
        self.requests = requests
        self.region_name = "regional"
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
        while retry_count <= max_retry:
            try:
                acquired = connection_lock.acquire()
                swiftconn = swiftclient.client.Connection(authurl=self.authurl,
                                    user=self.user, key=self.key,
                                    preauthtoken=self.preauthtoken,
                                    tenant_name=self.tenant_name,
                                    auth_version=self.auth_version,
                                    timeout=self.conn_timeout_sec,
                                    insecure=True,
                                    os_options={'region_name': self.region_name})
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
                 time.sleep(20)
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

    def createContainer(self):
        try:
            self.swift_conn.put_container(self.container_name)
        except Exception as err:
            logging.error(str(err))
            raise Exception("Update account failed with swift file server: " + str(err))

    def createObjectFile(self, filename, tempfilepath, md5, sha1):
        # if file is already existing return the file uri,
        # otherwise create the file object in FS
        try:
            meta = self.getObjectFileMeta(filename)

            if meta is not None and meta['etag'] == md5:
                file_uri = self.getobjectFileUri(filename)
                return file_uri
        except Exception:
            pass    # ignore this exception

        try:
            with open(tempfilepath, 'rb') as f:

                self.swift_conn.put_object(container=self.container_name,
                                           obj=filename, contents=f,
                                           chunk_size=self.chunk_size,
                                           headers={'X-Object-Meta-md5': md5,
                                                    'X-Object-Meta-sha1': sha1})
            # get object meta data to verify
            meta = self.getObjectFileMeta(filename)
            file_uri = None
            if meta is not None and meta['etag'] == md5:
                file_uri = self.getobjectFileUri(filename)

            return file_uri

        except ClientException as err:
            logging.error(err.msg)
            raise Exception("Create object failed with swift file server: " + str(err))

    def deleteObjectFile(self, filename):
        try:
            self.swift_conn.delete_object(container=self.container_name,
                                          obj=filename)
        except ClientException as err:
            logging.error(err.msg)
            raise Exception("Delete object failed with swift file server: " + str(err))

    def getObjectFileMeta(self, filename):
        try:
            meta = self.swift_conn.head_object(container=self.container_name,
                                               obj=filename)
            return meta
        except ClientException as err:
            logging.error(err.msg)
            raise Exception("Get object file meta failed with swift file server: " + str(err))

    def getobjectFileUri(self, filename):
        return getFileObjUri(self.storageurl, self.container_name, filename)

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

    def getObjExternalTempUrl(self, filename, expires, externalProxy):
        objectfile_uri = self.getobjectFileUri(filename)
        try:
            image_path = swiftclient.utils.generate_temp_url(
                                        objectfile_uri,
                                        expires,
                                        getattr(self, self.chosen_temp_url_key),
                                        'GET')
            image_url = externalProxy + image_path
            return image_url
        except Exception as e:
            logging.error(str(e))
            raise Exception("Get external object temp url failed with swift file server: " + str(e))

    def getObjectFile(self, location, filename):
        response, object_body = self.swift_conn.get_object(
                                              container=self.container_name,
                                              obj=filename)
        if not os.path.isdir(location):
             os.makedirs(location)
        f = open(location+ '/' + filename, 'wb')
        f.write(object_body)
        f.close()
        return response

    def getObjectContent(self, filename):
        return self.swift_conn.get_object(container=self.container_name,
                                          obj=filename)

    def close(self):
        if self.swift_conn:
            self.swift_conn.close()

def getFileObjUri(accountpath, container_name, fileobj_name):
    return urlparse('%s/%s/%s' % (accountpath, container_name, fileobj_name)).path

def main():

    module = AnsibleModule(
        argument_spec=dict(
            authtoken=dict(required=True),
            authurl=dict(required=True),
            user=dict(required=False),
            key=dict(required=False),
            tenant_name=dict(required=False, default="admin"),
            auth_version=dict(required=False, default='2.0'),
            temp_url_key=dict(required=False, default="mykey"),
            temp_url_key_2=dict(required=False, default="mykey2"),
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

    except Exception as e:
        error_msg="Exception occurred in swift_fileutil: "+str(e)

    results = {}
    results['url'] = url
    results['error_msg'] = error_msg

    module.exit_json(**results)

if __name__ == '__main__':
    main()

