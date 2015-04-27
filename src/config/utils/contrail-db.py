#!/usr/bin/python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from optparse import OptionParser
import sys
import argparse

class ContrailDb(object):
    """Converts the xml etree to dictionary/list of dictionary."""

    def __init__(self, xpath):
        self.xpath = xpath
    #end __init__

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
    #end _handle_list

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
    #end _get_one

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
    #end get_all_entry

    def find_entry(self, path, match):
        """Looks for a particular entry in the etree.
        Returns the element looked for/None.
        """
        xp = path.xpath(self.xpath)
        f = filter(lambda x: x.text == match, xp)
        if len(f):
            return f[0].text
        return None
    #end find_entry

#end class ContrailDb

class IntrospectUtil(object):
    def __init__(self, ip, port, show, timeout):
        self._ip = ip
        self._port = port
        self._show = show
        self._timeout = timeout
    #end __init__

    def _mk_url_str(self, path):
        return "http://%s:%d/%s" % (self._ip, self._port, path)
    #end _mk_url_str

    def _load(self, path):
        url = self._mk_url_str(path)
        resp = requests.get(url, timeout=self._timeout)
        if resp.status_code == requests.codes.ok:
            return etree.fromstring(resp.text)
        else:
            if self._show:
                print 'URL: %s : HTTP error: %s' % (url, str(resp.status_code))
            return None

    #end _load

    def get_uve(self, tname):
        path = 'Snh_SandeshUVECacheReq?x=%s' % (tname)
        xpath = './/' + tname
        p = self._load(path)
        if p is not None:
            return ContrailDb(xpath).get_all_entry(p)
        else:
            if self._show:
                print 'UVE: %s : not found' % (path)
            return None
    #end get_uve

#end class IntrospectUtil

def show_purge(options):
    svc_introspect = IntrospectUtil('localhost', 8103, options.show, options.timeout)
    purge_status = svc_introspect.get_uve('DatabasePurgeInfo')
    if purge_status is None:
        print 'DatabasePurgeUVE not found'
    db_purge_info = purge_status['stats']
    if len(db_purge_info) == 0:
        print 'Empty DatabasePurgeStats in DatabasePurgeUVE'
    print process_status_info


def main(args_str=None):
    parser = OptionParser()
    parser.add_option('-s', '--show', dest='show',
                      default=None, help="show purge status")
    parser.add_option('-t', '--timeout', dest='timeout', type="float",
                      default=2,
                      help="timeout in seconds to use for HTTP requests to services")


    (options, args) = parser.parse_args()
    if args:
        parser.error("No arguments are permitted")
    show_purge(options)
# end main

if __name__ == "__main__":
    main()
