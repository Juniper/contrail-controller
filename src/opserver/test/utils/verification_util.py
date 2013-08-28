#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import urllib2
import xmltodict
import json
import requests
from lxml import etree
import socket

class JsonDrv (object):
    def _http_con (self, url):
        return urllib2.urlopen (url)

    def load (self, url):
        return json.load (self._http_con (url))

class XmlDrv (object):
    def load (self, url):
        try: 
            resp=requests.get(url)
            return etree.fromstring(resp.text)
        except requests.ConnectionError,e:
            print "Socket Connection error : " + str(e)
            return None

class VerificationUtilBase (object):
    def __init__ (self, ip, port, drv=JsonDrv):
        self._ip   = ip
        self._port = port
        self._drv  = drv ()
        self._force_refresh = False

    def get_force_refresh (self):
        return self._force_refresh

    def set_force_refresh (self, force=False):
        self._force_refresh = force
        return self.get_force_refresh ()

    def _mk_url_str (self, path):
        if path:
            if path.startswith ('http:'):
                return path
            return "http://%s:%d/%s" % (self._ip, self._port, path)

    def dict_get (self, path=''):
        try: 
            if path:
                return self._drv.load (self._mk_url_str (path))
        except urllib2.HTTPError:
            return None
    #end dict_get

class Result (dict):
    def __init__ (self, d={}):
        super (Result, self).__init__ ()
        self.update (d)

    def xpath (self, *plist):
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
