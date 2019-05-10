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
r
fin
p blist
c
fin
p blist
c
fin
p mlist
c
fin
p mlist
c
fin
p blist
c
fin
p blist
c
fin
p mlist
c
fin
p mlist
c
fin
p bset
c
fin
p bset
c
fin
p mset
c
fin
p mset
c
fin
p bset
c
fin
p bset
c
fin
p mset
c
fin
p mset
c
q
