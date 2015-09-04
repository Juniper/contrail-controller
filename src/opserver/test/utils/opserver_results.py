#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
from opserver.introspect_util import *


def _OpResult_get_list_name(lst):
    sname = ""
    for sattr in lst.keys():
        if sattr[0] not in ['@']:
            sname = sattr
    return sname


def _OpResultFlatten(inp):
    #import pdb; pdb.set_trace()
    sname = ""
    if (inp['@type'] == 'struct'):
        sname = _OpResult_get_list_name(inp)
        if (sname == ""):
            return Exception('Struct Parse Error')
        ret = {}
        for k, v in inp[sname].items():
            ret[k] = _OpResultFlatten(v)
        return ret
    elif (inp['@type'] == 'list'):
        sname = _OpResult_get_list_name(inp['list'])
        ret = {}
        if (sname == ""):
            return ret
        items = inp['list'][sname]
        if not isinstance(items, list):
            items = [items]
        lst = []
        for elem in items:
            if not isinstance(elem, dict):
                lst.append(elem)
            else:
                lst_elem = {}
                for k, v in elem.items():
                    lst_elem[k] = _OpResultFlatten(v)
                lst.append(lst_elem)
        ret[sname] = lst
        return ret
    else:
        return inp['#text']


def _OpResultListParse(dct, match):
    ret = []
    sname = _OpResult_get_list_name(dct)
    if (sname == ""):
        return ret

    #import pdb; pdb.set_trace()
    if not isinstance(dct[sname], list):
        lst = [dct[sname]]
    else:
        lst = dct[sname]

    for elem in lst:
        if (match is None):
            isMatch = True
        else:
            isMatch = False

        if sname == 'element':
            if elem == match:
                isMatch = True
            if isMatch:
                ret.append(elem)
        else:
            dret = {}
            isMatcher = True
            for k, v in elem.items():
                if '#text' in v:
                    dret[k] = v["#text"]
                    if '@aggtype' in v:
                        if v['@aggtype'] == 'listkey':
                            if v['#text'] == match:
                                isMatch = True
                    if isinstance(match, list):
                        #import pdb; pdb.set_trace()
                        for matcher in match:
                            if not isinstance(matcher, tuple):
                                raise Exception('Incorrect matcher')
                            mk, mv = matcher
                            if (k == mk):
                                if (v['#text'] != mv):
                                    isMatcher = False
                else:
                    dret[k] = _OpResultFlatten(v)

            if isinstance(match, list):
                if isMatcher:
                    ret.append(dret)
            else:
                if isMatch:
                    ret.append(dret)
    return ret


def _OpResultGet(dct, p1, p2, match=None):
    ret = None
    try:
        res = dct.xpath(p1, p2)
        is_flat = False
        #import pdb; pdb.set_trace()
        if isinstance(res, list):
            if not isinstance(res[0], list):
                is_flat = True
            else:
                if not isinstance(res[0][0], dict):
                    is_flat = True
                else:
                    if "@type" not in res[0][0]:
                        is_flat = True
                    else:
                        if len(res) != 1:
                            raise Exception('Inconsistency')
                        res = res[0][0]

        if isinstance(res, dict):
            if "@type" in res:
                if res['@type'] in ["list"]:
                    ret = _OpResultListParse(res['list'], match)
                elif res['@type'] in ["struct"]:
                    sname = _OpResult_get_list_name(res)
                    ret = _OpResultFlatten(res)
                else:
                    if (match is not None):
                        raise Exception('Match is invalid for non-list')
                    ret = res['#text']
            else:
                is_flat = True
        else:
            is_flat = True

        if is_flat:
            ret = res
    except Exception as e:
        print e
    finally:
        return ret


class OpVNResult (Result):

    '''
        This class returns a VN UVE object
    '''

    def get_attr(self, tier, attr, match=None):
        #import pdb; pdb.set_trace ()
        if tier == "Config":
            typ = 'UveVirtualNetworkConfig'
        elif tier == "Agent":
            typ = 'UveVirtualNetworkAgent'
        else:
            raise Exception("Invalid Arguments - bad tier")

        return _OpResultGet(self, typ, attr, match)


class OpVMResult (Result):

    '''
        This class returns a VM UVE object
    '''

    def get_attr(self, tier, attr, match=None):
        #import pdb; pdb.set_trace ()
        if tier == "Config":
            typ = 'UveVirtualMachineConfig'
        elif tier == "Agent":
            typ = 'UveVirtualMachineAgent'
        else:
            raise Exception("Invalid Arguments - bad tier")

        return _OpResultGet(self, typ, attr, match)


class OpCollectorResult (Result):

    '''
        This class returns a CollectorInfo object
    '''

    def get_attr(self, tier, attr, match=None):
        #import pdb; pdb.set_trace ()
        if tier == "Analytics":
            typ = 'CollectorState'
        else:
            raise Exception("Invalid Arguments - bad tier")

        return _OpResultGet(self, typ, attr, match)
