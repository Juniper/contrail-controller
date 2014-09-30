#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# This file deals with the ifmap id handling for both vnc-user-visible entities
# and bgp-visible entities

import uuid
import re
import StringIO
from lxml import etree
from cfgm_common import exceptions
from cfgm_common.ifmap.client import client
from ifmap.request import NewSessionRequest, RenewSessionRequest, \
    EndSessionRequest, PublishRequest, SearchRequest, \
    SubscribeRequest, PurgeRequest, PollRequest
from ifmap.id import IPAddress, MACAddress, Device, AccessRequest, Identity, \
    CustomIdentity
from ifmap.operations import PublishUpdateOperation, PublishNotifyOperation, \
    PublishDeleteOperation, SubscribeUpdateOperation,\
    SubscribeDeleteOperation
from ifmap.util import attr, link_ids
from ifmap.response import Response, newSessionResult
from ifmap.metadata import Metadata


_TENANT_GRP = "(?P<tenant_uuid>.*)"
_VPC_GRP = "(?P<vpc_name>.*)"
_VN_GRP = "(?P<vn_name>.*)"
_SG_GRP = "(?P<sg_name>.*)"
_POL_GRP = "(?P<pol_name>.*)"
_INST_GRP = "(?P<instance_uuid>.*)"
_PORT_GRP = "(?P<port_id>.*)"

_TENANT_ID_RE = "contrail:tenant:%s" % (_TENANT_GRP)
_VPC_NAME_RE = "contrail:network-group:%s:%s" % (_TENANT_GRP, _VPC_GRP)
_VN_NAME_RE = "contrail:virtual-network:%s:%s:%s" % (
    _TENANT_GRP, _VPC_GRP, _VN_GRP)
_SG_NAME_RE = "contrail:security-group:%s:%s:%s" % (
    _TENANT_GRP, _VPC_GRP, _SG_GRP)
_POL_NAME_RE = "contrail:policy:%s:%s:%s" % (_TENANT_GRP, _VPC_GRP, _POL_GRP)
_INST_ID_RE = "contrail:instance:%s:%s:%s:%s" \
    % (_TENANT_GRP, _VPC_GRP, _VN_GRP, _INST_GRP)
_PORT_ID_RE = "contrail:port:%s:%s:%s:%s:%s" \
    % (_TENANT_GRP, _VPC_GRP, _VN_GRP, _INST_GRP, _PORT_GRP)

_CT_NS = "contrail"
_ROOT_IMID = _CT_NS + ":config-root:root"

_SOAP_XSD = "http://www.w3.org/2003/05/soap-envelope"
_IFMAP_XSD = "http://www.trustedcomputinggroup.org/2010/IFMAP/2"
_IFMAP_META_XSD = "http://www.trustedcomputinggroup.org/2010/IFMAP-METADATA/2"
_CONTRAIL_XSD = "http://www.contrailsystems.com/vnc_cfg.xsd"

# Parse ifmap-server returned search results and create list of tuples
# of (ident-1, ident-2, link-attribs)


def parse_result_items(result_items, my_imid=None):
    all_result_list = []
    for r_item in result_items:
        children = r_item.getchildren()
        num_children = len(children)
        if num_children == 1:  # ignore ident-only result-items
            continue
        elif num_children == 2:
            result_info = [children[0], None, children[1]]
        elif num_children == 3:
            result_info = [children[0], children[1], children[2]]
        else:
            raise Exception('Result item of length %s not handled!'
                            % (num_children))
        all_result_list.append(result_info)

    if not my_imid:
        return all_result_list

    # strip ones that don't originate from or to my_imid
    filtered_result_list = []
    for (ident_1, ident_2, meta) in all_result_list:
        if (((ident_2 is not None) and (ident_2.attrib['name'] == my_imid)) or
                (ident_1.attrib['name'] == my_imid)):
            if meta is None:
                filtered_result_list.append((ident_1, ident_2, None))
            else:
                # search gives all props under one metadata. expand it.
                for m_elem in meta:
                    filtered_result_list.append((ident_1, ident_2, m_elem))

    return filtered_result_list
# end parse_result_items


def get_ifmap_id_from_fq_name(type, fq_name):
    my_fqn = ':' + ':'.join(fq_name)
    my_imid = 'contrail:' + type + my_fqn

    return my_imid
# end get_ifmap_id_from_fq_name


def get_type_from_ifmap_id(ifmap_id):
    type = ifmap_id.split(':')[1]
    return type
# end get_type_from_ifmap_id


def get_fq_name_str_from_ifmap_id(ifmap_id):
    return re.sub(r'contrail:.*?:', '', ifmap_id)
# end get_fq_name_str_from_ifmap_id


def get_fq_name_from_ifmap_id(ifmap_id):
    type = get_type_from_ifmap_id(ifmap_id)
    # route-target has ':' in the name, so handle it as a special case
    if type=='route-target':
        return [':'.join(ifmap_id.split(':')[2:])]
    return ifmap_id.split(':')[2:]
