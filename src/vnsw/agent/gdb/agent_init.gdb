source /root/gdbmacro/agent.gdb
source /root/gdbmacro/vrf.gdb

python
import sys

sys.path.insert(0, '/root/gdbmacro/agent_printer')
from printers import register_agent_printers
register_agent_printers(None)
end

