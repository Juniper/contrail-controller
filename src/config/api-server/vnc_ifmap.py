#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from cStringIO import StringIO
from os import makedirs
from os.path import dirname
from os.path import exists
from pprint import pformat
import signal
from time import strftime
from time import time

from bottle import Bottle
from bottle import HTTPError
from bottle import request
from bottle import response
from gevent.queue import Empty
from gevent.queue import PriorityQueue
from lxml import etree
from OpenSSL.crypto import dump_certificate
from OpenSSL.crypto import dump_privatekey
from OpenSSL.crypto import Error as crypto_error
from OpenSSL.crypto import FILETYPE_PEM
from OpenSSL.crypto import load_certificate
from OpenSSL.crypto import load_privatekey
from OpenSSL.crypto import PKey
from OpenSSL.crypto import TYPE_RSA
from OpenSSL.crypto import X509

from cfgm_common.ifmap.id import Identity
from cfgm_common.ifmap.metadata import Metadata
from cfgm_common.imid import escape
from cfgm_common.imid import get_ifmap_id_from_fq_name
from cfgm_common import jsonutils as json
from cfgm_common.utils import str_to_class
from gen.resource_xsd import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

"""
Layer that transforms VNC config objects to ifmap representation, store them in
memory and serves them to controllers though minimalist HTTP IF-MAP server.
"""


