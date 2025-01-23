/*****************************************************************************\
 * Redes Integradas de Telecomunicacoes
 * MIEEC/MEEC - FCT NOVA  2022/2023
 *
 * callbacks.c
 *
 * Functions that handle main application logic for UDP communication, controlling query forwarding
 *
 * Updated on August 26, 2022
 * @author  Luis Bernardo
 \*****************************************************************************/

#include <gtk/gtk.h>
#include <arpa/inet.h>
#include <assert.h>
#include <time.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sock.h"
#include "gui.h"
#include "callbacks.h"
#include "callbacks_socket.h"
#include "proxy_thread.h"

#ifdef DEBUG
#define debugstr(x)     g_print(x)
#else
#define debugstr(x)
#endif

/**********************\
|*  Global variables  *|
 \**********************/

gboolean active = FALSE; 	// TRUE if server is active

GList *qlist = NULL;			// List of active queries

/*********************\
|*  Local variables  *|
 \*********************/

// Temporary buffer used for writing, logging, etc.
static char tmp_buf[8000];

// Local functions
gboolean callback_query_timeout(gpointer data);


/*****************************************\
|* Functions that handle the Query list  *|
\*****************************************/

// Create a Query descriptor and put it in qlist; starts the timer
Query *new_Query(const char *filename, uint16_t seq, gboolean is_ipv6, struct in6_addr *ipv6,struct in_addr *ipv4, u_short porto, char* buf, int bufLen) {
	Query *pt;
	assert((filename!=NULL));
	pt = (Query *) malloc(sizeof(Query));
	strncpy(pt->name, filename, sizeof(pt->name));
	pt->seq = seq;
	pt->thread = NULL;
	pt->is_ipv6 = is_ipv6;
	sprintf(pt->qname, "'%s'(%d)%s", filename, seq, is_ipv6 ? "v6" : "v4");

	// ############ part of TASK 2  ############
	// Complete add code to store the extra parameters in the Query structure
	//
	pt->timer_id = 0;
	pt->timer_id2 = 0;

	if(is_ipv6){
		pt->ipv6 = (struct in6_addr*) malloc(sizeof(struct in6_addr));
		memcpy(pt->ipv6, ipv6, sizeof(struct in6_addr));
		pt->ipv4 = NULL;
	}
	else{
		pt->ipv4 = (struct in_addr*) malloc(sizeof(struct in_addr));
		memcpy(pt->ipv4, ipv4, sizeof(struct in_addr));
		pt->ipv6 = NULL;
	}

	pt->port = porto;

	pt->state=S_JITTER;

	pt->buf_temp = (char*) malloc(bufLen + 1);
	memcpy(pt->buf_temp, buf, (bufLen + 1));
	pt->tmp_buflen = bufLen;

	pt->self_ = pt;
	qlist = g_list_append(qlist, pt);

	return pt;
}

// Search for Query descriptor in qlist
Query *locate_in_QueryList(const char *filename, uint16_t seq) {
	assert(filename != NULL);
	GList *list;
	for (list = qlist; list != NULL; list = g_list_next(list)) {
		if (((Query *) list->data)->seq == seq)
			if (!strcmp(filename, ((Query *) list->data)->name))
				return (Query *) list->data;
	}

	return NULL;
}

// Search for Query descriptor in qlist
Query *locate_in_QueryList_IP(const char *filename, uint16_t seq, gboolean is_ipv6) {
	assert(filename != NULL);
	GList *list;
	for (list = qlist; list != NULL; list = g_list_next(list)) {
		Query *q = (Query *) list->data;
		if (q->seq == seq)
			if (!strcmp(filename, q->name))
				if (q->is_ipv6 == is_ipv6)
					if (q->self_ == q)
						return (Query *) list->data;
	}
	return NULL;
}

// Free Query descriptor and all pending memory
// called_from_GUI - use TRUE if called from a GUI event; FALSE otherwise (i.e. from socket, thread or timer event)
void del_Query(Query *q, gboolean called_from_GUI) {
	if ((q == NULL) || (q->self_ != q))
		return;


	q->self_ = NULL;	// It is being freed

	qlist = g_list_remove(qlist, q);

	// ############ part of TASKs 2 to 6 ############
	// Complete putting here the code to stop and free everything
	// ????

	// Delete from GUI
	GUI_del_Query(q->name, q->seq, q->is_ipv6, called_from_GUI);

	free(q->buf_temp);
	free(q->ipv4);
	free(q->ipv6);

	free(q);
}

