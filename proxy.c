#include "csapp.h"
#include<errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<string.h>
#include<stdbool.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// Header Sizes
#define HEADER_NAME_MAX_SIZE 512
#define HEADER_VALUE_MAX_SIZE 1024
#define MAX_HEADERS 32
#define HTTP_REQUEST_MAX_SIZE 8192

#define MAXEVENTS 64
#define READ_REQUEST 1
#define SEND_REQUEST 2
#define READ_RESPONSE 3
#define SEND_RESPONSE 4

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


// void command(void);
typedef struct {
	char name[HEADER_NAME_MAX_SIZE];
	char value[HEADER_VALUE_MAX_SIZE];
} http_header;

struct request_info {
	int fd_to_client;
	int fd_to_server;
	int state;
	char clientbuf[MAX_OBJECT_SIZE];
	int numread_fromclient;
	int numwrite_toserver;
	int numwritten_toserver;
	int numread_fromserver;
	int numwritten_toclient;
	// Socket Variables
	char request_buff[HTTP_REQUEST_MAX_SIZE];

	// Http Parser Variables
	char method[8];
	char hostname[60];
	char port[6];
	char uri[60];
	http_header headers[30];
	int numHeaders;
};

int is_complete_request(struct request_info* active_event) {
	bool isValid = false;

	if (memcmp(active_event->request_buff, "GET", strlen("GET")) == 0) {
        isValid = true;
    } else if (memcmp(active_event->request_buff, "POST", strlen("POST")) == 0) {
        isValid = true;
    }

	if((memcmp(active_event->request_buff + (strlen(active_event->request_buff) - strlen("\r\n\r\n")), "\r\n\r\n", strlen("\r\n\r\n")) == 0) && isValid) {
		return 1;
	}
	else {
		return 0;
	}
}


void parse_request(struct request_info* active_event) {

	char request_copy[strlen(active_event->request_buff)];

	strcpy(request_copy, active_event->request_buff);

	char EOLdelimiter[] = "\r\n";
	char spaceDelimiter[] = " ";
	char backslashDelimiter[] = "/";
	char doubleBackslashDelimiter[] = "//";
	char portDelimiter[] = ":";
	

	char *request_line = strtok(request_copy, EOLdelimiter);  // get the first line of the request

	char *req_ptr;
	strcpy(active_event->method,strtok_r(request_line, spaceDelimiter ,&req_ptr));  // Obtains the GET from the 1st request line
	char url[128];
	strcpy(url,strtok_r(NULL, spaceDelimiter, &req_ptr));  // obtains the URL - req_ptr helps the tokenizer continue from the last call

	bool hasPort = false;
	int num_cols = 0;

	int i;
	for(i = 0; i < (strlen(url) - 1); ++i) {
		if(url[i] == ':') {
			++num_cols;
		}
	}
	char *url_ptr;
	strtok_r(url, doubleBackslashDelimiter, &url_ptr);  // move the ptr past the “http://”

	if (num_cols > 1) {
		hasPort = true;
	}

	// If there is a port
	if(hasPort) {
		url_ptr++;
		strcpy(active_event->hostname,strtok_r(NULL, portDelimiter ,&url_ptr));
		strcpy(active_event->port, strtok_r(NULL, backslashDelimiter, &url_ptr));
	}
	// Else add the default http port
	else {
		strcpy(active_event->hostname,strtok_r(NULL, backslashDelimiter ,&url_ptr));
		strcpy(active_event->port, "80");
	}

    // Make sure there is a uri
    memcpy(active_event->uri, "/", 1);

    if(*url_ptr != '\0') {
	    strcpy(active_event->uri + 1,strtok_r(NULL, EOLdelimiter ,&url_ptr));
    }

	char *req_line_ptr;
	int numHeaders = 0;

	while((request_line = strtok(NULL, EOLdelimiter))) { // get subsequent lines until it returns NULL

		strcpy(active_event->headers[numHeaders].name, strtok_r(request_line, portDelimiter, &req_line_ptr));
		req_line_ptr++;
		strcpy(active_event->headers[numHeaders].value, strtok_r(NULL, EOLdelimiter, &req_line_ptr));

		numHeaders++;
	} 
	
	active_event->numHeaders = numHeaders;
}

