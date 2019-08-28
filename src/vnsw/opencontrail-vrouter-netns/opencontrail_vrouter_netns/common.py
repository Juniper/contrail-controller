from builtins import str
import uuid

def validate_uuid(val):
    try:
        if str(uuid.UUID(val)) == val:
            return val
    except (TypeError, ValueError, AttributeError):
        raise ValueError('Invalid UUID format')
