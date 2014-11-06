#include <pthread.h>


#include "ssvd-stat.h"
#include "ssvd-control.h"

#ifndef KVM
#include "xen-dummy.h"
#else
#include "dummy.h"
#endif

struct ssvdserver_state {
	int closed;
	int listenfd;
	pthread_t thread_listen;
} ssvdserver_state;

char server_buf[SERVERBUFLEN];

static void *ssvd_server_listen(void *arg);

static ssize_t socket_readn(int sockfd, char* buf, size_t n){
	size_t nleft;
	ssize_t nread;
	char *ptr;

	ptr = buf;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(sockfd, ptr, nleft)) < 0) {
			if (errno == EINTR)
				nread = 0;
			else
				return -1;
		} else if (nread == 0) break;

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);
}
int ssvd_server_init(void);//exported in several headers
int ssvd_server_init(void) {
	int ret;

	int listenfd;

	struct sockaddr_in serversockaddr;

	//	int serverport = SERVERPORT;
	int listenqsize = LISTENQUEUESIZE;
	socklen_t len = sizeof(serversockaddr);

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		DPRINTF("block-ssvd: ssvd-server: socket error");
		return listenfd;
	}

	bzero(&serversockaddr, sizeof(serversockaddr));
	serversockaddr.sin_family = AF_INET;
	serversockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if ((ret = bind(listenfd, (struct sockaddr *) &serversockaddr, sizeof(serversockaddr))) < 0) {
		DPRINTF("block-ssvd: ssvd-server: bind error\n");
		return ret;
	}

	if ((ret = listen(listenfd, listenqsize)) < 0) {
		DPRINTF("block-ssvd: ssvd-server: listen error\n");
		return ret;
	}

	if (getsockname(listenfd, (struct sockaddr *) &serversockaddr, &len) == -1)
		perror("block-ssvd: ssvd-server: getsockname");
	else
		DPRINTF("block-ssvd: ssvd-server: listen at %d\n", ntohs(serversockaddr.sin_port));

	ssvdserver_state.closed = 0;
	ssvdserver_state.listenfd = listenfd;
	ssvdserver_state.thread_listen = 0;

	if ((ret = pthread_create(&(ssvdserver_state.thread_listen), NULL, ssvd_server_listen, NULL)) != 0) {
		DPRINTF("block-ssvd: ssvd_server_init: error pthread_create\n");
		return ret;
	}
	return 0;
}

static void *ssvd_server_listen(void *arg) {
	int i;
	int nready;
	int len;
	
	int newfd, sockfd;

	struct timeval tv;
	fd_set rset, allset;

 	struct sockaddr_in clientsockaddr;
 	socklen_t clientsocklen;

	int clientfds[FD_SETSIZE];

	int maxfd = ssvdserver_state.listenfd + 1;
	int maxi = -1;

	for (i = 0; i < FD_SETSIZE; i++) {
		clientfds[i] = -1;
	}
	FD_ZERO(&allset);
	FD_SET(ssvdserver_state.listenfd, &allset);

	tv.tv_sec = 0;
	tv.tv_usec = 10000;

	for (;;) {
		if (ssvdserver_state.closed) {
			return NULL;
		}

		rset = allset;
		nready = select(maxfd + 1, &rset, NULL, NULL, &tv);

		if (FD_ISSET(ssvdserver_state.listenfd, &rset)) {
			DPRINTF("block-ssvd: ssvd-server: incoming connection\n");
			clientsocklen = sizeof(clientsockaddr);
			newfd = accept(ssvdserver_state.listenfd, (struct sockaddr *) &clientsockaddr, &clientsocklen);
			
			if (newfd >= 0) {
				for (i = 0; i < FD_SETSIZE; i++) {
					if (clientfds[i] < 0) {
						clientfds[i] = newfd;
						break;
					}
				}

				if (i == FD_SETSIZE) {
					DPRINTF("block-ssvd: ssvd-server: too many clients\n");
				} else {
					FD_SET(newfd, &allset);
					if (newfd > maxfd) maxfd = newfd;
					if (i > maxi) maxi = i;
				}

				if (--nready <= 0)
					continue;
			}			
		}

		for (i = 0; i <= maxi; i++) {
			if ((sockfd = clientfds[i]) < 0) continue;

			if (FD_ISSET(sockfd, &rset)) {
				len = socket_readn(sockfd, server_buf, SERVERBUFLEN);

				if (len == 0) {
					close(sockfd);
					FD_CLR(sockfd, &allset);
					clientfds[i] = -1;
				} else {
					server_buf[len] = '\0';
					DPRINTF("block-ssvd: ssvd-server: [%s] received\n", server_buf);

					if (strcmp(server_buf, "disable") == 0) {
						DPRINTF("block-ssvd: ssvd-server: disable logging\n");
						enable_log = 0;
					} else if (strcmp(server_buf, "enable") == 0) {
						DPRINTF("block-ssvd: ssvd-server: enable logging\n");
						enable_log = 1;
					} else if (strcmp(server_buf, "clear") == 0) {
						DPRINTF("block-ssvd: ssvd-server: clear log file\n");
						enable_log = 0;
						clear_log = 1;
					}
				}

				if (--nready <= 0) break;
			}
		}
	}
}

int ssvd_server_close(void);//exported in several headers
int ssvd_server_close(void) {
	int ret;
	ssvdserver_state.closed = 1;

	if (ssvdserver_state.thread_listen && ((ret = pthread_join(ssvdserver_state.thread_listen, NULL)) != 0)) {
		DPRINTF("block-ssvd: ssvd-server: pthread_join error\n");
		return ret;
	}

	return 0;
}