// Abort and free all active queries
void del_query_list(gboolean called_from_GUI) {
	while (qlist != NULL) {
		Query *pt = (Query *) qlist->data;
		del_Query(pt, called_from_GUI);
		if ((qlist!=NULL) && (pt == qlist->data)) {
			fprintf(stderr, "Internal error in del_query_list()\n");
			break;
		}
	}
}


/*******************************************************\
|* Functions to control the state of the application   *|
 \*******************************************************/

// Auxiliary function that extracts the directory name from a full path name
static const char *get_trunc_filename(const char *FileName) {
	char *pt = strrchr(FileName, (int) '/');
	if (pt != NULL)
		return pt + 1;
	else
		return FileName;
}

// Start timer
void start_query_timer(Query *q, long int timeout) {
	if ((q == NULL) || (q->self_ != q))
		return;

	// ############ part of TASK 5 ############
	if(q->state == S_JITTER){
		g_timeout_add(timeout,                    //Numero de milisegundos
	                  callback_query_timeout,    //Funcao invocada
					  q);                        //Argumento passado a funcao

		//Log("Jitter timer ON.\n");

	}

	// ############ part of TASK 4 ############
	if(q->state==S_IDLE){

		q->timer_id = g_timeout_add(timeout,
									callback_query_timeout,
									q);

		if(q->timer_id < 0){
			Log("Failed to create timer.\n");
			return;
		}

		q->state=S_TIMER;
	}

	if(q->state == S_TRY_TCP){

		q->timer_id2 = g_timeout_add(timeout,
								     callback_query_timeout,
									 q);

		if(q->timer_id2 < 0){
			Log("Failed to create timer.\n");
			return;
		}
	}



	#ifdef DEBUG
		fprintf(stderr, "%s started timer %u : %ld ms\n", q->qname, q->timer_id, timeout);
	#endif
}

// Stop timer
void stop_query_timer(Query *q) {
	if ((q == NULL) || (q->self_ != q))
		return;

	// ############ part of TASK 4 ############
	if ((q->timer_id > 0) && (q->state == S_TIMER)){
		 g_source_remove(q->timer_id);
		 q->timer_id=0;

		 Log("HIT Timer canceled \n");

		#ifdef DEBUG
				fprintf(stderr, "%s stopped timer %u\n", q->qname, q->timer_id);
		#endif
	}

	if((q->timer_id2 > 0) && (q->state == S_TRY_TCP)){
		g_source_remove(q->timer_id2);
		q->timer_id2 = 0;

		Log("Connection Timer canceled\n");

		#ifdef DEBUG
				fprintf(stderr, "%s stopped timer %u\n", q->qname, q->timer_id2);
		#endif
	}
}


// Callback that handles query timeouts
gboolean callback_query_timeout(gpointer data) {
	Query *q = (Query *) data;
	if ((q == NULL) || (q->self_ != q)) {
		fprintf(stderr, "Error in callback_query_timeout - invalid data\n");
		return FALSE; // stop timer
	}

	if (q->qname != NULL)
		g_print("Callback query_timeout (%s)\n", q->qname);
	else
		g_print("Callback query_timeout (NULL)\n");

	if(q->state == S_JITTER){

			Log("End of Jitter timer.\n");

			//gboolean write_query_message(char *buf, int *len, uint16_t seq, const char* filename)
			if(!write_query_message(q->buf_temp, &q->tmp_buflen, q->seq, q->name)){
				del_Query(q, FALSE);
				return FALSE;
			}
			//gboolean send_multicast(const char *buf, int n, gboolean use_IPv6)
			if(!send_multicast(q->buf_temp, q->tmp_buflen, !(q->is_ipv6))){
				del_Query(q, FALSE);
				return FALSE;
			}

	        send_multicast(q->buf_temp, q->tmp_buflen, !(q->is_ipv6));
	        q->state=S_IDLE;

	        //Task 4 - start the Query timer to limit the waiting time for an HIT message
	        start_query_timer(q, QUERY_TIMEOUT);
	        Log("Started timeout.\n");

	    }


	// ############ part of TASKs 4 ############

	else if((q->state == S_TIMER) || (q->state == S_TRY_TCP)){
		//Para o timer -> Apaga a query -> apaga-a do GUI
		stop_query_timer(q);
	    del_Query(q, FALSE);
	}
	// ############ part of TASKs 4-6, 7-8 ############
	// Put here what you should do about when the timer ends!

	// It should depend on the query state

	// If you are running a jitter timer before transmitting the Query, you should send the Query
	//  using send_multicast. The jitter value should be set to
	//			long int jitter_time = (long) floor(1.0 * random() / RAND_MAX * QUERY_JITTER);

	// If you are waiting for a Hit, cancel the pending Query

	// If you are waiting for a connection, cancel the pending Query

	// Otherwise, it is a mistake

	return FALSE;	// Stop the timer
}


