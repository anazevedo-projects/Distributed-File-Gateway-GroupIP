/*****************************************************************************\
 * Redes Integradas de Telecomunicacoes
 * MIEEC/MEEC - FCT NOVA  2022/2023
 *
 * callbacks.h
 *
 * Header file of functions that handle main application logic for UDP communication,
 *    controlling query forwarding
 *
 * Updated on August 26, 2022
 * @author  Luis Bernardo
\*****************************************************************************/

#include <gtk/gtk.h>

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE (!FALSE)
#endif


// Program constants
#define CNAME_LENGTH			80		// Maximum filename
#define MSG_BUFFER_SIZE			64000	// Message buffer size
#define QUERY_JITTER			100		/* Jitter time for Query retransmission - 100 mseconds */
#define QUERY_TIMEOUT			10000	/* Query timeout - 10 seconds */
#define HIT_CONNECTION_TIMEOUT	10000	/* Wait for connection timeout - 10 seconds */


struct Hit;
struct Query;



#ifndef INCL_CALLBACKS_
#define INCL_CALLBACKS_

#include "gui.h"
#include "proxy_thread.h"


// typedef enum { ??? } QueryState;

typedef enum {S_JITTER,					//Waiting the JITTER time to send the query
			  S_IDLE,					//After sending the query on multicast
			  S_TIMER,					//Waiting for a HIT
			  S_HIT, 					//Received a HIT
			  S_TRY_TCP, 				//The HIT was sended to the sender fileexchange
			  S_CONNECT,				//GateWay has been connected to both ip's
			  S_F_TRANSF				//The transfer file is ON
} QueryState;

// Query information
typedef struct Query {
	char			qname[CNAME_LENGTH+41];	// Query string (for logging)
    int				seq;					// Sequence number
    char			name[CNAME_LENGTH+1]; 	// Name looked up

    // Sender information
    gboolean		is_ipv6;				// Sender domain: TRUE - IPv6 ; FALSE - IPv4

    // State information
    thread_state   *thread;
    // Thread state, when a thread is active

    struct in6_addr *ipv6;					//IPv6
	struct in_addr *ipv4;					//IPv4
    u_short port;							//Port

    QueryState state;						//Status of Query
	guint timer_id;							//ID timer that limits the waiting time for a HIT message
	guint timer_id2;						//ID timer for connection time out

	char *buf_temp;							//Buffer used to store query information
	int tmp_buflen;							//Length of buffer used to store query information

    /*
     * 	To complete during TASKs 2, 3, 4, 5
     * 	...
     * 	Put here everything you need to store about a Query, including the information about
     * 	the Hit, the server socket, the QUERY sender's IP and port, buffers, etc.
     */

    struct Query   *self_;
} Query;



/**********************\
|*  Global variables  *|
\**********************/

extern gboolean active; // TRUE if server is active

// Main window
extern WindowElements *main_window;


/*****************************************\
|* Functions that handle the Query list  *|
\*****************************************/

// Create a Query descriptor and put it in qlist
//Query *new_Query(const char *filename, uint16_t seq, gboolean is_ipv6 ,struct in6_addr *ipv6, struct in_addr *ipv4, u_short porto);
Query *new_Query(const char *filename, uint16_t seq, gboolean is_ipv6 ,struct in6_addr *ipv6,
		struct in_addr *ipv4, u_short porto, char* buf, int bufLen);
// Search for Query descriptor in qlist
Query *locate_in_QueryList(const char *filename, uint16_t seq);
Query *locate_in_QueryList_IP(const char *filename, uint16_t seq, gboolean is_ipv6);
// Free qlist descriptor and all pending memory
// called_from_GUI - use TRUE if called from a GUI event; FALSE otherwise (i.e. from socket, thread or timer event)
void del_Query(Query *ppt, gboolean called_from_GUI);
// Abort and free all active queries
void del_query_list(gboolean called_from_GUI);


/*******************************************************\
|* Functions to control the state of the application   *|
\*******************************************************/

// Start timer
void start_query_timer(Query *q, long int timeout);
// Stop timer
void stop_query_timer(Query *q);
// Handle the reception of a Query packet (is_ipv6, ipv6, ipv4 and port contain the sender's address)
void handle_Query(char *buf, int buflen, gboolean is_ipv6, struct in6_addr *ipv6, struct in_addr *ipv4, u_short port);
// Handle the reception of an Hit packet
void handle_Hit(char *buf, int buflen, struct in6_addr *ip, u_short port, gboolean is_ipv6);
// Handle the reception of a new connection on a server socket
gboolean handle_new_connection(int sock, struct sockaddr_in6 *cli_addr);

// Close everything
void close_all(gboolean called_from_GU);

// Button that starts and stops the application
void on_togglebuttonActive_toggled(GtkToggleButton *togglebutton, gpointer user_data);

// Callback button 'Stop': stops the selected TCP transmission and associated proxy
void on_buttonStop_clicked(GtkButton *button, gpointer user_data);

// Callback function that handles the end of the closing of the main window
gboolean on_window1_delete_event (GtkWidget * widget,
		GdkEvent * event, gpointer user_data);

#endif
