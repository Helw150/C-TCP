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
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"


/*
 * You ar required to change the implementation to support
 * window size greater than one.
 * In the currenlt implemenetation window size is one, hence we have
 * onlyt one send and receive packet
 */
#define CACHE_SIZE 1073741824/MSS_SIZE
tcp_packet *recvpkt;
tcp_packet *sndpkt;
tcp_packet *cache[CACHE_SIZE];

tcp_packet* find_next_packet(tcp_packet *cache[], int expected_seqno){
    int i;
    for(i = 0; i < CACHE_SIZE; i++){
        if(cache[i] != NULL && cache[i]->hdr.seqno == expected_seqno){
            tcp_packet *nxtpkt = cache[i];
            cache[i] = NULL;
            return nxtpkt;
        } else if( cache[i] != NULL && cache[i]->hdr.seqno < expected_seqno){
	    cache[i] = NULL;
	}
    }
    return NULL;
}

int main(int argc, char **argv) {
    int sockfd; /* socket */
    struct timeval time_cache;
    int expected_seqno = 0;
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    /* 
     * check command line arguments 
     */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp  = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
                sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *) buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        if ( recvpkt->hdr.data_size == 0) {
            VLOG(INFO, "End Of File has been reached");
            fclose(fp);
            
            sndpkt = make_packet(0);
            sndpkt->hdr.ackno = -1;
            sndpkt->hdr.ctr_flags = ACK;
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                       (struct sockaddr *) &clientaddr, clientlen) < 0) {
                error("ERROR in sendto");
            }
            break;
        }
        /* 
         * sendto: ACK back to the client 
         */
	int i;
	if(recvpkt->hdr.seqno == expected_seqno) {
	    //VLOG(DEBUG, "Received correct packet: %d", recvpkt->hdr.seqno);
            tcp_packet *nextpkt = recvpkt;
            time_cache = recvpkt->hdr.time_sent;
            while(nextpkt != NULL && nextpkt->hdr.seqno == expected_seqno){
                expected_seqno += recvpkt->hdr.data_size;
                fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
                nextpkt = find_next_packet(cache, expected_seqno);
            }
	} else if(recvpkt->hdr.seqno > expected_seqno){
	    //VLOG(DEBUG, "Cached Packet: %d", recvpkt->hdr.seqno);
	    for(i = 0; i < CACHE_SIZE; i++){
                if(cache[i] == NULL){
                    cache[i] = recvpkt;
                }
            }
        }
	gettimeofday(&tp, NULL);
	VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
        
	sndpkt = make_packet(0);
	sndpkt->hdr.ackno = expected_seqno;
        sndpkt->hdr.time_sent = time_cache;
        sndpkt->hdr.ctr_flags = ACK;
	//VLOG(DEBUG, "Sending Ack: %d", sndpkt->hdr.ackno);
	if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
		   (struct sockaddr *) &clientaddr, clientlen) < 0) {
	    error("ERROR in sendto");
	}
    }

    return 0;
}
