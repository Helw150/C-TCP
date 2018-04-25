# project2
Task 1 is tagged in the Task 1 Release and held in the task-1 branch.
Task 2 is held in the master branch and tagged as the Task 2 release.
## How to Build

cd ~/github/project2/rdt2.0/src

mininet@mininet-vm:~/github/project2/rdt2.0/src$ make


## How to test Task1 ?


cd ~/github/project2/

dd if=/dev/urandom of=small_file.bin  bs=1048576 count=1

### run mininet
sudo mn --link tc,bw=10,delay=10ms,loss=2

mininet> xterm h1 h2

### Two terminal will popup for h1 and h2; Run sender and receiver on these two terminal

### terminal node h1:
./rdt2.0/obj/rdt_receiver 60001 FILE_RCVD

### terminal node h2:
./rdt2.0/obj/rdt_sender 10.0.0.1 60001 small_file.bin


### verify the two files

cksum FILE_RCVD small_file.bin


## How to test Task 2 ?
### Repeat the step from task1
### Go to rdt2.0/src directory and build

### Go to project2 directory

project2$ sudo ./run.sh


### RUN.SH has been amended according to the instructions to produce cwnd.pdf in the output directory
### Check that directory to evaluate after running run.sh


