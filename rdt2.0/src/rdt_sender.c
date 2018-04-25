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
float ssthresh = 64.0;

int sockfd, serverlen;
int num_packets_sent = 0;
int last_ackno = 0;
int rounds_since_ack = 0;
double estimated_rtt=120, dev_rtt=0;
double alpha=0.125, beta=0.25;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt[TCP_MAX_PACKETS];
tcp_packet *cache[TCP_MAX_PACKETS];
tcp_packet *recvpkt;
FILE *csv;
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

void writeToCSV(double window_size){
    char buff[20];
    time_t now = time(0);
    struct tm *sTm = gmtime (&now);
    int millisec;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    millisec = (int)tv.tv_usec;
    
    strftime (buff, sizeof(buff), "%H:%M:%S:", sTm);
    fprintf(csv, "%s.%03d,%f,%f\n", buff, millisec, window_size, ssthresh);
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

    assert(cache[lastNonNullIndex] == NULL);
    
    for (int i = 0; i < TCP_MAX_PACKETS; i++){
	if(cache[i] != NULL){
	    for (int j = i+1; j < TCP_MAX_PACKETS; j++){
		assert(cache[i] != NULL);
		if(cache[j] == NULL){
		    break;
		} else if (cache[j]->hdr.seqno < cache[i]->hdr.seqno){
		    tcp_packet *tmp = cache[i];
		    cache[i] = cache[j];   
		    cache[j] = tmp;
		} else if (i != j && cache[j]->hdr.seqno == cache[i]->hdr.seqno) {
		    cache[j] = NULL;
		}
	    }
	}
    }
    for(int i = 0; i < lastNonNullIndex; i++){
	VLOG(DEBUG, "Cached Packet on Shrink: %d", cache[i]->hdr.seqno);
    }
}

void sortSndPkt(){
    int lastNonNullIndex = 0;

    for (int i = 0; i < TCP_MAX_PACKETS; i++){
	if(sndpkt[i] != NULL){
	    sndpkt[lastNonNullIndex++] = sndpkt[i];
	}
    }

    int count = lastNonNullIndex;
    
    while(count < TCP_MAX_PACKETS){
	sndpkt[count++] = NULL;
    }

    assert(sndpkt[lastNonNullIndex] == NULL);
    
    for (int i = 0; i < TCP_MAX_PACKETS; i++){
	if(sndpkt[i] != NULL){
	    for (int j = i+1; j < TCP_MAX_PACKETS; j++){
		assert(sndpkt[i] != NULL);
		if(sndpkt[j] == NULL){
		    break;
		} else if (sndpkt[j]->hdr.seqno < sndpkt[i]->hdr.seqno){
		    tcp_packet *tmp = sndpkt[i];
		    sndpkt[i] = sndpkt[j];   
		    sndpkt[j] = tmp;
		} else if (i != j && sndpkt[j]->hdr.seqno == sndpkt[i]->hdr.seqno) {
		    sndpkt[j] = NULL;
		}
	    }
	}
    }
}


void shrinkWindow(int newWindow)
{
    sortSndPkt();
    // Cache any packets that were in the window before shrinkage
    for(int i = newWindow; i < WINDOW_SIZE; i++){
        if(sndpkt[i] != NULL){
            for(int j = 0; j < TCP_MAX_PACKETS; j++){
                if(cache[j] == NULL){
                    cache[j] = sndpkt[i];
                    sndpkt[i] = NULL;
                    num_packets_sent--;
                    break;
                }
            }
        }
    }
    sortCache();
    WINDOW_SIZE = newWindow;
    congestion_control = 0;
    return;
}


