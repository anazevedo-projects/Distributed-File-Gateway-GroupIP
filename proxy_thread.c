/*****************************************************************************\
 * Redes Integradas de Telecomunicacoes
 * MIEEC/MEEC - FCT NOVA  2022/2023
 *
 * proxy_thread.c
 *
 * Functions that implement the proxy threads, which bind IPv4 clients to IPv6 servers
 *
 * Updated on August 26, 2022
 * @author  Luis Bernardo
\*****************************************************************************/

#include <pthread.h>
#include <gtk/gtk.h>
#include <arpa/inet.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include "sock.h"
#include "gui.h"
#include "callbacks.h"
#include "callbacks_socket.h"
#include "proxy_thread.h"


GList *plist= NULL;			// List of active proxy threads

/******************************************\
|* Functions that handle the thread list  *|
\******************************************/

// Create a new thread state object
thread_state *new_thread_state(int sock4, struct sockaddr_in6 *cli_addr) {
	assert(sock4 >= 0);
	thread_state *pt = (thread_state *) malloc(sizeof(thread_state));
	pt->status= INITIAL_STATE;
	memcpy(&pt->cli_ip, &cli_addr->sin6_addr, 16);
	pt->cli_port= ntohs(cli_addr->sin6_port);
	pt->sock4 = sock4;
	pt->sock6 = -1;
	pt->filename = NULL;
	pt->seq= -1;
	pt->q= NULL;

	pt->self = pt;
	plist= g_list_append(plist, pt);

	return pt;
}

// Update the information about the IPv6 server
void update_thread_state(thread_state *pt, int sock6, const char *fname, u_int16_t seq) {
	assert(pt != NULL);
	pt->sock6 =sock6;
	pt->filename = strdup(fname);
	pt->seq= seq;
}

// Search for thread_state descriptor in plist using the filename and sequence number
thread_state *locate_state_in_plist(const char *filename, u_int16_t seq) {
	assert(filename != NULL);
	GList *list;
	for (list = plist; list != NULL; list = g_list_next(list)) {
		if (((thread_state *) list->data)->seq == seq)
			if (!strcmp(filename, ((thread_state *) list->data)->filename))
				return (thread_state *) list->data;
	}
	return NULL;
}


// Free a thread state object, removing it from the list, clearing all info from the GUI
// and freeing all memory previously allocated
void free_thread_state(thread_state *pt, gboolean called_from_GUI) {
	if ((pt==NULL) || (pt->self != pt))
		return;

	pt->self = NULL;	// It is being freed

	// Remove from proxy thread list
	plist = g_list_remove(plist, pt);

	// Clear GUI table
	GUI_del_Proxy(pt->filename, pt->seq, pt->sock4, called_from_GUI);

	// Get pointer to Query
	Query *q= pt->q;
	if ((q == NULL) && (pt->filename != NULL))
		q= locate_in_QueryList_IP(pt->filename, pt->seq, FALSE);
	if (q != NULL) {
		del_Query(q, called_from_GUI);
		q->thread= NULL;
	}

	// Free memory
	if (pt->filename != NULL) {
		free(pt->filename);
		pt->filename= NULL;
	}
	if (pt->sock6 != -1) {
		close(pt->sock6);
		pt->sock6 = -1;
	}
	Log("free_thread_state did not close socket IPv4\n");
	// add the closing of socket to fileexchange IP4
	// ????

	free(pt);
}


// Stop a thread identified by the filename and the sequence number
gboolean stop_thread(const char *filename, u_int16_t seq, gboolean called_from_GUI) {
	thread_state *pt= locate_state_in_plist(filename, seq);
	if (pt == NULL)
		return FALSE;
	free_thread_state(pt, called_from_GUI);
	return TRUE;
}


// Close all threads
void close_all_threads(gboolean called_from_GUI) {
	while (plist != NULL) {
		thread_state *pt= (thread_state *)plist->data;
		if (pt == NULL)
			continue;
		if ((pt->q != NULL) && (pt->q->thread != NULL))
			pt->q->thread= NULL;
		free_thread_state(pt, called_from_GUI);	// Clear the object and remove from the list
		if ((plist != NULL) && (pt == plist->data)) {
			fprintf(stderr, "Internal error in close_all_threads()\n");
			break;
		}
	}
}


/*****************************************************************************************\
|* Functions that implement the proxy and handle the communication between IPv4 and IPv6 *|
\*****************************************************************************************/


