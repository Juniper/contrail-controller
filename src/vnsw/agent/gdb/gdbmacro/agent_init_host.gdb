source /home/kvpraveen/gdbmacro/agent.gdb
source /home/kvpraveen/gdbmacro/vrf.gdb

python
import sys

sys.path.insert(0, '/home/kvpraveen/gdbmacro/agent_printer')
from printers import register_agent_printers
register_agent_printers(None)
end
