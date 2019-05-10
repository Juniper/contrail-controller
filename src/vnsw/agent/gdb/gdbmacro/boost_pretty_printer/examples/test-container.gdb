set pagination off

python
import sys
sys.path.insert(0, "..")
try:
    reload (sys.modules["boost.printers"])
except:
    pass
from boost.printers import register_printer_gen
register_printer_gen(None)
end

b break_here
#flat_set
r
fin
p fset
c
fin
p fset
p itr
p empty_itr
#flat_map
c
fin
p fmap
c
fin
p fmap
p itr
p empty_itr
c
q
