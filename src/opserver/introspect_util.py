#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import urllib
import xmltodict
import json
import requests
from lxml import etree
import socket
from requests.auth import HTTPBasicAuth

class JsonDrv (object):

    def load(self, url, user, password, cert, ca_cert):
        try:
            if user and password:
                auth=HTTPBasicAuth(user, password)
            else:
                auth=None
            resp = requests.get(url, auth=auth)
            return json.loads(resp.text)
        except requests.ConnectionError, e:
            print "Socket Connection error : " + str(e)
            return None

class XmlDrv (object):

    def load(self, url, user, password, cert, ca_cert):
        try:
            if user and password:
                auth=HTTPBasicAuth(user, password)
            else:
                auth=None
            resp = requests.get(url, auth=auth, timeout=10, verify=ca_cert, cert=cert)
            return etree.fromstring(resp.text)
        except requests.ConnectionError, e:
            print "Socket Connection error : " + str(e)
            return None


class IntrospectUtilBase (object):

    def __init__(self, ip, port, drv=JsonDrv, config=None):
        self._ip = ip
        self._port = port
        self._drv = drv()
        self._force_refresh = False
        self._http_str = "http"
        self._cert = tuple()
        self._ca_cert = False
        ssl_enabled = config.introspect_ssl_enable if config else False
        if ssl_enabled:
            self._http_str = "https"
            self._cert = (config.certfile, config.keyfile)
            self._ca_cert = config.ca_cert

    def get_force_refresh(self):
        return self._force_refresh

    def set_force_refresh(self, force=False):
        self._force_refresh = force
        return self.get_force_refresh()

    def _mk_url_str(self, path, query):
        if path:
            query_str = ''
            if query:
                query_str = '?'+urllib.urlencode(query)
            if path.startswith('http:') or path.startswith('https:'):
                return path+query_str
            url = self._http_str + "://%s:%d/%s%s" % (self._ip, self._port, path, query_str)
            return url

    def dict_get(self, path='', query=None, drv=None, user=None,
                 password=None):
        if path:
            if drv is not None:
                return drv().load(self._mk_url_str(path, query), user,
                    password, self._cert, self._ca_cert)
            return self._drv.load(self._mk_url_str(path, query), user,
                password, self._cert, self._ca_cert)
    # end dict_get


class Result (dict):

    def __init__(self, d={}):
        super(Result, self).__init__()
        self.update(d)

    def xpath(self, *plist):
        ''' basic path '''
        d = self
        for p in plist:
            d = d[p]
        return d


class EtreeToDict(object):

    """Converts the xml etree to dictionary/list of dictionary."""

    def __init__(self, xpath):
        self.xpath = xpath

    def _handle_list(self, elems):
        """Handles the list object in etree."""
        a_list = []
        for elem in elems.getchildren():
            rval = self._get_one(elem, a_list)
            if 'element' in rval.keys():
                a_list.append(rval['element'])
            elif 'list' in rval.keys():
                a_list.append(rval['list'])
            else:
                a_list.append(rval)

        if not a_list:
            return None
        return a_list

    def _get_one(self, xp, a_list=None):
        """Recrusively looks for the entry in etree and converts to dictionary.

        Returns a dictionary.
        """
        val = {}

        child = xp.getchildren()
        if not child:
            val.update({xp.tag: xp.text})
            return val

        for elem in child:
            if elem.tag == 'list':
                val.update({xp.tag: self._handle_list(elem)})
            else:
                rval = self._get_one(elem, a_list)
                if elem.tag in rval.keys():
                    val.update({elem.tag: rval[elem.tag]})
                else:
                    val.update({elem.tag: rval})
        return val

    def get_all_entry(self, path):
        """All entries in the etree is converted to the dictionary

        Returns the list of dictionary/didctionary.
        """
        xps = path.xpath(self.xpath)

        if type(xps) is not list:
            return self._get_one(xps)

        val = []
        for xp in xps:
            val.append(self._get_one(xp))
        if len(val) == 1:
            return val[0]
        return val

    def find_entry(self, path, match):
        """Looks for a particular entry in the etree.
        Returns the element looked for/None.
        """
        xp = path.xpath(self.xpath)
        f = filter(lambda x: x.text == match, xp)
        if len(f):
            return f[0].text
        return None
