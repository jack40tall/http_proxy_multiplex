#include "csapp.h"
#include<errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<string.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAXEVENTS 64

void command(void);

struct client_info {
	int fd;
	char desc[100];
};

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int main(int argc, char **argv) 
{
	int listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	int efd;
	struct epoll_event event;
	struct epoll_event *events;
	int i;
	int len;

	struct client_info listen_event;
	struct client_info stdin_event;
	struct client_info *new_client;
	struct client_info *active_event;

	size_t n; 
	char buf[MAXLINE]; 

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	listenfd = Open_listenfd(argv[1]);

	if ((efd = epoll_create1(0)) < 0) {
		fprintf(stderr, "error creating epoll fd\n");
		exit(1);
	}

	listen_event.fd = listenfd;
	sprintf(listen_event.desc, "Listen file descriptor");
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

	stdin_event.fd = 0;
	sprintf(stdin_event.desc, "stdin file descriptor");

	event.data.ptr = &stdin_event;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, 0, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(1);
	}

	/* Buffer where events are returned */
	events = calloc(MAXEVENTS, sizeof(event));

	while (1) {
		// wait for event to happen (no timeout)
		n = epoll_wait(efd, events, MAXEVENTS, -1);

		for (i = 0; i < n; i++) {
			active_event = (struct client_info *)(events[i].data.ptr);

			printf("new activity from %s\n", active_event->desc);
			if ((events[i].events & EPOLLERR) ||
					(events[i].events & EPOLLHUP) ||
					(events[i].events & EPOLLRDHUP)) {
				/* An error has occured on this fd */
				fprintf (stderr, "epoll error on %s\n", active_event->desc);
				close(active_event->fd);
				free(active_event);
				continue;
			}

			if (0 == active_event->fd) { //line:conc:select:listenfdready
				len = read(0, buf, MAXLINE);
				buf[len] = 0;
				printf("received the following: %s\n", buf);
			} 
			else if (listenfd == active_event->fd) { //line:conc:select:listenfdready
				clientlen = sizeof(struct sockaddr_storage); 
				while (1) {
					connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
					if (connfd < 0) {
						break;
					}

					if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
						fprintf(stderr, "error setting socket option\n");
						exit(1);
					}

					// add event to epoll file descriptor
					new_client = (struct client_info *)malloc(sizeof(struct client_info));
					new_client->fd = connfd;
					sprintf(new_client->desc, "client with file descriptor %d", connfd);

					event.data.ptr = new_client;
					event.events = EPOLLIN | EPOLLET;
					if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
						fprintf(stderr, "error adding event\n");
						exit(1);
					}
				}
			} else { //line:conc:select:listenfdready
				while (1) {
					len = recv(active_event->fd, buf, MAXLINE, 0);   
					if (len == 0) { // EOF received
						// closing the fd will automatically
						// unregister the fd from the efd
						close(active_event->fd);
						free(active_event);
						break;
					} else if (len < 0) {
						if (!(errno == EWOULDBLOCK || errno == EAGAIN)) {
							perror("Error reading from socket");
							close(active_event->fd);
							free(active_event);
						}
						break;
					} else {
						printf("Received %d bytes\n", len);
						send(active_event->fd, buf, len, 0);
					}
				}
			}
		}
	}
	free(events);
}
