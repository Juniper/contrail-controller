from __future__ import unicode_literals
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Util to manage RBAC group and rules (add, delete etc)",
#
import argparse
import uuid as __uuid
import os
import re

from vnc_api.vnc_api import *
from vnc_api.gen.resource_xsd import *
from cfgm_common.exceptions import *

# match two rules (type RbacRuleType)
# r1 is operational, r2 is part of rbac group
# return (obj_type & Field match, rule is subset of existing rule, match index, merged rule
def match_rule(r1, r2):
    if r1.rule_object != r2.rule_object:
        return None
    if r1.rule_field != r2.rule_field:
        return None

    s1 = set(r.role_name+":"+r.role_crud for r in r1.rule_perms)
    s2 = set(r.role_name+":"+r.role_crud for r in r2.rule_perms)

    d1 = {r.role_name:set(list(r.role_crud)) for r in r1.rule_perms}
    d2 = {r.role_name:set(list(r.role_crud)) for r in r2.rule_perms}

    diffs = {}
    for role, cruds in list(d2.items()):
        diffs[role] = cruds - d1.get(role, set([]))
    diffs = {role:crud for role,crud in list(diffs.items()) if len(crud) != 0}

    merge = d2.copy()
    for role, cruds in list(d1.items()):
        merge[role] = cruds|d2.get(role, set([]))

    return [True, s1==s2, diffs, merge]
# end

# check if rule already exists in rule list and returns its index if it does
def find_rule(rge, rule):
    idx = 1
    for r in rge.rbac_rule:
        m = match_rule(rule, r)
        if m:
            m[0] = idx
            return m
        idx += 1
    return None
# end

def build_perms(rule, perm_dict):
    rule.rule_perms = []
    for role_name, role_crud in list(perm_dict.items()):
        rule.rule_perms.append(RbacPermType(role_name, "".join(role_crud)))
# end

# build rule object from string form
# "useragent-kv *:CRUD" (Allow all operation on /useragent-kv API)
def build_rule(rule_str):
    r = rule_str.split(" ", 1) if rule_str else []
    if len(r) < 2:
        return None

    # [0] is object.field, [1] is list of perms
    obj_field = r[0].split(".")
    perms = r[1].split(",")

    o = obj_field[0]
    f = obj_field[1] if len(obj_field) > 1 else ''
    o_f = "%s.%s" % (o,f) if f else o

    # perms eg ['foo:CRU', 'bar:CR']
    rule_perms = []
    for perm in perms:
        p = perm.strip().split(":")
        rule_perms.append(RbacPermType(role_name = p[0], role_crud = p[1]))

    # build rule
    rule = RbacRuleType(
              rule_object = o,
              rule_field = f,
              rule_perms = rule_perms)
    return rule
#end