class VncIfmapServer(Bottle):
    r"""Limited IF-MAP v2 server for Contrail.

    Stores in memory the Contrail IF-MAP graph and exposes a limited IF-MAP
    HTTP(S) server that permits control nodes to subscribe to the graph's root
    and pool it.

    _graph is dict of ident_names where value for each key is dict with keys
    'ident' and 'links'
    - 'ident' has ident xml element
    - 'links' is a dict with keys of concat('<meta-name> <ident-name>')
      and values of dict with 'meta' which has meta xml element and 'other'
      which has other ident xml element

    cls._graph['contrail:network-ipam:default-domain:default-project:ipam2'] =
    {
        'ident': u'<identity name=... />',
        'links': {
            'contrail:id-perms': {
                'meta': u'<metadata><contrail:...>...</metadata>'
            },
            'contrail:project-network-ipam contrail:project:default-domain:\
                default-project': {
                    'other': u'<identity name="contrail:project:default-domain\
                        :default-project" type="other" ... />',
                    'meta': u'<metadata><contrail:...>...</metadata>'
            },
            'contrail:virtual-network-network-ipam contrail:virtual-network:\
                default-domain:default-project:vn2': {
                'other': u'<identity name="contrail:project:virtual-network:\
                    default-domain:default-project:vn2" type="other" ... />',
                'meta': u'<metadata><contrail:...>...</metadata>'
            }
        }
    }
    """

    # NOTE(ethuleau):
    #    - use tuple for link keys instead of string (link with other ident)
    #    - Not sure we need to store reverse link in the dict graph
    #    - Purge stale subscriptions
    #    -

    # dict contains ifmap graph (as describe above)
    _graph = dict()
    # list of all subscribers indexed by session-id
    _subscribe_list = []
    # XML envelope header
    _RSP_ENVELOPE = \
        """<?xml version="1.0" encoding="UTF-8" standalone="yes"?> """\
        """<env:Envelope xmlns:ifmap="http://www.trustedcomputinggroup.org"""\
        """/2010/IFMAP/2" """\
        """xmlns:env="http://www.w3.org/2003/05/soap-envelope" """\
        """xmlns:meta="http://www.trustedcomputinggroup.org"""\
        """/2010/IFMAP-METADATA/2" """\
        """xmlns:contrail="http://www.contrailsystems.com/vnc_cfg.xsd"> """\
        """<env:Body><ifmap:response> %(result)s """\
        """</ifmap:response></env:Body></env:Envelope>"""
    # Max message item size in search, update and delete result poll request
    _ITEM_MAX_MSG_SIZE = 7500000 - (len(_RSP_ENVELOPE) + 66)

    def __init__(self, db_client_mgr, args):
        super(VncIfmapServer, self).__init__()
        self._host = args.ifmap_listen_ip
        self._port = args.ifmap_listen_port
        self._db_client_mgr = db_client_mgr
        self._key_path = args.ifmap_key_path
        self._cert_path = args.ifmap_cert_path
        self._credentials = args.ifmap_credentials

        # Set the signal handler to snapshot ifmap graph in temporary file
        signal.signal(signal.SIGUSR2, VncIfmapServer._dump_ifmap_dict)

        # Define bottle routes
        self.get('/', callback=self._export_xml)
        self.post('/', callback=self._call)

    def _log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)

    def _create_self_signed_cert(self):
        key = None
        cert = None

        if exists(self._key_path):
            try:
                with open(self._key_path, 'r') as key_file:
                    key = load_privatekey(FILETYPE_PEM, key_file.read())
            except crypto_error:
                pass

        if exists(self._cert_path):
            try:
                with open(self._cert_path, 'r') as cert_file:
                    cert = load_certificate(FILETYPE_PEM, cert_file.read())
            except crypto_error:
                pass

        if key is None:
            key = PKey()
            key.generate_key(TYPE_RSA, 2048)
            if not exists(dirname(self._key_path)):
                makedirs(dirname(self._key_path))
            with open(self._key_path, "w") as key_file:
                key_file.write(
                    dump_privatekey(FILETYPE_PEM, key))

        if cert is None:
            cert = X509()
            cert.get_subject().C = "US"
            cert.get_subject().ST = "California"
            cert.get_subject().L = "Palo Alto"
            cert.get_subject().O = "Juniper Networks"
            cert.get_subject().OU = "Contrail"
            cert.get_subject().CN = "Contrail's IF-MAP v2"
            cert.set_serial_number(1)
            cert.gmtime_adj_notBefore(0)
            cert.gmtime_adj_notAfter(10*365*24*60*60)  # Ten years
            cert.set_issuer(cert.get_subject())
            cert.set_pubkey(key)
            cert.sign(key, 'sha1')

            if not exists(dirname(self._cert_path)):
                makedirs(dirname(self._cert_path))
            with open(self._cert_path, 'w') as cert_file:
                cert_file.write(dump_certificate(FILETYPE_PEM, cert))

    def _validate_session_id(self, method, xml_body):
        if method == 'newSession':
            return

        session_id = xml_body[0].get('session-id')
        if (session_id is None or
                int(session_id) > (len(self._subscribe_list) - 1)):
            msg = ("Session ID %s was not found. Please create a new session."
                   % session_id)
            self._log(msg, SandeshLevel.SYS_WARN)
            result = etree.Element('errorResult', errorCode='InvalidSessionID')
            err_str = etree.SubElement(result, 'errorString')
            err_str.text = msg
            return self._RSP_ENVELOPE % {'result': etree.tostring(result)}

    def _export_xml(self):
        response.content_type = 'text/xml; charset="UTF-8"'
        buffer = StringIO()
        VncIfmapServer._export_root_graph(buffer)
        result = buffer.getvalue()
        result = ('<pollResult><searchResult name="root">%s'
                  '</searchResult></pollResult>' % buffer.getvalue())
        buffer.close
        return self._RSP_ENVELOPE % {'result': result}

    @classmethod
    def _export_root_graph(cls, outfile):
        graph = cls._graph.copy()
        visited_links = set()
        for id_name in graph:
            ident = graph[id_name]['ident']
            property_items = ''
            for link_key, link_info in graph[id_name]['links'].iteritems():
                if link_info is None or link_info.get('meta') is None:
                    continue
                if link_info.get('other') is not None:
                    if '%s %s' % (link_info['other'], ident) in visited_links:
                        continue
                    outfile.write('<resultItem>%s%s%s</resultItem>' %
                                  (ident, link_info['other'],
                                   link_info['meta']))
                    visited_links.add('%s %s' % (ident, link_info['other']))
                else:
                    property_items += link_info['meta'][10:-11]
            if property_items:
                outfile.write('<resultItem>%s<metadata>%s'
                              '</metadata></resultItem>' %
                              (ident, property_items))

    @classmethod
    def _dump_ifmap_dict(cls, signum, frame):
        file_name = ('/tmp/api-server-ifmap-dict_%s.txt' %
                     strftime('%Y%m%d-%H%M%S'))
        with open(file_name, "w") as file:
            file.write(pformat(cls._graph))

    @classmethod
    def get_links(cls, id_name):
        """Return a copy of links of an IF-MAP identity."""
        if id_name in cls._graph:
            return cls._graph[id_name].get('links', {}).copy()
        return {}

    @classmethod
    def update(cls, id1_name, id2_name=None, metadatas=None):
        """Create or update identity with link and metadata provided."""
        if metadatas is None or not metadatas:
            return

        id1_str = unicode(Identity(name=id1_name, type="other",
                                   other_type="extended"))
        if id1_name not in cls._graph:
            cls._graph[id1_name] = {'ident': id1_str, 'links': {}}

        if id2_name is None:
            property_items = ''
            for metadata in metadatas:
                metadata_name = metadata._Metadata__name
                meta_string = unicode(metadata)

                link_key = metadata_name
                link_info = {'meta': meta_string}
                cls._graph[id1_name]['links'][link_key] = link_info
                property_items += meta_string[10:-11]
            if property_items:
                items = ('<resultItem>%s<metadata>%s</metadata></resultItem>' %
                         (id1_str, property_items))
        else:
            id2_str = unicode(Identity(name=id2_name, type="other",
                                       other_type="extended"))
            if id2_name not in cls._graph:
                cls._graph[id2_name] = {'ident': id2_str, 'links': {}}

            items = ''
            for metadata in metadatas:
                metadata_name = metadata._Metadata__name
                meta_string = unicode(metadata)

                link_key = '%s %s' % (metadata_name, id2_name)
                link_info = {'meta': meta_string, 'other': id2_str}
                cls._graph[id1_name]['links'][link_key] = link_info
                items += "<resultItem>%s%s%s</resultItem>" % (id1_str, id2_str,
                                                              meta_string)

                link_key = '%s %s' % (metadata_name, id1_name)
                link_info = {'meta': meta_string, 'other': id1_str}
                cls._graph[id2_name]['links'][link_key] = link_info

        for queue in cls._subscribe_list:
            if queue is not None:
                queue.put((2, time(), 'updateResult', items))

    @classmethod
    def delete(cls, id_name, link_keys=None):
        """Delete metadata and link of an identity.

        If no link or metadata provided, all the identity is evinced
        """
        if cls._graph.get(id_name) is None:
            return
        id_str = cls._graph[id_name].get('ident')
        if id_str is None or cls._graph[id_name].get('links') is None:
            # Stale ifmap identity, remove it and ignore it
            del cls._graph.get[id_name]
            return

        if link_keys:
            items = ''
            property_items = ''
            for link_key in link_keys:
                link_info = cls._graph[id_name]['links'].pop(link_key, None)
                if link_info is None:
                    continue
                metadata = link_info['meta']
                if link_info.get('other') is None:
                    property_items += metadata[10:-11]
                else:
                    meta_name, _, other_name = link_key.partition(' ')
                    rev_link_key = '%s %s' % (meta_name, id_name)
                    if other_name in cls._graph:
                        cls._graph[other_name]['links'].pop(rev_link_key, None)
                    items += "<resultItem>%s%s%s</resultItem>" %\
                             (id_str, link_info['other'], link_info['meta'])
                    # delete ident if no links left
                    if (other_name in cls._graph and
                            not cls._graph[other_name].get('links')):
                        del cls._graph[other_name]
            if property_items:
                items += ('<resultItem>%s<metadata>%s</metadata></resultItem>'
                          % (id_str, property_items))
            # delete ident if no links left
            if not cls._graph[id_name]['links']:
                del cls._graph[id_name]

            for queue in cls._subscribe_list:
                if queue is not None:
                    queue.put((2, time(), 'deleteResult', items))
        else:
            # No links provided, delete all ident's links
            cls.delete(id_name, cls.get_links(id_name).keys())

    @classmethod
    def reset_graph(cls):
        """Wipe all IF-MAP graph and revoke all subscriptions."""
        cls._graph.clear()
        cls._subscribe_list = []

    def _call(self):
        if request.auth not in self._credentials:
            error = HTTPError(401, "Access denied")
            error.add_header('WWW-Authenticate', 'Basic realm="private"')
            return error

        method = request.headers.get('SOAPAction')
        xml_body = etree.fromstring(request.body.read())[0]
        response.content_type = 'text/xml; charset="UTF-8"'

        valide_session_response = self._validate_session_id(method, xml_body)
        if valide_session_response is not None:
            return valide_session_response

        if method == 'newSession':
            result = etree.Element('newSessionResult')
            session_id = len(self._subscribe_list)
            result.set("session-id", str(session_id))
            result.set("ifmap-publisher-id", "111")
            result.set("max-poll-result-size", str(self._ITEM_MAX_MSG_SIZE))
            self._subscribe_list.append(None)
            msg = "New session %d established." % session_id
            self._log(msg, SandeshLevel.SYS_DEBUG)
            return self._RSP_ENVELOPE % {'result': etree.tostring(result)}
        elif method == 'subscribe':
            session_id = int(xml_body[0].get('session-id'))
            self._subscribe_list[session_id] = PriorityQueue()
            buffer = StringIO()
            try:
                VncIfmapServer._export_root_graph(buffer)
                self._subscribe_list[session_id].put(
                    (1, time(), 'searchResult', buffer.getvalue()))
            finally:
                buffer.close()
            result = etree.Element('subscribeReceived')
            msg = "Session %d has subscribed to the root graph" % session_id
            self._log(msg, SandeshLevel.SYS_DEBUG)
            return self._RSP_ENVELOPE % {'result': etree.tostring(result)}
        elif method == 'poll':
            session_id = int(xml_body[0].get('session-id'))
            queue = self._subscribe_list[session_id]
            if queue is None:
                msg = "Session ID %d did not subscribed to the graph's root. "\
                      "Please subscribe before polling." % session_id
                self._log(msg, SandeshLevel.SYS_WARN)
                result = etree.Element('errorResult', errorCode='AccessDenied')
                err_str = etree.SubElement(result, 'errorString')
                err_str.text = msg
                return self._RSP_ENVELOPE % {'result': etree.tostring(result)}

            _, _, action, items = queue.get()
            while True:
                try:
                    _, _, new_action, new_item = queue.peek(timeout=1)
                except Empty:
                    break
                if new_action != action:
                    break
                if (len(items) + len(new_item)) > self._ITEM_MAX_MSG_SIZE:
                    break
                try:
                    items += queue.get_nowait()[3]
                except Empty:
                    break

            poll_str = ('<pollResult><%s name="root">%s</%s></pollResult>' %
                        (action, items, action))
            msg = "Session %d polled and get %s" % (session_id, action)
            self._log(msg, SandeshLevel.SYS_DEBUG)
            return self._RSP_ENVELOPE % {'result': poll_str}
        elif method == 'search':
            # grab ident string; lookup graph with match meta and return
            start_name = xml_body[0][0].get('name')
            match_links = xml_body[0].get('match-links', 'all')
            if match_links != 'all':
                match_links = set(match_links.split(' or '))
            result_filter = xml_body[0].get('result-filter', 'all')
            if result_filter != 'all':
                result_filter = set(result_filter.split(' or '))

            visited_nodes = set([])
            result_items = []
            def visit_node(ident_name):
                if ident_name in visited_nodes:
                    return
                visited_nodes.add(ident_name)
                # add all metas on current to result, visit further nodes
                to_visit_nodes = set([])
                ident_str = VncIfmapServer._graph[ident_name]['ident']
                links = VncIfmapServer._graph[ident_name]['links']
                property_items = ''
                for link_key, link_info in links.iteritems():
                    meta_name = link_key.split()[0]
                    if 'other' in link_info:
                        to_visit_nodes.add(link_key.split()[1])
                        if (result_filter != 'all' and
                                meta_name in result_filter):
                            result_items.append(
                                '<resultItem>%s%s%s</resultItem>' % (ident_str,
                                link_info['other'], link_info['meta']))
                    elif (result_filter != 'all' and
                            meta_name in result_filter):
                        property_items += link_info['meta'][10:-11]
                if property_items:
                    result_items.append('<resultItem>%s<metadata>%s'
                                        '</metadata></resultItem>' %
                                        (ident_str, property_items))

                # all metas on ident walked
                for new_node in to_visit_nodes:
                    visit_node(new_node)
            # end visit_node

            visit_node(start_name)

            search_str = ('<searchResult>%s</searchResult>' %
                          ''.join(result_items))
            return VncIfmapServer._RSP_ENVELOPE % {'result': search_str}
        else:
            msg = "IF-MAP method '%s' is not implemented." % method
            self._log(msg, level=SandeshLevel.SYS_DEBUG)
            result = etree.Element('errorResult', errorCode='InvalidMethod')
            err_str = etree.SubElement(result, 'errorString')
            err_str.text = msg
            return self._RSP_ENVELOPE % {'result': etree.tostring(result)}

    def run_server(self):
        """Start to bind custom IF-MAP server."""
        self._create_self_signed_cert()

        try:
            self.run(host=self._host, port=self._port, server='gevent',
                     server_side=True, keyfile=self._key_path,
                     certfile=self._cert_path)
        except KeyboardInterrupt:
            # quietly handle Ctrl-C
            pass