// Create a connection to (ip,port) and return the socket TCP
static int connect_to_ipv6_server(const char *ip, uint port) {
	// Creates TCP socket
	struct hostent *hp, *gethostbyname2();
	struct sockaddr_in6 server;
	int sockTCP;

	assert(ip != NULL);

	/* Connect socket using the name specified in the command line. */
	hp = gethostbyname2(ip, AF_INET6);
	if (hp == 0) {
		fprintf(stderr, "%s: unknown host\n", ip);
		return -1;
	}
	server.sin6_family = AF_INET6;
	server.sin6_flowinfo = 0;
	server.sin6_port = htons(port);
	bcopy(hp->h_addr, &server.sin6_addr, hp->h_length);
	// Creates TCP socket
	sockTCP = init_socket_ipv6(SOCK_STREAM, 0, FALSE);
	if (sockTCP < 0) {
		Log("Failed opening IPv6 TCP socket\n");
		return -1;
	}

	if (connect(sockTCP, (struct sockaddr *) &server, sizeof(server)) < 0) {
		perror("connecting stream socket");
		fprintf(stderr, "Failed connecting IPv6 TCP socket to %s-%hu\n",
				ip, port);
		close(sockTCP);
		return -1;
	}
	return sockTCP;
}


// Connect to one file server, cycling through all hits received
int connect_to_file_server(thread_state *state, const char *filename, u_int16_t seq) {
	// Locate IPv6 server with file requested
	const char *hits;
	char hits_buf[512];

	if (!GUI_get_Query_hits(filename, seq, FALSE/*IPv4*/, &hits)) {
		// No hits available
		return -1;
	}

	fprintf(stderr, "Filename='%s' Seq=%d Hits=%s\n", filename, seq, hits);
	strncpy(hits_buf, hits, sizeof(hits_buf));

	char *next = hits_buf, *pt, *ip;
	int port;
	int sock = -1;
	do {
		ip = next;
		pt = strchr(ip, '-');
		if (!pt) {
			// Invalid hits format
			return -1;
		}
		*pt = '\0';
		port = strtol(pt + 1, &next, 10);
		while ((next != NULL) && (*next==' '))
			next++;	// Skip spaces

		printf("Trying connection to %s:%d\n", ip, port);
		sock = connect_to_ipv6_server(ip, port);

	} while ((sock < 0) && (next != NULL));

	if (sock >= 0) {
		// Update Proxy information
		GUI_update_serv_details_Proxy(state->sock4, ip, port);
	}
	return sock;
}


// Update the % transmitted on the GUI
gboolean update_transf(thread_state *pt, int transf) {
	if (!GUI_update_transf_Proxy(pt->sock4, transf))
		printf("GUI update transfer failed\n");
	return TRUE;
}


