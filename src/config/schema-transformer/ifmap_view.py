#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import logging
from pprint import pformat
import argparse
import sys
reload(sys)
sys.setdefaultencoding('UTF8')

import lxml.etree as et
import StringIO
import re
import json

from cfgm_common.ifmap.client import client
from cfgm_common.ifmap.request import NewSessionRequest, SearchRequest
from cfgm_common.ifmap.response import newSessionResult
from cfgm_common.ifmap.id import Identity

logger = logging.getLogger(__name__)


class IfmapSearcher():

    def __init__(self):
        """
        .. attention:: username/passwd from right place
        """
        self._CONTRAIL_XSD = "http://www.contrailsystems.com/vnc_cfg.xsd"

        self._NAMESPACES = {
            'a': 'http://www.w3.org/2003/05/soap-envelope',
            'b': 'http://www.trustedcomputinggroup.org/2010/IFMAP/2',
            'c': self._CONTRAIL_XSD
        }

        namespaces = {
            'env':   "http://www.w3.org/2003/05/soap-envelope",
            'ifmap':   "http://www.trustedcomputinggroup.org/2010/IFMAP/2",
            'meta':   "http://www.trustedcomputinggroup.org/"
                      "2010/IFMAP-METADATA/2",
            'contrail':   self._CONTRAIL_XSD
        }

        self.parse_args()
        if self._args.verbose:
            logger.setLevel('DEBUG')
        mapclient = client(("%s" % (self._args.ip),
                            "%s" % (self._args.port)),
                           self._args.username,
                           self._args.password, namespaces)
        result = mapclient.call('newSession', NewSessionRequest())
        mapclient.set_session_id(newSessionResult(result).get_session_id())
        mapclient.set_publisher_id(newSessionResult(result).get_publisher_id())

        self._mapclient = mapclient
        self.soap_doc = None
    # end __init__

    def _search(self, start_id, match_meta=None, result_meta=None):
        # set ifmap search parmeters
        srch_params = {}
        srch_params['max-depth'] = self._args.max_depth
        srch_params['max-size'] = '50000000'

        if match_meta is not None:
            srch_params['match-links'] = match_meta

        if result_meta is not None:
            # all => don't set result-filter, so server returns all id + meta
            if result_meta == "all":
                pass
            else:
                srch_params['result-filter'] = result_meta
        else:
            # default to return match_meta metadata types only
            srch_params['result-filter'] = match_meta

        mapclient = self._mapclient
        srch_req = SearchRequest(mapclient.get_session_id(), start_id,
                                 search_parameters=srch_params
                                 )
        result = mapclient.call('search', srch_req)

        return result
    # end _search

    def parse_args(self):
        # Eg. python ifmap_search.py 192.168.1.17 8443 test2 test2 --identifier 
        parser = argparse.ArgumentParser(
            description="Display IFMAP configuration")
        parser.add_argument('ip', default='localhost', nargs='?',
            help="Hostname/IP address of ifmap server")
        parser.add_argument('--port', default='8443', nargs='?',
             help="Port of ifmap server")
        parser.add_argument('username',
            help="Username known to ifmap server")
        parser.add_argument('password',
            help="Password known to ifmap server")
        parser.add_argument('--search-identifier',
            default = 'contrail:config-root:root',
            help = "IFMAP identifier to search e.g."
                   "contrail:virtual-network:default-domain:admin:test-net")
        parser.add_argument('--search-metas',
            help = "Comma separated metadata names to search e.g."
                   "contrail:project-virtual-network")
        parser.add_argument('--result-metas',
            help = "Comma separated metadata names to pick in response e.g."
                   "contrail:project-virtual-network")
        parser.add_argument('--max-depth', default='10',
            help = "Maximum depth to span from search-identifier e.g. 1")
        parser.add_argument('--verbose',
            action='store_true', default=False)
        self._args = parser.parse_args()
    # end parse_args

    def search(self):
        if self._args.search_metas:
            search_metas = ' or '.join(
                self._args.search_metas.split(','))
        else:
            search_metas = None
        if self._args.result_metas:
            result_metas = ' or '.join(
                self._args.result_metas.split(','))
        else:
            result_metas = None
        start_id = str(
            Identity(name=self._args.search_identifier,
            type='other', other_type='extended'))
        soap_result = self._search(
            start_id,
            search_metas,
            result_metas)
        self.soap_doc = et.fromstring(soap_result)
        self.result_items = self.soap_doc.xpath(
            '/a:Envelope/a:Body/b:response/searchResult/resultItem',
            namespaces={'a': 'http://www.w3.org/2003/05/soap-envelope',
            'b': 'http://www.trustedcomputinggroup.org/2010/IFMAP/2'})

        #vnc_viewer._args.outfile.write(et.tostring(soap_doc, pretty_print=True))
    # end search

    def print_search_results(self):
        if self._args.verbose:
            if self.soap_doc is None:
                logger.error("Not able to get a result for search")
                return
            logger.debug("Raw search result:\n%s",
                et.tostring(self.soap_doc, pretty_print=True))

        logger.info("Number of result items for search = %s",
            len(self.result_items))

        props = []
        links = []
        for r_item in self.result_items:
            if (len(r_item) == 2 and 
                r_item[0].get('name') == self._args.search_identifier):
                props.extend([x.tag for x in r_item[1]])
            elif len(r_item) == 3:
                if r_item[1].get('name') != self._args.search_identifier:
                    links.append(r_item[1].get('name'))
                else:
                    links.append(r_item[0].get('name'))

        logger.info("Properties on identifier = %s", pformat(props))
        logger.info("Links from identifier = %s", pformat(links))
    # end print_search_results
# end IfmapSearcher

def main():
     # Example usage:
     # python ifmap_search.py localhost 8443 <username> <password>
     #     --search-identifier contrail:virtual-network:default-domain:admin:test-net 
     #     --search-metas contrail:project-virtual-network --verbose --max-depth 1
    logger.setLevel('INFO')
    logformat = logging.Formatter("%(levelname)s: %(message)s")
    stdout = logging.StreamHandler()
    stdout.setLevel('DEBUG')
    stdout.setFormatter(logformat)
    logger.addHandler(stdout)

    searcher = IfmapSearcher()
    searcher.search()
    searcher.print_search_results()
# end main

if __name__ == '__main__':

    main()
