RDT2.0 Implementation

My RDT implementation has 3 phases which roughly model the Psuedo-Code RDT implementation on page 269 of Computer Networking: A Top Down Approach. The first while loop sends packets until the Window Size is reached by counting how many packets are currently sent on the network. 

Then, a listener waits until an ACK is recieved. The receiver is set to ACK the highest consecutive segment, which means that the listener can assume that all sequence numbers lower than the highest received ACK have been received by the sender. Therefore, it deletes all ACK's it knows to be received, decrements the counter of how many packets are currently in the network and then continues to the loop that allows the sender to fill the Window size again.

There is constantly a timer running from either one of two options: The last successful ACK or the last re-transmission. If this timer reaches 120 milliseconds, the sender will retransmit all data with higher sequence number than the last successfully received ACK.

To end transmission, the sender waits for either an EOF acknowledgement (Sequence Number -1), for it's number of packets in the network to reach 0, or for a 500 retransmissions with no response. This last step is neccesary as the receiver only sends one ACK of the EOF and then exits, which could cause the sender to retransmit forever if several final ACK's were lost and it did not have an ultimate cap. With 1000 retransmissions, the sender is waiting a full minute, a reasonable time to assume that the receiver has either received the end of file, has disconnected from the data stream intentinally, or is undergoing a loss of service significant enough to merit a full retransmission.


Instructions:
To build - run Make in the rdt2.0/src directory.
To run the sender - run /rdt2.0/obj/rdt_sender receiver_ip receiver_port infile.
To run the receiver - run /rdt2.0/obj/rdt_receiver port outfile.
To test:

cd project2-will
dd if=/dev/urandom of=small_file.bin bs=1048576 count=1

run mininet
sudo mn --link tc,bw=10,delay=10ms,loss=2

mininet> xterm h1 h2

Two terminal will popup for h1 and h2; Run sender and receiver on these two terminal
terminal node h1:
./rdt2.0/obj/rdt_receiver 60001 FILE_RCVD

terminal node h2:
./rdt2.0/obj/rdt_sender 10.0.0.1 60001 small_file.bin

verify the two files
cksum FILE_RCVD small_file.bin
