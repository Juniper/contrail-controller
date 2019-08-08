from __future__ import print_function
from vnc_api.vnc_api import VncApi
import json

api = VncApi()
GSC = 'default-global-system-config'

def dump(res_type, fq_name):
    obj = api._object_read(res_type=res_type, fq_name=fq_name)
    dumpobj(obj)

def dumpobj(obj):
    print(json.dumps(api.obj_to_dict(obj), indent=4))

def dumplist(res_type, detail=False):
    refs = api._objects_list(res_type)
    if refs:
        refs = refs.get(res_type + 's')
    if detail:
        obj_list = []
        for ref in refs or []:
            obj = api._object_read(res_type, id=ref.get('uuid'))
            obj_list.append(api.obj_to_dict(obj))
        print(json.dumps({ 'objs': obj_list }, indent=4))
    else:
        print(json.dumps(refs, indent=4))

def dump_pr(name):
    dump('physical-router', [GSC, name])

def dump_pi(pr_name, pi_name):
    dump('physical-interface', [GSC, pr_name, pi_name])

def dump_li(pr_name, pi_name, unit=0):
    dump('logical-interface', [GSC, pr_name, pi_name, "%s.%d" % (pi_name, unit)])

def validate_json(filepath):
    with open(filepath, 'r') as f:
        data = json.load(f)
        print(json.dumps(data, indent=4))
