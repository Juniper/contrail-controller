def _obj_serializer_all(obj):
    if hasattr(obj, 'serialize_to_json'):
        return obj.serialize_to_json()
    else:
        return dict((k, v) for k, v in obj.__dict__.iteritems())
# end _obj_serializer_all
