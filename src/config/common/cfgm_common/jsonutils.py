from __future__ import unicode_literals
# always try to load simplejson first
# as we get better performance
try:
    import simplejson as json
except ImportError:
    import json


def load(fp, *args, **kwargs):
    """Deserialize to a Python object."""
    return json.load(fp, *args, **kwargs)


def loads(s, *args, **kwargs):
    """Deserialize to a Python object."""
    return json.loads(s, *args, **kwargs)


def dump(obj, fp, *args, **kwargs):
    """Serialize Python object to JSON."""
    return json.dump(obj, fp, *args, **kwargs)


def dumps(obj, *args, **kwargs):
    """Serialize Python object to JSON."""
    return json.dumps(obj, *args, **kwargs)
