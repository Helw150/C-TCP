import numpy as np
import matplotlib.pyplot as plt
import time
import datetime
import sys
from argparse import ArgumentParser

def scale(a):
    return a/1000000.0

def parse_throughput(filename):
    times = []
    pktsize = []

    throughput_file = open(filename,"r")
    for line in throughput_file:
        tokens = line.split(",")
        #pktsize.append((float(tokens[1])))
        pktsize.append(1450)
        times.append((float(tokens[0])))
        # if float(tokens[0]) > 1488343998.112447+30.0:
        #     break

    throughput_file.close()
    return times, pktsize

def parse_delay(filename):
    delays = []
    times1 = []
    cnt = 0

    delay_file = open(filename,"r")
    first_delay = float(delay_file.readline().split(",")[0])
    delay_file.seek(0)
    for line in delay_file:
        tokens = line.split(",")
        if len(tokens) < 3 :
            break
        if float(tokens[1]) > 0.0 :
            delays.append((float(tokens[2])))
            times1.append((float(tokens[0]) - first_delay))
        if cnt == 2000:
            break
        cnt+=1
    delay_file.close()

    return  delays, times1

def calc_throughput(data, pktsize):
    values = []
    last_index = 0
    ctr = 0
    w = float(data[0] +float(1))
    for i, v in enumerate(data):
        if w < v:
            values.append(ctr*8.0/1000000.0)
            #w = float(data[i] +float(1))
            w = float(w +float(1))
            ctr=0

        ctr = ctr + pktsize[i]
    return values

parser = ArgumentParser(description="plot")
parser.add_argument('--dir', '-d',
                    help="Directory to store outputs",
                    )

parser.add_argument('--name', '-n',
                    help="name of the experiment",
                    )

parser.add_argument('--trace', '-tr',
                    help="name of the trace",
                    )



args = parser.parse_args()

fig = plt.figure(figsize=(21,3), facecolor='w')
ax = plt.gca()

# plotting cwnd
cwndDL = []
timeDL = []

traceDL = open ('CWND.csv', 'r')
traceDL.readline()

for line in traceDL:
    try:
        timeArray = line.strip().split(",")[0].split(".")
        timeVal = time.mktime(time.strptime(timeArray[0], "%H:%M:%S"))+float('0.'+timeArray[1])
        if timeVal not in timeDL:
            cwndDL.append(float(line.strip().split(",")[1]))
            timeDL.append(float(timeVal))
    except:
        print("CSV writing was killed mid process")
plt.plot(sorted(timeDL), cwndDL, lw=2, color='r')

plt.ylabel("CWND (packets)")
plt.xlabel("Time (s)")
plt.xlim([timeDL[0],timeDL[-1]])
plt.grid(True, which="both")
plt.savefig(args.dir + '/cwnd.pdf',dpi=1000,bbox_inches='tight')
print("Graph Written to " +args.dir +"/cwnd.pdf")