// Handle the reception of a Query packet  (is_ipv6, ipv6, ipv4 and port contain the sender's address)
void handle_Query(char *buf, int buflen, gboolean is_ipv6,
		struct in6_addr *ipv6, struct in_addr *ipv4, u_short port) {
	uint16_t seq;
	const char *fname;

	assert((buf != NULL) && ((is_ipv6&&(ipv6!=NULL)) || (!is_ipv6&&(ipv4!=NULL))));
	if (!read_query_message(buf, buflen, &seq, &fname)) {
		Log("Invalid Query packet\n");
		return;
	}

	assert(fname != NULL);
	if (strcmp(fname, get_trunc_filename(fname))) {
		Log("ERROR: The Query must not include the pathname - use 'get_trunc_filename'\n");
		return;
	}

	char tmp_ip[100];
	if (is_ipv6)
		strcpy(tmp_ip, addr_ipv6(ipv6));
	else
		strcpy(tmp_ip, addr_ipv4(ipv4));
	if (is_local_ip(tmp_ip) && (port == portUDPq)) {
		// Ignore local loopback
		return;
	}

	sprintf(tmp_buf, "Received Query '%s'(%d) from [%s]:%hu\n", fname, seq,
			(is_ipv6 ? addr_ipv6(ipv6) : addr_ipv4(ipv4)), port);
	Log(tmp_buf);

	// ############ part of TASKs 2,4,5 ############

	// Check if it is a new query (equal name+seq+is_ipv6) searching the Query list - ignore the query if it is an old one
	// Check if the query has appeared in the !is_ipv6 domain - if it has, ignore it because someone else send it before.
	// Use the GUI or qlist  ...
	Query * q= NULL;
	q= (Query*) locate_in_QueryList_IP(fname,seq,is_ipv6);

	if(q==NULL){

		Query* new_query = new_Query(fname, seq, is_ipv6 , ipv6, ipv4, port, buf, buflen);

		long int jitter_time=(long)floor(1.0*random())/RAND_MAX*QUERY_JITTER;
		start_query_timer(new_query,jitter_time);
	}

	// If it is new, create a new Query struct and store it in your list
	// Store the query information in the list and start the jitter Timer
	//		new_Query(fname, seq, is_ipv6, ...);
	//
	// NOTE: If you think that this qlist stuff is just too much for you, you canuse the graphical functions in gui.h (but they are less powerful)

	// Start by forwarding here the Query message to the other domain (i.e. !is_ipv6) using:
	//		send_multicast(buf, buflen, !is_ipv6);
	//
	// At the end, if you have time, start here a jitter timer, which will send the Query later!
	//This helps when there are more than one gateway connecting two multicast groups!

	// Add the query to the graphical Query list
	 GUI_add_Query(fname, seq, is_ipv6, strdup(tmp_ip), port);
	 Log("Query added to GUI\n");
}


