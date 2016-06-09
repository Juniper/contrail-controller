# always try to load ujson first as we get better performance
try:
    import ujson as third_party_json
except ImportError:
    import json as third_party_json
import json as builtin_json


def load(fp, **kwargs):
    try:
        return third_party_json.load(fp, *args, **kwargs)
    except ValueError:
        return builtin_json.load(fp, *args, **kwargs)


def loads(s, *args, **kwargs):
    try:
        return third_party_json.loads(s, *args, **kwargs)
    except ValueError:
        return builtin_json.loads(s, *args, **kwargs)


def dump(obj, fp, *args, **kwargs):
    try:
        return third_party_json.dump(obj, fp, *args, **kwargs)
    except (ValueError, OverflowError):
        return builtin_json.dump(obj, fp, *args, **kwargs)


def dumps(obj, *args, **kwargs):
    try:
        return third_party_json.dumps(obj, *args, **kwargs)
    except (ValueError, OverflowError):
        return builtin_json.dumps(obj, *args, **kwargs)