void connect_server(struct request_info* active_event, int efd) {

    #define BUF_SIZE 500

    // Vars for communicating with Server
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s;
	struct epoll_event event;

	/* Obtain address(es) matching host/port */

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;          /* Any protocol */

	s = getaddrinfo(active_event->hostname, active_event->port, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully connect(2).
	   If socket(2) (or connect(2)) fails, we (close the socket
	   and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,
				rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;                  /* Success */

		close(sfd);
	}

	if (rp == NULL) {               /* No address succeeded */
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);           /* No longer needed */

	// Set as non-blocking
	if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option\n");
		exit(1);
	}

	active_event->fd_to_server = sfd;
	event.data.ptr = active_event;
	event.events = EPOLLOUT | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(1);
	}
}

void makeRequest_server(struct request_info* active_event) {
	
	// Send Request to server
	sprintf(active_event->request_buff, "%s %s HTTP/1.0\r\n", active_event->method, active_event->uri);
	sprintf(active_event->request_buff, "%sHost: %s\r\n", active_event->request_buff, active_event->hostname);
	sprintf(active_event->request_buff, "%sUser-Agent: %s", active_event->request_buff, user_agent_hdr);
	sprintf(active_event->request_buff, "%sConnection: close\r\n", active_event->request_buff);
	sprintf(active_event->request_buff, "%sProxy-Connection: close\r\n", active_event->request_buff);

    int header_index;
    for(header_index = 0; header_index < active_event->numHeaders; ++header_index) {
        if((strcmp(active_event->headers[header_index].name, "Host") == 0) ||
        ((strcmp(active_event->headers[header_index].name, "User-Agent") == 0)) ||
        ((strcmp(active_event->headers[header_index].name, "Connection") == 0)) ||
        ((strcmp(active_event->headers[header_index].name, "Proxy-Connection") == 0))) {
            //skip these
        } else {
            sprintf(active_event->request_buff, "%s%s: %s\r\n", active_event->request_buff, active_event->headers[header_index].name, active_event->headers[header_index].value);
        }
    }
    active_event->numwrite_toserver = sprintf(active_event->request_buff, "%s\r\n", active_event->request_buff);
}

void readAndParse_client(struct request_info* active_event, int efd) {
    int nread = 0;

    for (;;) {
        // Get Request from client
        nread = recv(active_event->fd_to_client, &active_event->request_buff[active_event->numread_fromclient], HTTP_REQUEST_MAX_SIZE, 0);
		if (nread < 0) { 
			if (!(errno == EWOULDBLOCK || errno == EAGAIN)) {  // Non-standard error, so close and free. 
				perror("Error reading from socket");
				close(active_event->fd_to_client);
				free(active_event);
			}
			else {
				//Has more to read so don't change state
			}
			break;
		} else if (!strstr(active_event->request_buff, "\r\n\r\n")) { 
			active_event->numread_fromclient += nread;
		} else {
			if (is_complete_request(active_event)) {  // Ready to continue
				parse_request(active_event);
				connect_server(active_event, efd);
				makeRequest_server(active_event);
				active_event->state = SEND_REQUEST;
			}
			else {
				printf("request is incomplete\n");
			}
			break;
		}
    }				
}

void writeRequest_server(struct request_info* active_event, int efd) {
	size_t wbytes = 0;
	struct epoll_event event;
	
	while(1) {
		wbytes = write(active_event->fd_to_server, active_event->request_buff, active_event->numwrite_toserver);
		if(wbytes < 0) {
			if (!(errno == EWOULDBLOCK || errno == EAGAIN)) {  // Non-standard error, so close and free. 
				perror("Error writing to server");
				close(active_event->fd_to_client);
				close(active_event->fd_to_server);
				free(active_event);
			}
			else {
				//Has more to read so don't change state
			}
			break;
		}
		else { // positive bytes or zero written
			active_event->numwritten_toserver += wbytes;
			if(active_event->numwritten_toserver == active_event->numwrite_toserver) {
				// Done writing
				//register the socket with the epoll instance for reading
				event.data.ptr = active_event;
				event.events = EPOLLIN | EPOLLET;
				if (epoll_ctl(efd, EPOLL_CTL_MOD, active_event->fd_to_server, &event) < 0) {
					fprintf(stderr, "error adding event\n");
					exit(1);
				}
				active_event->state = READ_RESPONSE;
				break;
			}
			else { 
				// Short Write, continue to loop
			}
		}
	}
}