// Handle the reception of an Hit packet
void handle_Hit(char *buf, int buflen, struct in6_addr *ip, u_short port,
		gboolean is_ipv6) {
	char hbuf[MSG_BUFFER_SIZE];		// sending HIT buffer
	int hlen;						// sending HIT message length
	uint16_t seq;
	const char *fname;
	unsigned long long flen;
	uint32_t fhash;
	unsigned short sTCP_port;
	const char *serverIP;

	if (!read_hit_message(buf, buflen, &seq, &fname, &fhash, &flen,
			&sTCP_port, &serverIP)) {
		Log("Received invalid Hit packet\n");
		return;
	}

	sprintf(tmp_buf,
			"Received Hit '%s' (IP= %s; port= %hu; Len=%llu; Hash=%hu) from [%s]:%hu\n",
			fname, serverIP, sTCP_port, flen, fhash, addr_ipv6(ip), port);
	Log(tmp_buf);

	// ############ part of TASKs 3,6 ############

	// Test here if this HIT matches one of the pending Query contents
	// If not, ignore the Hit received
	Query * query_hit= NULL;
	query_hit=locate_in_QueryList(fname, seq);

	if(query_hit==NULL || (query_hit->state!=S_TIMER) ){
		return;
	}

	Log("Stopping Timer an HIT has arrived.\n");
	stop_query_timer(query_hit);
	query_hit->state=S_HIT;
	sprintf(tmp_buf, "%s-%hu", addr_ipv6(ip), sTCP_port);

	// Add HIT to GUI list
	if(!GUI_add_hit_to_Query(fname, seq, !is_ipv6, tmp_buf)){
		Log("ERROR - The received Hit was not added to the GUI.\n");
		return;
	}

	if ((query_hit->is_ipv6) /* HIT comes from IPv4 fileexchange == QUERY came from IPv6 */ ) {
		/***********************************************/
		/**** HIT received from an IPv4 fileexchange ***/
		/***********************************************/

		Log("--> HIT IPV4\n");

		// Send the HIT message to the client
		// You should get the client information from your Query list.
		// You may also get the client's information from the graphical table calling
		// gboolean GUI_get_Query_details(const char *filename, uint16_t seq, gboolean is_ipv6, const char **str_ip, unsigned int *port, const char **hits);
		//	   str_ip has the IP address and port has the port number of the client.
		//write_hit_message(hbuf, &hlen, seq, fname, fhash, flen, sTCP_port, serverIP);
		if(!write_hit_message(hbuf, &hlen, seq, fname, fhash, flen, sTCP_port, addr_ipv6(&local_ipv6))){
			Log("ERROR - The Hit message couldn't be created.\n");
			return;
		}

		//(char *buf, int *len, uint16_t seq, const char* filename, uint32_t fhash,
		//							unsigned long long flen, unsigned short sTCP_port, const char *serverIP)
		//{
		// Use the function write_hit_message(hbuf, &hlen, seq, fname, fhash, flen, sTCP_port, ...) to prepare the Hit message
		//     in buffer hbuf, where the missing parameter is a string with the server's IP address converted from IPv4 to IPv6 in string format

		// Send the HIT packet to the client
		//    use the function send_M6reply(... , ... , hbuf, hlen) to send the Hit
		//gboolean send_M6reply(struct in6_addr *ip, u_short port, const char *buf, int n)
		if(!send_M6reply(query_hit->ipv6, query_hit->port, hbuf, hlen)){
			Log("ERROR - The Hit was not sended.\n");
			return;
		}
		// In order to avoid not seeing the Query in the graphical table, I recommend that you wait for timeout to clear the GUI entry
		// Otherwise, you can clear it from the GUI table here using:
		//GUI_del_Query(fname, seq, !is_ipv6, FALSE);


//#ifdef DEBUG
//		sprintf(tmp_buf,
//				"Sent Hit '%s' (IP= %s, port= %hu; Len=%llu; Hash=%u) to %s:%hu\n",
//				fname, /*server_ipv6*/, portTCP, flen, fhash, addr_ipv6(&local_ipv6), q->snd_port);
//		Log(tmp_buf);
//#endif

		// Wait for timeout to clear the GUI entry
		return;

	}

	/***********************************************/
	/**** HIT received from an IPv6 fileexchange => Query came from IPV4***/
	/***********************************************/
	// ############ TASK 6 ############

	if(!(query_hit->is_ipv6)){

		Log("--> HIT IPV6\n");

		const char *hits;
		//gboolean GUI_get_Query_hits(const char *filename, uint16_t seq, gboolean is_ipv6, const char **hits)
		GUI_get_Query_hits(fname, seq, is_ipv6, &hits);

		// Test if an HIT was previously received and a thread is already active ...
		if((hits != NULL) && (locate_state_in_plist(fname, seq) != NULL)){
			Log("NULL Hit.\n");
			return;
		}

		//gboolean write_hit_message(char *buf, int *len, uint16_t seq, const char* filename, uint32_t fhash,
		//		unsigned long long flen, unsigned short sTCP_port, const char *serverIP);
		if(!write_hit_message(hbuf, &hlen, seq, fname, fhash, flen, portTCP, addr_ipv4(&local_ipv4))){
			Log("ERROR - IPv4 write Hit failed.\n");
			return;
		}

		if(!send_message4(query_hit->ipv4, query_hit->port, hbuf, hlen)){
			Log("ERROR - IPv4 Hit failed to send.\n");
			return;
		}
		if(!GUI_add_Proxy(fname, seq)){
			Log("ERROR - Failed to add Hit to GUI interface.\n");
			return;
		}

		query_hit->state = S_TRY_TCP;
		//Task 8
		start_query_timer(query_hit, HIT_CONNECTION_TIMEOUT);

		return;

	// Prepare a new HIT message with the proxy information and send it to the client.
	// Use  write_hit_message(...) and
	//		send_message4(&client_ipv4_address, client_port, HIT_buffer, HIT_buflen)
	// where client_ipv4_address and client_port should be in the Query structure
	//
	// Remember that you need to send a valid IPv4 address to the IPv4 fileexchange, identifying
	//  the gateway TCP port, and later, when you receive the TCP communication, this
	//  thread will be associated with the Query.
	// You may add the proxy information to the GUI, just to inform that you are expecting a connection using
//	GUI_add_Proxy(filename, seq);
	// where filename and seq are stored in the Query structure

	// Restart the timer, to wait for up to QUERY_TIMEOUT microseconds for a connection
	// In this case you need to create a new proxy object and add it to the GUI
	}
}


