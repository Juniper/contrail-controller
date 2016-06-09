# always try to load ujson first as we get better performance

import ujson
import simplejson


# TODO: when ujson will support to specify decode/encode hook, update that code
#       to use it. Under review https://github.com/esnme/ultrajson/pull/209


def load(fp, **kwargs):
    try:
        return ujson.load(fp, *args, **kwargs)
    except (ValueError, TypeError):
        # As ujson does not support all the json api catching TypeError
        # permits to fallback to simplejson
        return simplejson.load(fp, *args, **kwargs)


def loads(s, *args, **kwargs):
    try:
        return ujson.loads(s, *args, **kwargs)
    except (ValueError, TypeError):
        # As ujson does not support all the json api catching TypeError
        # permits to fallback to simplejson
        return simplejson.loads(s, *args, **kwargs)


def dump(obj, fp, *args, **kwargs):
    try:
        return ujson.dump(obj, fp, *args, **kwargs)
    except (ValueError, OverflowError, TypeError):
        # As ujson does not support all the json api catching TypeError
        # permits to fallback to simplejson
        return simplejson.dump(obj, fp, *args, **kwargs)


def dumps(obj, *args, **kwargs):
    try:
        return ujson.dumps(obj, *args, **kwargs)
    except (ValueError, OverflowError, TypeError):
        # As ujson does not support all the json api catching TypeError
        # permits to fallback to simplejson
        return simplejson.dumps(obj, *args, **kwargs)
