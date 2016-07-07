import sys
import re

_illegal_unichrs = [(0x00, 0x08), (0x0B, 0x0C), (0x0E, 0x1F),
                        (0x7F, 0x84), (0x86, 0x9F),
                        (0xFDD0, 0xFDDF), (0xFFFE, 0xFFFF)]
if sys.maxunicode >= 0x10000:  # not narrow build
        _illegal_unichrs.extend([(0x1FFFE, 0x1FFFF), (0x2FFFE, 0x2FFFF),
                                 (0x3FFFE, 0x3FFFF), (0x4FFFE, 0x4FFFF),
                                 (0x5FFFE, 0x5FFFF), (0x6FFFE, 0x6FFFF),
                                 (0x7FFFE, 0x7FFFF), (0x8FFFE, 0x8FFFF),
                                 (0x9FFFE, 0x9FFFF), (0xAFFFE, 0xAFFFF),
                                 (0xBFFFE, 0xBFFFF), (0xCFFFE, 0xCFFFF),
                                 (0xDFFFE, 0xDFFFF), (0xEFFFE, 0xEFFFF),
                                 (0xFFFFE, 0xFFFFF), (0x10FFFE, 0x10FFFF)])

_illegal_ranges = ["%s-%s" % (unichr(low), unichr(high))
                   for (low, high) in _illegal_unichrs]
illegal_xml_chars_RE = re.compile(u'[%s]' % u''.join(_illegal_ranges))

INVALID_NAME_CHARS = set('<>&":')

def validate_mandatory_fields(obj_uuid, obj_cols):
    for fname in ['fq_name', 'type', 'prop:id_perms']:
        fval = obj_cols.get(fname)
        if not fval:
            raise Exception("Error, no %s. Row: %s Columns: %s" %(fname, obj_uuid, str(obj_cols)))

for row, cols in OBJ_UUID_TABLE.get_range():
    try:
        validate_mandatory_fields(row, cols)
    except Exception as e:
        print str(e)
        continue

    fq_name = cols['fq_name']
    if illegal_xml_chars_RE.search(fq_name[-1]):
        print "Error, illegal xml char in name %s" %(fq_name[-1])
        continue

        if obj_type[:].replace('-','_') == 'route_target':
            invalid_chars = INVALID_NAME_CHARS - set(':')
        else:
            invalid_chars = INVALID_NAME_CHARS

        if any((c in invalid_chars) for c in fq_name[-1]):
            print "Error, restricted xml characters %s in name %s" %(str(invalid_chars), fq_name[-1])
            continue

exit()