int remove_stale_packets(int seqno)
{
    VLOG(DEBUG, "Removing all packets less than: %d", seqno);
    int pktindex = -1;
    for(int i = 0; i < WINDOW_SIZE; i++){
        if (sndpkt[i] != NULL && sndpkt[i]->hdr.seqno <= seqno){
            sndpkt[i] = NULL;
            num_packets_sent--;
	    if(WINDOW_SIZE < ssthresh){
		expandWindow(WINDOW_SIZE+1);
		writeToCSV(WINDOW_SIZE);
	    } else {
		congestion_control += 1/(WINDOW_SIZE+congestion_control);
		if(congestion_control > 1){
		    congestion_control -= 1;
		    expandWindow(WINDOW_SIZE+1);
		}
		writeToCSV(WINDOW_SIZE+congestion_control);
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
    if (sig == SIGALRM)
	{
            rounds_since_ack++;
            if(WINDOW_SIZE >= 4){
                ssthresh = (WINDOW_SIZE+congestion_control)/2.0;
            } else {
                ssthresh = 2.0;
            }
            shrinkWindow(1);
            writeToCSV(1);
            assert(sndpkt[0] != NULL);
            VLOG(DEBUG, "Last Acknowledgement %d \n", last_ackno);
	    //Resend all packets in our currently sent packets
	    // array. Since already acknowledged packets are
	    // removed from the array.
	    VLOG(INFO, "Timeout happend");
            VLOG(DEBUG, "Resending %d \n", sndpkt[0]->hdr.seqno);
            VLOG(DEBUG, "Hello");
            if(sendto(sockfd, sndpkt[0], TCP_HDR_SIZE + get_data_size(sndpkt[0]), 0, 
                      ( const struct sockaddr *)&serveraddr, serverlen) < 0){
                error("sendto");
            }

            init_timer(estimated_rtt+dev_rtt, resend_packets);
	    start_timer();
	}
}

/*
 * init_timer: Initialize timeer
 * delay: delay in milli seconds
 * sig_handler: signal handler function for resending unacknoledge packets
 */
void init_timer(double float_delay, void (*sig_handler)(int)) 
{
    int delay = (int) float_delay;
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
    int new_ack_since_dup;
    int next_seqno;
    int taken_from_cache = 0, retransmit = 0;
    int dup_ack = 0;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;
    
    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    csv = fopen("CWND.csv", "w");
    if (csv == NULL) {
	error("CWND.csv");
    }
    fprintf(csv, "TIME,CWND\n");
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
    next_seqno = 0;
    while(1){
	while (num_packets_sent < WINDOW_SIZE)
	    {
                VLOG(INFO, "Sending new packet");
                num_packets_sent++;
		pkt_index = find_empty_index();
		assert(sndpkt[pkt_index] == NULL);
		assert(pkt_index != -1);
                if(cache[0] != NULL){
                    taken_from_cache++;
                    VLOG(DEBUG, "Sending from cache");
                    sndpkt[pkt_index] = cache[0];
                    cache[0] = NULL;
                    sortCache();
                }
		if(sndpkt[pkt_index] == NULL){
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
                }
		//Wait for ACK
		VLOG(DEBUG, "Sending packet %d to %s", 
		     send_base, inet_ntoa(serveraddr.sin_addr));
                gettimeofday(&sndpkt[pkt_index]->hdr.time_sent, NULL);
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
                    init_timer(estimated_rtt+4*dev_rtt, resend_packets);
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
        VLOG(DEBUG, "Acknowledgement: %d - Last Correct Acknowledgement: %d\n", recvpkt->hdr.ackno, last_ackno);
        if(recvpkt->hdr.ackno > last_ackno){
            struct timeval tmp, diff;
            gettimeofday(&tmp, NULL);
            timersub(&tmp, &recvpkt->hdr.time_sent, &diff);
            VLOG(DEBUG, "RTT: %f", diff.tv_sec*1000.0 + diff.tv_usec/1000.0);
            estimated_rtt = (1-alpha)*estimated_rtt + alpha * ((diff.tv_sec*1000.0) + (diff.tv_usec/1000.0));
            dev_rtt = (1-beta)*dev_rtt + beta*abs(((diff.tv_sec*1000.0) + (diff.tv_usec/1000.0)) - estimated_rtt);
            VLOG(DEBUG, "Estimated RTT: %f - STD Dev RTT: %f", estimated_rtt, dev_rtt);
            new_ack_since_dup = 1;
            rounds_since_ack = 0;
	    assert(get_data_size(recvpkt) <= DATA_SIZE);
	    stop_timer();
	    if(num_packets_sent > 0){
                init_timer(estimated_rtt+dev_rtt, resend_packets);
                start_timer();
	    }
            remove_stale_packets(last_ackno);
            last_ackno = recvpkt->hdr.ackno;
            dup_ack = 0;
	} else {
            remove_stale_packets(recvpkt->hdr.ackno-1);
            dup_ack++;
            VLOG(DEBUG, "DUP ACK");
            if(dup_ack >= 3 && new_ack_since_dup){
                new_ack_since_dup = 0;
                stop_timer();
                retransmit++;
                resend_packets(SIGALRM);
                dup_ack = 0;
                VLOG(DEBUG, "Fast Retransmit");
            }
        }
        VLOG(DEBUG, "Number of Packets on the Wire: %d \n", num_packets_sent);
        VLOG(DEBUG, "Window Size: %d \n", WINDOW_SIZE);
        if(recvpkt->hdr.ackno == -1 || rounds_since_ack==500){
            VLOG(DEBUG, "Number of Fast Retransmits: %d \n", retransmit);
            VLOG(DEBUG, "Final Window Size: %d \n", WINDOW_SIZE);
	    VLOG(DEBUG, "Cached Count: %d \n", taken_from_cache);
            return 0;
        }
    }
    return 0;
}