# end get_fq_name_from_ifmap_id

def get_vm_id_from_interface(vmi_obj):
    if vmi_obj.parent_type=='virtual-machine':
        return vmi_obj.parent_uuid
    else:
        vm_refs = vmi_obj.get_virtual_machine_refs()
        return vm_refs[0]['uuid'] if vm_refs else None
# end get_vmi_id_from_interface

def subscribe_root(ssrc_mapc):
    #self._ident_type_subscribe(_CLOUD_IMID, "ct:member-of")
    ident = str(Identity(name=_ROOT_IMID, type="other",
                         other_type="extended"))
    subreq = SubscribeRequest(
        ssrc_mapc.get_session_id(),
        operations=str(SubscribeUpdateOperation("root", ident,
                                                {"max-depth": "255", })))

    result = ssrc_mapc.call('subscribe', subreq)
# end _subscribe_root


def ssrc_initialize(args):
    ssrc_mapc = ifmap_server_connect(args)
    result = ssrc_mapc.call('newSession', NewSessionRequest())
    ssrc_mapc.set_session_id(newSessionResult(result).get_session_id())
    ssrc_mapc.set_publisher_id(newSessionResult(result).get_publisher_id())
    subscribe_root(ssrc_mapc)
    return ssrc_mapc
# end ssrc_initialize


def arc_initialize(args, ssrc_mapc):
    #
    # Poll requests go on ARC channel which don't do newSession but
    # share session-id with ssrc channel. so 2 connections to server but 1
    # session/session-id in ifmap-server (mamma mia!)
    #
    arc_mapc = ifmap_server_connect(args)
    arc_mapc.set_session_id(ssrc_mapc.get_session_id())
    arc_mapc.set_publisher_id(ssrc_mapc.get_publisher_id())

    return arc_mapc
# end arc_initialize


def ifmap_server_connect(args):
    _CLIENT_NAMESPACES = {
        'env':  _SOAP_XSD,
        'ifmap':  _IFMAP_XSD,
        'meta':  _IFMAP_META_XSD,
        _CT_NS:  _CONTRAIL_XSD
    }

    ssl_options = None
    if args.use_certs:
        ssl_options = {
            'keyfile': args.keyfile,
            'certfile': args.certfile,
            'ca_certs': args.ca_certs,
            'cert_reqs': ssl.CERT_REQUIRED,
            'ciphers': 'ALL'
        }
    return client(("%s" % (args.ifmap_server_ip),
                   "%s" % (args.ifmap_server_port)),
                  args.ifmap_username, args.ifmap_password,
                  _CLIENT_NAMESPACES, ssl_options)
# end ifmap_server_connect


def parse_poll_result(poll_result_str):
    _XPATH_NAMESPACES = {
        'a': _SOAP_XSD,
        'b': _IFMAP_XSD,
        'c': _CONTRAIL_XSD
    }

    soap_doc = etree.parse(StringIO.StringIO(poll_result_str))
    #soap_doc.write(sys.stdout, pretty_print=True)

    xpath_error = '/a:Envelope/a:Body/b:response/errorResult'
    error_results = soap_doc.xpath(xpath_error,
                                   namespaces=_XPATH_NAMESPACES)

    if error_results:
        if error_results[0].get('errorCode') == 'InvalidSessionID':
            raise exceptions.InvalidSessionID(etree.tostring(error_results[0]))
        raise Exception(etree.tostring(error_results[0]))

    xpath_expr = '/a:Envelope/a:Body/b:response/pollResult'
    poll_results = soap_doc.xpath(xpath_expr,
                                  namespaces=_XPATH_NAMESPACES)

    result_list = []
    for result in poll_results:
        children = result.getchildren()
        for child in children:
            result_type = child.tag
            if result_type == 'errorResult':
                raise Exception(etree.tostring(child))

            result_items = child.getchildren()
            item_list = parse_result_items(result_items)
            for item in item_list:
                ident1 = item[0]
                ident2 = item[1]
                meta = item[2]
                idents = {}
                ident1_imid = ident1.attrib['name']
                ident1_type = get_type_from_ifmap_id(ident1_imid)
                idents[ident1_type] = get_fq_name_str_from_ifmap_id(
                    ident1_imid)
                if ident2 is not None:
                    ident2_imid = ident2.attrib['name']
                    ident2_type = get_type_from_ifmap_id(ident2_imid)
                    if ident1_type == ident2_type:
                        idents[ident1_type] = [
                            idents[ident1_type],
                            get_fq_name_str_from_ifmap_id(ident2_imid)]
                    else:
                        idents[ident2_type] = get_fq_name_str_from_ifmap_id(
                            ident2_imid)
                result_list.append((result_type, idents, meta))
    return result_list
# end parse_poll_result