void readResponse_server(struct request_info* active_event, int efd) {
	int nread = 0;
	struct epoll_event event;

	for (;;) {
        // Get Request from client
        nread = recv(active_event->fd_to_server, &active_event->request_buff[active_event->numread_fromserver], HTTP_REQUEST_MAX_SIZE, 0);

		if(nread == 0) { // Read Completed
			//register client socket for writing
			event.data.ptr = active_event;
			event.events = EPOLLOUT | EPOLLET;
			if (epoll_ctl(efd, EPOLL_CTL_MOD, active_event->fd_to_client, &event) < 0) {
				fprintf(stderr, "error adding event\n");
				exit(1);
			}
			active_event->state = SEND_RESPONSE;
			close(active_event->fd_to_server);
			break;
		} else if (nread < 0) {
			if (!(errno == EWOULDBLOCK || errno == EAGAIN)) {  // Non-standard error, so close and free. 
				perror("Error reading from socket");
				close(active_event->fd_to_client);
				free(active_event);
			}
			else {
				//Has more to read so don't change state
			}
			break;
		} else { //Short read, so continue loop
			active_event->numread_fromserver += nread;
		}
	}
}

void sendResponse_client(struct request_info* active_event, int efd) {
	int wbytes = 0;

	while(1) {
		wbytes = write(active_event->fd_to_client, active_event->request_buff, active_event->numread_fromserver);
		if(wbytes < 0) {
			if (!(errno == EWOULDBLOCK || errno == EAGAIN)) {  // Non-standard error, so close and free. 
				perror("Error writing to client");
				close(active_event->fd_to_client);
				close(active_event->fd_to_server);
				free(active_event);
			}
			else {
				//Has more to read so don't change state
			}
			break;
		}
		else { // positive bytes or zero written
			active_event->numwritten_toclient += wbytes;
			if(active_event->numwritten_toclient == active_event->numread_fromserver) {
				// Done writing
				// close client socket
				close(active_event->fd_to_client);
				break;
			}
			else { 
				// Short Write, continue to loop
			}
		}
	}
}


int main(int argc, char **argv) 
{
	int listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	int efd;
	struct epoll_event event;
	struct epoll_event *events;
	int i;

	struct request_info listen_event;
	struct request_info *new_client;
	struct request_info *active_event;

	size_t n; 

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	listenfd = Open_listenfd(argv[1]);

	if ((efd = epoll_create1(0)) < 0) {
		fprintf(stderr, "error creating epoll fd\n");
		exit(1);
	}

	listen_event.fd_to_client = listenfd;
	if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option\n");
		exit(1);
	}

	event.data.ptr = &listen_event;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(1);
	}

	/* Buffer where events are returned */
	events = calloc(MAXEVENTS, sizeof(event));

	while (1) {
		// wait for event to happen 
		n = epoll_wait(efd, events, MAXEVENTS, 1000);

		for (i = 0; i < n; i++) {
			active_event = (struct request_info *)(events[i].data.ptr);

			if ((events[i].events & EPOLLERR) ||
					(events[i].events & EPOLLHUP) ||
					(events[i].events & EPOLLRDHUP)) {
				/* An error has occured on this fd */
				close(active_event->fd_to_client);
				close(active_event->fd_to_server);
				free(active_event);
				continue;
			}

			if (listenfd == active_event->fd_to_client) { //line:conc:select:listenfdready
				clientlen = sizeof(struct sockaddr_storage); 
				while (1) {
					connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
					if (connfd < 0) {
						break;
					}
					// Set as non-blocking
					if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
						fprintf(stderr, "error setting socket option\n");
						exit(1);
					}

					// add event to epoll file descriptor
					new_client = (struct request_info *)malloc(sizeof(struct request_info));
					new_client->fd_to_client = connfd;
					new_client->state = READ_REQUEST;
					new_client->numread_fromclient = 0;
					new_client->numwrite_toserver = 0;
					new_client->numwritten_toserver = 0;
					new_client->numread_fromserver = 0;
					new_client->numwritten_toclient = 0;


					event.data.ptr = new_client;
					event.events = EPOLLIN | EPOLLET;
					if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
						fprintf(stderr, "error adding event\n");
						exit(1);
					}
				}
			} else { //line:conc:select:listenfdready
				if(active_event->state == READ_REQUEST) {
					readAndParse_client(active_event, efd);
				}
				else if(active_event->state == SEND_REQUEST) {
					writeRequest_server(active_event, efd);
				}
				else if(active_event->state == READ_RESPONSE) {
					readResponse_server(active_event, efd);

				}
				else if(active_event->state == SEND_RESPONSE) {
					sendResponse_client(active_event, efd);
				}
			}
		}
	}
	free(events);
}
