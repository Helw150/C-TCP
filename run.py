#Nabil Rahiman
#NYU, Abudhabi
#email:nr83@nyu.edu
#18/Feb/2018

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.link import TCLink
from mininet.log import setLogLevel
import filecmp
import sys

class SingleSwitchTopo(Topo):
    "Single switch connected to n hosts."
    def build(self, n=2):
        switch = self.addSwitch('s1')
        # Python's range(N) generates 0..N-1
        for h in range(n):
            # Each host gets 50%/n of system CPU
            host = self.addHost( 'h%s' % (h + 1))
            # 10 Mbps, 5ms delay, 2% loss, 1000 packet queue
            self.addLink( host, switch, bw=10, delay='5ms', loss=2, max_queue_size=1000 )

"Create and test a simple network"
topo = SingleSwitchTopo()

# Mininet provides performance limiting and isolation features, through the
# CPULimitedHost and TCLink classes.
net = Mininet(topo, link=TCLink)

net.start()
print "Dumping host connections"
dumpNodeConnections(net.hosts)
print "Testing network connectivity"
h1 = net.get('h1')
h2 = net.get('h2')
#CLI(net)
h2.sendCmd('/home/rdt2.0/obj/rdt_receiver 4040 /home/FILE_RCVD') 
h1.cmd('/home/rdt2.0/obj/rdt_sender 10.0.0.2 4040 /home/small_file.bin')
if(filecmp.cmp('/home/small_file.bin', '/home/FILE_RCVD')):
    print "Task 1 Passed"
else:
    print "Task 1 Failed"
