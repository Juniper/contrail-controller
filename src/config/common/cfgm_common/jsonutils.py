# always try to load simplejson first 
# as we get better performance
try:
    import simplejson as json
except ImportError:
    import json


def load(fp, **kwargs):
    return json.load(fp, *args, **kwargs)


def loads(s, *args, **kwargs):
    return json.loads(s, *args, **kwargs)


def dump(obj, fp, *args, **kwargs):
    return json.dump(obj, fp, *args, **kwargs)


def dumps(obj, *args, **kwargs):
    return json.dumps(obj, *args, **kwargs)
