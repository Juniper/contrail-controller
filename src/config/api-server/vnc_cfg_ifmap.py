#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from cStringIO import StringIO
import signal
from time import strftime
from time import time

from bottle import auth_basic
from bottle import Bottle
from bottle import request
from bottle import response
from gevent.queue import Empty
from gevent.queue import PriorityQueue
from lxml import etree

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
                    'other': <Element identity at 0x2b3eaa0>,
                    'meta': u'<metadata><contrail:...>...</metadata>'
            },
            'contrail:virtual-network-network-ipam contrail:virtual-network:\
                default-domain:default-project:vn2': {
                'other': <Element identity at 0x2b3ee10>,
                'meta': u'<metadata><contrail:...>...</metadata>'
            }
        }
    }
    """

    # NOTE(ethuleau):
    #    - needs to unicode indetities before store them in memory?
    #    - use tuple for link keys instead of string (link with other ident)
    #    - manage authentication
    #    - Not sure we need to store reverse link in the dict graph
    #    - Purge stale subscriptions
    #    - implements search request for tools like ifmap-view
    #    - add to conf param host, port and ssl options
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

    def __init__(self, db_client_mgr):
        super(VncIfmapServer, self).__init__()
        self._db_client_mgr = db_client_mgr

        # Set the signal handler to snapshot ifmap graph in temporary file
        signal.signal(signal.SIGUSR2, VncIfmapServer._dump_ifmap_dict)

        # Define bottle routes
        self.get('/', callback=self._export_xml)
        self.post('/', callback=self._call)

    def _log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)

    def _check_authentication(username, password):
        return True

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
            property_item = ''
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
                    property_item += link_info['meta'][10:-11]
            if property_item:
                outfile.write('<resultItem>%s<metadata>%s'
                              '</metadata></resultItem>' %
                              (ident, property_item))

    @classmethod
    def _dump_ifmap_dict(cls, signum, frame):
        file_name = ('/tmp/api-server-ifmap-dict_%s.txt' %
                     strftime('%Y%m%d-%H%M%S'))
        with open(file_name, "w") as file:
            file.write(pformat(cls._graph))

    @classmethod
    def get_links(cls, id_name):
        if id_name in cls._graph:
            return cls._graph[id_name].get('links', {}).copy()
        return {}

    @classmethod
    def update(cls, id1_name, id2_name=None, metadatas=None):
        if metadatas is None or not metadatas:
            return

        id1_str = unicode(Identity(name=id1_name, type="other",
                                   other_type="extended"))
        if id1_name not in cls._graph:
            cls._graph[id1_name] = {'ident': id1_str, 'links': {}}

        if id2_name is None:
            property_item = ''
            for metadata in metadatas:
                metadata_name = metadata._Metadata__name[9:]
                meta_string = unicode(metadata)

                link_key = metadata_name
                link_info = {'meta': meta_string}
                cls._graph[id1_name]['links'][link_key] = link_info
                property_item += meta_string[10:-11]
            if property_item:
                items = ('<resultItem>%s<metadata>%s</metadata></resultItem>' %
                         (id1_str, property_item))
        else:
            id2_str = unicode(Identity(name=id2_name, type="other",
                                       other_type="extended"))
            if id2_name not in cls._graph:
                cls._graph[id2_name] = {'ident': id2_str, 'links': {}}

            items = ''
            for metadata in metadatas:
                metadata_name = metadata._Metadata__name[9:]
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
        if cls._graph.get(id_name) is None:
            return
        id_str = cls._graph[id_name].get('ident')
        if id_str is None or cls._graph[id_name].get('links') is None:
            # Stale ifmap identity, remove it and ignore it
            del cls._graph.get[id_name]
            return

        if link_keys:
            items = ''
            property_item = ''
            for link_key in link_keys:
                link_info = cls._graph[id_name]['links'].pop(link_key, None)
                if link_info is None:
                    continue
                metadata = link_info['meta']
                if link_info.get('other') is None:
                    property_item += metadata[10:-11]
                else:
                    meta_name = link_key.split()[0]
                    other_name = link_key.split()[1]
                    rev_link_key = '%s %s' % (meta_name, id_name)
                    if other_name in cls._graph:
                        cls._graph[other_name]['links'].pop(rev_link_key, None)
                    items += "<resultItem>%s%s%s</resultItem>" %\
                             (id_str, link_info['other'], link_info['meta'])
                    # delete ident if no links left
                    if not cls._graph[other_name]['links']:
                        del cls._graph[other_name]
            if property_item:
                items += ('<resultItem>%s<metadata>%s</metadata></resultItem>'
                          % (id_str, property_item))
            # delete ident if no links left
            if not cls._graph[id_name]['links']:
                del cls._graph[id_name]

            for queue in cls._subscribe_list:
                if queue is not None:
                    queue.put((2, time(), 'deleteResult', items))
        else:
            # No links provided, delete all ident's links
            cls.delete(id_name, cls.get_links(id_name))

    @classmethod
    def reset_graph(cls):
        cls._graph.clear()
        cls._subscribe_list = []

    @auth_basic(_check_authentication)
    def _call(self):
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
        else:
            msg = "Don't know IF-MAP '%s' request. Ignore it." % method
            self._log(msg, level=SandeshLevel.SYS_DEBUG)

    def run_http(self, host, port, workers=1):
        try:
            self.run(host=host, port=port, server='gevent', spawn=workers)
        except KeyboardInterrupt:
            # quietly handle Ctrl-C
            pass

    def run_https(self, host, port, workers=3):
        try:
            # Note(ethuleau): we need at least 2 worker to CN be able to
            #                 initialize IFMAP SSL session ??? I think CN
            #                 maintain TCP ssl session.
            #                 We should no set a pool of worker to the
            #                 GeventServer and let by default to have no
            #                 artificial limit
            #                 http://www.gevent.org/gevent.baseserver.html
            # self.run(host=host, port=port, server='gevent', spawn=workers,
            self.run(host=host, port=port, server='gevent',
                     server_side=True, certfile='/tmp/server.pem')
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

    def _object_set(self, obj_class, ifmap_id, obj_dict, existing_metas=None):
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
            # if obj does not have this meta_name (or)
            # or if the meta_name is different from what we have currently,
            # then update
            if (not existing_metas or
                    meta_name not in existing_metas or
                    ('' in existing_metas[meta_name] and
                     str(meta) != str(existing_metas[meta_name]['']))):
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
            if meta in existing_links and new_obj_dict.get(prop) is None:
                delete_links.append(meta)

        # remove refs that are no longer active
        refs = dict((obj_cls.ref_field_metas[rf],
                     obj_cls.ref_field_types[rf][0])
                    for rf in obj_cls.ref_fields)
        # refs = {'virtual-network-qos-forwarding-class': 'qos-forwarding-...',
        #         'virtual-network-network-ipam': 'network-ipam',
        #         'virtual-network-network-policy': 'network-policy',
        #         'virtual-network-route-table': 'route-table'}
        for meta, ref_res_type in refs.items():
            ref_obj_type = self._db_client_mgr.get_resource_class(
                ref_res_type).object_type
            for ref in new_obj_dict.get(ref_obj_type+'_refs', []):
                to_imid = get_ifmap_id_from_fq_name(ref_res_type, ref['to'])
                link_key = '%s %s' % (meta, to_imid)
                if link_key in existing_links:
                    delete_links.append(link_key)

        if delete_links:
            VncIfmapServer.delete(ifmap_id, delete_links)

        return self._object_set(obj_cls, ifmap_id, new_obj_dict, existing_links)

    def object_delete(self, obj_ids):
        VncIfmapServer.delete(obj_ids['imid'])
        return (True, '')
