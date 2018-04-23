/*
 * Nabil Rahiman
 * NYU Abudhabi
 * email: nr83@nyu.edu
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120 //milli secon
#define TCP_MAX_PACKETS 1073741824/MSS_SIZE // TCP max num packets

int next_seqno=0;
int send_base=0;
int WINDOW_SIZE = 1;
float congestion_control = 0.0;
int ssthresh = 64;

int sockfd, serverlen;
int num_packets_sent = 0;
int last_ackno = 0;
int rounds_since_ack = 0;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt[TCP_MAX_PACKETS];
tcp_packet *cache[TCP_MAX_PACKETS];
tcp_packet *recvpkt;
sigset_t sigmask;       


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

void expandWindow(int newWindow)
{
    VLOG(DEBUG, "Expanding window to %d\n", newWindow);
    WINDOW_SIZE = newWindow;
    return;
}

void sortCache(){
    int lastNonNullIndex = 0;

    for (int i = 0; i < TCP_MAX_PACKETS; i++){
	if(cache[i] != NULL){
	    cache[lastNonNullIndex++] = cache[i];
	}
    }

    int count = lastNonNullIndex;

    while(count < TCP_MAX_PACKETS){
	cache[count++] = NULL;
    }

    for (int i = 0; i < TCP_MAX_PACKETS; i++){
	if(cache[i] != NULL){
	    for (int j = 0; j < TCP_MAX_PACKETS; j++){
		assert(cache[i] != NULL);
		if(cache[j] == NULL){
		    break;
		} else if (cache[j]->hdr.seqno > cache[i]->hdr.seqno){
		    tcp_packet *tmp = cache[i];
		    cache[i] = cache[j];   
		    cache[j] = tmp;
		} else if (i != j && cache[j]->hdr.seqno == cache[i]->hdr.seqno) {
		    cache[j] = NULL;
		}
	    }
	}
    }
    assert(cache[lastNonNullIndex+1] == NULL);
}


void shrinkWindow(int newWindow)
{
    // Cache any packets that were in the window before shrinkage
    for(int i = newWindow; i < WINDOW_SIZE; i++){
	for(int j = 0; j < TCP_MAX_PACKETS; j++){
	    if(cache[j] == NULL){
		cache[j] = sndpkt[i];
		sndpkt[i] = NULL;
		num_packets_sent--;
		break;
	    }
	}
    }
    sortCache();
    WINDOW_SIZE = newWindow;
    return;
}


int remove_stale_packets(int seqno)
{
    int pktindex = -1;
    for(int i = 0; i < WINDOW_SIZE; i++){
        if (sndpkt[i] != NULL && sndpkt[i]->hdr.seqno <= seqno){
            sndpkt[i] = NULL;
            num_packets_sent--;
	    if(WINDOW_SIZE < ssthresh){
		expandWindow(WINDOW_SIZE+1);
	    } else {
		congestion_control += 1/(WINDOW_SIZE+congestion_control);
		if(congestion_control > 1){
		    congestion_control -= 1;
		    expandWindow(WINDOW_SIZE+1);
		}
	    }
        }
	if (sndpkt[i] != NULL && seqno == sndpkt[i]->hdr.seqno){
	    pktindex = i;
	}
    }
    return pktindex;
}

int find_empty_index()
{
    for(int i = 0; i < WINDOW_SIZE; i++){
	if (sndpkt[i] == NULL){
	    return i;
	}
    }
    return -1;
}


void resend_packets(int sig)
{
    rounds_since_ack++;
    if(WINDOW_SIZE >= 4){
	ssthresh = WINDOW_SIZE/2;
    } else {
	ssthresh = 2;
    }
    shrinkWindow(1);
    if (sig == SIGALRM)
	{
            VLOG(DEBUG, "Last Acknowledgement %d \n", last_ackno);
	    //Resend all packets in our currently sent packets
	    // array. Since already acknowledged packets are
	    // removed from the array.
	    VLOG(INFO, "Timeout happend");
	    for(int i = 0; i < WINDOW_SIZE; i++){
		if(sndpkt[i] != NULL && sndpkt[i]->hdr.seqno >= last_ackno){
                    VLOG(DEBUG, "Resending %d \n", sndpkt[i]->hdr.seqno);
		    if(sendto(sockfd, sndpkt[i], TCP_HDR_SIZE + get_data_size(sndpkt[i]), 0, 
			      ( const struct sockaddr *)&serveraddr, serverlen) < 0)
			{
			    error("sendto");
			}
		}
	    }
	    start_timer();
	}
}


/*
 * init_timer: Initialize timeer
 * delay: delay in milli seconds
 * sig_handler: signal handler function for resending unacknoledge packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int main (int argc, char **argv)
{
    int portno, len=1, pkt_index;
    int next_seqno;
    int dup_ack = 0;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;
    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol
    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    while(1){
	while (num_packets_sent < WINDOW_SIZE)
	    {
                VLOG(INFO, "Sending new packet");
                num_packets_sent++;
		pkt_index = find_empty_index();
		len = fread(buffer, 1, DATA_SIZE, fp);
		if (len <= 0)
		    {
			VLOG(INFO, "End Of File has been reached");
			sndpkt[pkt_index] = make_packet(0);
			sendto(sockfd, sndpkt[pkt_index], TCP_HDR_SIZE,  0,
			       (const struct sockaddr *)&serveraddr, serverlen);
			break;
		    }
		send_base = next_seqno;
		next_seqno = send_base + len;
		sndpkt[pkt_index] = make_packet(len);
		memcpy(sndpkt[pkt_index]->data, buffer, len);
		sndpkt[pkt_index]->hdr.seqno = send_base;
		//Wait for ACK
		VLOG(DEBUG, "Sending packet %d to %s", 
		     send_base, inet_ntoa(serveraddr.sin_addr));
		/*
		 * If the sendto is called for the first time, the system will
		 * will assign a random port number so that server can send its
		 * response to the src port.
		 */
		if(sendto(sockfd, sndpkt[pkt_index], TCP_HDR_SIZE + get_data_size(sndpkt[pkt_index]), 0, 
			  ( const struct sockaddr *)&serveraddr, serverlen) < 0)
		    {
			error("sendto");
		    }
		if(num_packets_sent == 1){
		    start_timer();
		}
		//ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
		//struct sockaddr *src_addr, socklen_t *addrlen);
		printf("%d \n", num_packets_sent);
	    }
	if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
		    (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
	    {
		error("recvfrom");
	    }
	recvpkt = (tcp_packet *)buffer;
        VLOG(DEBUG, "%d \n", recvpkt->hdr.ackno);
	if(recvpkt->hdr.ackno > last_ackno){
            rounds_since_ack = 0;
            VLOG(DEBUG, "New Acknowledgement");
	    assert(get_data_size(recvpkt) <= DATA_SIZE);
	    stop_timer();
	    printf("%d \n", num_packets_sent);
	    if(num_packets_sent > 0){
		start_timer();
	    }
	    remove_stale_packets(last_ackno);
  	    last_ackno = recvpkt->hdr.ackno;
            dup_ack = 0;
	} else {
            dup_ack++;
            if(dup_ack >= 3){
                resend_packets(SIGALRM);
                dup_ack = 0;
            }
        }
        VLOG(DEBUG, "%d \n", num_packets_sent);
        if(recvpkt->hdr.ackno == -1 || rounds_since_ack==500){
	    VLOG(DEBUG, "Final Window Size: %d \n", WINDOW_SIZE);
            return 0;
        }
    }
    return 0;
}