// Function that implements the thread function:
//		it implements all communications between client fileexchange IPv4 and server fileexchange IPv6
//		ptr - pointer to the thread state object
void *proxy_function(void *ptr) {
	assert(ptr != NULL);
	thread_state *pt= (thread_state *)ptr;
	gboolean slow = get_checkbutton_Slow_state();	// get the slow state from the checkbox

	char conn_str[20];		// Temporary buffer with the thread name
	char write_buf[256];	// Write temporary buffer for logging
	char buf[FILE_BUFLEN];				// Temporary data buffer for file transfer

	struct timeval 	tv1, tv2; // To measure file transfer delay
	struct timezone tz;		  // Auxiliary variable

	uint16_t seq;				// Request header variable - sequence number
	int16_t namelen;			// Request header variable - namelength
	unsigned long long flen;	//File Length -- ADICIONEI VERIFICAR SE FIZ BEM EM CRIAR UMA NOVA OU REUTILIZO A DE BAIXO
	unsigned long long f_diff;
	long diff;					// % of bytes received
	int n= -1;
	Query *q= NULL;

	sprintf(conn_str, "th(%d): ", pt->sock4);
	if (!active || (pt->self != pt)) {
		sprintf(write_buf, "%sInvalid state pointer\n", conn_str);
		Log(write_buf);
		pthread_exit(NULL);
	}

	// ############ part of TASK 10 ############
	// Configure your socket IPv4 to define a timeout time for reading operations and
	// to set buffers or other any configuration that maximizes throughput
	//	e.g. SO_SNDBUF, SO_RECVBUF, timeout, etc.
	// ???

	// Read seq
	if (!active || (read(pt->sock4, &seq, sizeof(seq)) != sizeof(seq))) {
		sprintf(write_buf, "%sDid not receive seq\n", conn_str);
		Log(write_buf);

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
	}
	// Read the name length
	if (!active || (read(pt->sock4, &namelen, sizeof(namelen)) != sizeof(namelen))) {
		sprintf(write_buf, "%sDid not received the filename's length\n",
				conn_str);
		Log(write_buf);

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
	}
	if (namelen > 256) {
		sprintf(write_buf, "%sInvalid filename's length (%d)\n", conn_str, namelen);
		Log(write_buf);

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
	}

	// ############ TASK 7 ############
	// Complete this function to implement the communication between the two fileexchanges IPv4 and IPv6,
	//  forwarding the signaling messages and file contents, and validating the signaling contents.
	//  You may need to add extra fields to the thread_state structure

	// Read filename
	// ???
	// (do not forget to use an existing buffer to receive the filename)
	//buf = (char *) malloc(namelen + 1);

	// Read filename
	if (!active || (read(pt->sock4, buf, namelen) != namelen)) {
		sprintf(write_buf, "%sInvalid filename %s\n", conn_str, buf);
		Log(write_buf);

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
	}


	// Update Proxy client information
	// use GUI_update_cli_details_Proxy(filename, seq, pt->sock4, addr_ipv6(&pt->cli_ip), pt->cli_port);
	GUI_update_cli_details_Proxy(buf, seq, pt->sock4, addr_ipv6(&pt->cli_ip), pt->cli_port);
	Log("GUI client UPDATED\n");

	// Locate the Query state associated with the connection
	// q= locate_in_QueryList_IP(filename, seq, FALSE);
	// and update the state on both structures to store the association - Query e Thread
	//filename is on buf
	q = locate_in_QueryList_IP(buf, seq, FALSE);
	pt->status = ACTIVE4_STATE;

	// Connect to fileexchange on IPv6, creating socket pt->sock6.
	// use connect_to_file_server(pt, filename, seq);
	pt->sock6 = connect_to_file_server(pt, buf, seq);
	stop_query_timer(q);
	pt->status = ACTIVE6_STATE;
	q->state = S_CONNECT;
	Log("Established Connection with sock6 \n");

	// ############ part of TASK 10 ############
	// Configure your socket IPv4 to define a timeout time for reading operations and
	// to set buffers or other any configuration that maximizes throughput
	//	e.g. SO_SNDBUF, SO_RECVBUF, timeout, etc.
	// ???


	// Update state
	// update_thread_state(pt, pt->sock6, filename, seq);
	update_thread_state(pt, pt->sock6, buf, seq);

	// Send request to IPv6 filexchange
	// ???
	if (!active || (write(pt->sock6, &seq, sizeof(seq)) != sizeof(seq))) {
		Log("Couldn't write seq on Socket6.\n");

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
		return FALSE;

	}

	if(!active || (write(pt->sock6, &namelen, sizeof(namelen)) != sizeof(namelen))){
		Log("Couldn't write File Length of file.\n");

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
		return FALSE;
	}
	if(!active || (write(pt->sock6, buf, namelen) != namelen)){
		Log("Couldn't write File Name of file.\n");

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
		return FALSE;
	}

	// Receive the file length from the IPv6 filexchange
	// ???
	if (!active || (read(pt->sock6, &flen, sizeof(unsigned long long)) != sizeof(unsigned long long))) {
		sprintf(write_buf, "%sInvalid filename %s\n", conn_str, buf);
		Log(write_buf);

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
	}

	// Send length to IPv4 filexchange
	// ???
	if(!active || (write(pt->sock4, &flen, sizeof(flen)) != sizeof(flen))){
		Log("ERROR: Sending length of file to IPv4.\n");

		free_thread_state(pt, FALSE);
		pthread_exit(NULL);
		return FALSE;
	}

	// Test if file is empty
	// ???

	if(flen == 0){
		Log("This file doesn't exist.\n");
		return FALSE;
	}

	// Memorize the time when transmission starts
	if (gettimeofday(&tv1, &tz))
		Log("Error getting time\n");

	// Receive file from fileexchange ipv6 and forward it to fileexchange ipv4
	// See the file copy example in the documentation, and adapt to a socket scenario ...
	// do { // Loop forever until end of file
	//		n = read(pt->sock6, buf, FILE_BUFLEN);
	//  	...
	// 		update the % of file transmitted using: update_transf(pt, percent);
	//					percent should be a value between 0 and 100.
	//  	...
	//		if (slow)
	//			usleep(SLOW_SLEEPTIME);
	//  } while (active && (n > 0) && ????);
	pt->status = S_TRANSF;			//THREAD status
	q->state = S_F_TRANSF;			//QUERY	status

	u_int transf = 0;

	do{
		//received file from fileexchange ipv6
		n = read(pt->sock6, buf, FILE_BUFLEN);
		Log("File received from ipv6.\n");
		f_diff += n;

		//forward it to fileexchange ipv4
		if(write(pt->sock4, buf, n) != n){
			Log("ERROR - Not all data was forwarded to IPv4.\n");

			free_thread_state(pt, FALSE);
			pthread_exit(NULL);
			return FALSE;
		}
		Log("Sending file to ipv4.\n");

		transf = (float) ((float)f_diff/(float)flen) * 100;

		//percentage of the transfer
		update_transf(pt, (int) transf);
		//gboolean GUI_update_transf_Proxy(u_int TCPsock, u_int transf);
		GUI_update_transf_Proxy(pt->sock4, transf);

		//to slow down the speed
		if(slow){
			usleep(SLOW_SLEEPTIME);
		}

	}while(active && (n > 0));


	if (gettimeofday(&tv2, &tz)) {
		g_print("%sError getting time\n", conn_str);
		diff= 0;
	} else
		diff= (tv2.tv_sec-tv1.tv_sec)*1000000+(tv2.tv_usec-tv1.tv_usec);
	g_print("%sproxy ended - lasted %ld usec\n", conn_str, diff);

	// Wrap up
	free_thread_state(pt, FALSE);

	return NULL;
}
