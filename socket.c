#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

/*
* host :the host addr
* client_port :the dest port num 
*
*/
int create_socket_info(const char *host, int cli_port)
{
	int sock;
	unsigned long inaddr;
	struct hostent *hp;
	struct sockaddr_in addr;
	
	memset(&addr, 0, sizeof(addr));
	inaddr = inet_addr(host);
	if (inaddr != INADDR_NONE) {
		memcpy(&addr.sin_addr, &inaddr, sizeof(inaddr));
	} else {
		hp = gethostbyname(host);
		if (hp == NULL) {
			return -1;
		}
		memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
	}
	addr.sin_port = htons(cli_port);
	addr.sin_family = AF_INET;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		return sock;
	}
	/* connect to the server */
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		return -1;
	}
    
	return sock;
}