// Handle the reception of a new connection on the TCP server socket
// Return TRUE if it should accept more connections; FALSE otherwise
gboolean handle_new_connection(int sock, struct sockaddr_in6 *cli_addr) {
	assert(sock >= 0);
	assert(cli_addr != NULL);

	// Create a new thread state object - you will need it to pass it to the thread!
	thread_state *state = new_thread_state(sock, cli_addr);

	// Start a new thread
	int err = pthread_create(&state->tid, NULL, proxy_function, (void *) state);
	if (err) {
		fprintf(stderr, "Error starting thread: return code %d\n", err);
	}

	return TRUE;	// Keep accepting more connections
}


// Callback button 'Stop': stops the selected TCP transmission and associated proxy
void on_buttonStop_clicked(GtkButton *button, gpointer user_data) {
	GtkTreeIter iter;
	const char *fname;
	uint16_t seq;
	int Tsock;

	if (GUI_get_selected_Proxy(&fname, &seq, &Tsock, &iter)) {
#ifdef DEBUG
		g_print("Proxy with socket %d will be stopped\n", Tsock);
#endif
	} else {
		Log("No proxy selected\n");
		return;
	}
	if (Tsock <= 0) {
		Log("Invalid TCP socket in the selected line\n");
		return;
	}

	gtk_list_store_remove(main_window->listProxies, &iter);

	// Stop proxy
	sprintf(tmp_buf, "Stopping Query/thread to %s:%d\n", fname, seq);
	Log(tmp_buf);
	// ############ TASK 9 ############
	Log("on_buttonStop_clicked not implemented yet\n");
	// ...
	// you can use the function stop_thread(fname, seq, TRUE), after completing it ...
	// you also need to delete the query associated to the thread using del_Query.

	//Task 9
	if(stop_thread(fname, seq, TRUE)){
		Query* q = locate_in_QueryList(fname, seq);

		del_Query(q, TRUE);
	}
}


// Closes everything
void close_all(gboolean called_from_GUI) {
	// Stop all active queries
	del_query_list(called_from_GUI);
	// Close all sockets
	close_sockTCP();
	close_sockUDP();
	// Stop threads
	close_all_threads(called_from_GUI);
}


// Button that starts and stops the application
void on_togglebuttonActive_toggled(GtkToggleButton *togglebutton,
		gpointer user_data) {

	if (gtk_toggle_button_get_active(togglebutton)) {

		// *** Starts the server ***
		const gchar *addr4_str, *addr6_str;
		int n4, n6;

		n4 = get_PortIPv4Multicast();
		n6 = get_PortIPv6Multicast();
		if ((n4 < 0) || (n6 < 0)) {
			Log("Invalid multicast port number\n");
			gtk_toggle_button_set_active(togglebutton, FALSE); // Turns button off
			return;
		}
		port_MCast4 = (unsigned short) n4;
		port_MCast6 = (unsigned short) n6;

		addr6_str = get_IPv6Multicast(NULL);
		addr4_str = get_IPv4Multicast(NULL);
		if (!addr6_str && !addr4_str) {
			gtk_toggle_button_set_active(togglebutton, FALSE); // Turns button off
			return;
		}
		if (!init_sockets(port_MCast4, addr4_str, port_MCast6, addr6_str)) {
			Log("Failed configuration of server\n");
			gtk_toggle_button_set_active(togglebutton, FALSE); // Turns button off
			return;
		}
		set_PID(getpid());
		//
		block_entrys(TRUE);
		active = TRUE;
		Log("gateway active\n");

	} else {

		// *** Stops the server ***
		active = FALSE;
		close_all(TRUE);
		block_entrys(FALSE);
		set_PID(0);
		Log("gateway stopped\n");
	}

}


// Callback function that handles the end of the closing of the main window
gboolean on_window1_delete_event(GtkWidget * widget, GdkEvent * event,
		gpointer user_data) {
	gtk_main_quit();	// Close Gtk main cycle
	return FALSE;// Must always return FALSE; otherwise the window is not closed.
}