class VncIfmapDb(object):
    """Transforms VNC config objects to IF-MAP representation.

    Transforms VNC config objects to ifmap representation and store them in
    the VncIfmapServer _graph dict directly, don't use the IF-MAP HTTP API.
    """

    # If the property is not relevant at all, define the property
    # with None. If it is partially relevant, then define the fn.
    # which would handcraft the generated xml for the object.
    IFMAP_PUBLISH_SKIP_LIST = {
        # Format - <prop_field> : None | <Handler_fn>
        u"perms2": None,
        u"id_perms": '_build_idperms_ifmap_obj',
    }

    def __init__(self, db_client_mgr):
        self._db_client_mgr = db_client_mgr

    def _log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)

    @staticmethod
    def _build_idperms_ifmap_obj(prop_field, values):
        prop_xml = u'<uuid><uuid-mslong>'
        prop_xml += unicode(json.dumps(values[u'uuid'][u'uuid_mslong']))
        prop_xml += u'</uuid-mslong><uuid-lslong>'
        prop_xml += unicode(json.dumps(values[u'uuid'][u'uuid_lslong']))
        prop_xml += u'</uuid-lslong></uuid><enable>'
        prop_xml += unicode(json.dumps(values[u'enable']))
        prop_xml += u'</enable>'
        return prop_xml

    @staticmethod
    def object_alloc(obj_class, parent_res_type, fq_name):
        """Allocate IF-MAP ID of the resource and its parent."""
        res_type = obj_class.resource_type
        my_fqn = ':'.join(fq_name)
        parent_fqn = ':'.join(fq_name[:-1])

        ifmap_id = 'contrail:%s:%s' % (res_type, my_fqn)
        if parent_fqn:
            if parent_res_type is None:
                err_msg = "Parent: %s type is none for: %s" % (parent_fqn,
                                                               my_fqn)
                return False, (409, err_msg)
            parent_ifmap_id = 'contrail:' + parent_res_type + ':' + parent_fqn
        else:  # parent is config-root
            parent_ifmap_id = 'contrail:config-root:root'

        return True, (escape(ifmap_id), escape(parent_ifmap_id))

    def _object_set(self, obj_class, ifmap_id, obj_dict, existing_links=None):
        # Properties Metadata
        metadatas = []
        for prop_field in obj_class.prop_fields:
            prop_value = obj_dict.get(prop_field)
            if prop_value is None:
                continue
            # construct object of xsd-type and get its xml repr
            # e.g. virtual_network_properties
            prop_field_types = obj_class.prop_field_types[prop_field]
            is_simple = not prop_field_types['is_complex']
            prop_type = prop_field_types['xsd_type']
            # e.g. virtual-network-properties
            meta_name = obj_class.prop_field_metas[prop_field]
            meta_long_name = 'contrail:%s' % meta_name
            meta_value = ''
            meta_elements = ''

            if prop_field in self.IFMAP_PUBLISH_SKIP_LIST:
                # Field not relevant, skip publishing to IfMap
                if not self.IFMAP_PUBLISH_SKIP_LIST[prop_field]:
                    continue
                # Call the handler fn to generate the relevant fields.
                try:
                    func = getattr(self,
                                   self.IFMAP_PUBLISH_SKIP_LIST[prop_field])
                    meta_elements = func(prop_field, prop_value)
                except AttributeError:
                    log_str = ("%s is marked for partial publish to Ifmap but "
                               "handler not defined" % prop_field)
                    self._log(log_str, level=SandeshLevel.SYS_DEBUG)
                    continue
            elif is_simple:
                meta_value = escape(str(prop_value))
            else:  # complex type
                prop_cls = str_to_class(prop_type, __name__)
                buf = StringIO()
                # perms might be inserted at server as obj.
                # obj construction diff from dict construction.
                if isinstance(prop_value, dict):
                    prop_cls(**prop_value).exportChildren(buf,
                                                          level=1,
                                                          name_=meta_name,
                                                          pretty_print=False)
                elif isinstance(prop_value, list):
                    for elem in prop_value:
                        if isinstance(elem, dict):
                            prop_cls(**elem).exportChildren(buf,
                                                            level=1,
                                                            name_=meta_name,
                                                            pretty_print=False)
                        else:
                            elem.exportChildren(buf,
                                                level=1,
                                                name_=meta_name,
                                                pretty_print=False)
                else:  # object
                    prop_value.exportChildren(buf,
                                              level=1,
                                              name_=meta_name,
                                              pretty_print=False)
                meta_elements = buf.getvalue()
                buf.close()
            meta = Metadata(meta_name,
                            meta_value,
                            {'ifmap-cardinality': 'singleValue'},
                            ns_prefix='contrail',
                            elements=meta_elements)
            # If obj is new (existing metas is none) or
            # if obj does not have this meta (or)
            # or if the meta is different from what we have currently,
            # then update
            if (not existing_links or
                    meta_long_name not in existing_links or
                    ('meta' in existing_links[meta_long_name] and
                     unicode(meta) != existing_links[meta_long_name]['meta'])):
                metadatas.append(meta)
        # end for all property types
        VncIfmapServer.update(ifmap_id, metadatas=metadatas)

        # References Metadata
        for ref_field in obj_class.ref_fields:
            refs = obj_dict.get(ref_field)
            if not refs:
                continue

            for ref in refs:
                ref_fq_name = ref['to']
                ref_fld_types_list = list(obj_class.ref_field_types[ref_field])
                ref_res_type = ref_fld_types_list[0]
                ref_link_type = ref_fld_types_list[1]
                ref_meta = obj_class.ref_field_metas[ref_field]
                ref_ifmap_id = get_ifmap_id_from_fq_name(ref_res_type,
                                                         ref_fq_name)
                ref_data = ref.get('attr')
                if ref_data:
                    buf = StringIO()
                    attr_cls = str_to_class(ref_link_type, __name__)
                    attr_cls(**ref_data).exportChildren(buf,
                                                        level=1,
                                                        name_=ref_meta,
                                                        pretty_print=False)
                    ref_link_xml = buf.getvalue()
                    buf.close()
                else:
                    ref_link_xml = ''
                metadata = Metadata(ref_meta,
                                    '',
                                    {'ifmap-cardinality': 'singleValue'},
                                    ns_prefix='contrail',
                                    elements=ref_link_xml)
                VncIfmapServer.update(ifmap_id, ref_ifmap_id, [metadata])
        # end for all ref types
        return (True, '')

    def object_create(self, obj_ids, obj_dict):
        """From resource ID, create the corresponding IF-MAP identity."""
        obj_ifmap_id = obj_ids['imid']
        obj_type = obj_ids['type']
        obj_class = self._db_client_mgr.get_resource_class(obj_type)

        if 'parent_type' not in obj_dict:
            # parent is config-root
            parent_type = 'config-root'
            parent_ifmap_id = 'contrail:config-root:root'
        else:
            parent_type = obj_dict['parent_type']
            parent_ifmap_id = obj_ids.get('parent_imid', None)

        # Parent Link Meta
        parent_cls = self._db_client_mgr.get_resource_class(parent_type)
        parent_link_meta = parent_cls.children_field_metas.get(
            '%ss' % (obj_type))
        if parent_link_meta:
            metadata = Metadata(parent_link_meta,
                                '',
                                {'ifmap-cardinality': 'singleValue'},
                                ns_prefix='contrail')
            VncIfmapServer.update(parent_ifmap_id, obj_ifmap_id,
                                  metadatas=[metadata])

        return self._object_set(obj_class, obj_ifmap_id, obj_dict)

    def object_update(self, obj_cls, new_obj_dict):
        """From resource ID, update the corresponding IF-MAP identity."""
        ifmap_id = get_ifmap_id_from_fq_name(obj_cls.resource_type,
                                             new_obj_dict['fq_name'])
        # read in refs from ifmap to determine which ones become inactive after
        # update
        existing_links = VncIfmapServer.get_links(ifmap_id)

        if not existing_links:
            # UPDATE notify queued before CREATE notify, Skip publish to IFMAP.
            return (True, '')

        # remove properties that are no longer active
        delete_links = []
        for prop, meta in obj_cls.prop_field_metas.items():
            meta_name = 'contrail:%s' % meta
            if meta_name in existing_links and new_obj_dict.get(prop) is None:
                delete_links.append(meta_name)

        # remove refs that are no longer active
        refs = dict((obj_cls.ref_field_metas[rf],
                     obj_cls.ref_field_types[rf][0])
                    for rf in obj_cls.ref_fields)
        # refs = {'virtual-network-qos-forwarding-class': 'qos-forwarding-...',
        #         'virtual-network-network-ipam': 'network-ipam',
        #         'virtual-network-network-policy': 'network-policy',
        #         'virtual-network-route-table': 'route-table'}
        for meta, ref_res_type in refs.items():
            old_refs = set(link_key for link_key in existing_links.keys()
                           if meta in link_key)
            new_refs = set()
            ref_obj_type = self._db_client_mgr.get_resource_class(
                ref_res_type).object_type
            for ref in new_obj_dict.get(ref_obj_type + '_refs', []):
                to_imid = get_ifmap_id_from_fq_name(ref_res_type, ref['to'])
                new_refs.add('contrail:%s %s' % (meta, to_imid))

            delete_links.extend(list(old_refs - new_refs))

        if delete_links:
            VncIfmapServer.delete(ifmap_id, delete_links)

        return self._object_set(obj_cls, ifmap_id, new_obj_dict,
                                existing_links)

    def object_delete(self, obj_ids):
        """From resource ID, delete the corresponding IF-MAP identity."""
        VncIfmapServer.delete(obj_ids['imid'])
        return (True, '')
