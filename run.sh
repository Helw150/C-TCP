mn --link tc,bw=10,delay=10ms,loss=2
h1 ./rdt2.0/obj/rdt_receiver 60001 FILE_RCVD &
h2 ./rdt2.0/obj/rdt_sender 10.0.0.1 60001 small_file.bin &
exit
cksum FILE_RCVD small_file.bin
