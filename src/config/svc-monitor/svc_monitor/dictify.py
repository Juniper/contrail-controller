import types

def dictify(v):
    if (isinstance(v, types.InstanceType) or 'class' in str(type(v))) and  not 'mock' in str(type(v)):
        return { k:dictify(_v) for k, _v in v.__dict__.iteritems() }
    elif isinstance(v, dict):
        return { k:dictify(_v) for k, _v in v.iteritems() }
    elif isinstance(v, set):
        try:
            return { dictify(i) for i in v }
        except TypeError as e:
            raise
    elif isinstance(v, list):
        return [ dictify(i) for i in v ]
    else:
        return v
