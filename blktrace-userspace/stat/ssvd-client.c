#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "ssvd-control.h"

int main(int argc, char **argv)
{
	int len;
	int	sockfd;

	struct sockaddr_in servaddr;
	struct in_addr **pptr;
	struct hostent *hp;
	char charname[SERVERBUFLEN];

	int serverport = SERVERPORT;

	if (argc < 3)
		return 0;

	serverport = atoi(argv[1]);


	if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("cannot create socket");
		return 0;
	}

	if (!(hp = gethostbyname("localhost"))) {
		printf("cannot resolve hostname\n");
		return 0;
	}

	pptr = (struct in_addr **) hp->h_addr_list;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(serverport);
	memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));

	if (!inet_ntop(AF_INET, &servaddr.sin_addr, charname, sizeof(charname))) {
		return 0;	
	} 
	printf("connect to %s\n", charname);

	if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		printf("cannot connect to server\n");
		close(sockfd);
		return 0;
	}

	len = write(sockfd, argv[2], strlen(argv[2]));
	printf("%s sent [%d bytes]\n", argv[2], len);
	close(sockfd);
	return 0;
}