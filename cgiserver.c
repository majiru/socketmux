#define _WITH_GETLINE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <string.h>
#include <time.h>

#define HEADER_SIZE 10240L

typedef int (*sockhandler)(int, struct sockaddr*, socklen_t);
typedef struct Server Server;

struct Server{
	int 			sock;
	sockhandler 	handler;
};

int
setupsock(int port)
{
	int sock;
	
	struct sockaddr_in sin;
	sock				= 	socket(AF_INET, SOCK_STREAM, 0);
	sin.sin_family 		=	AF_INET;
	sin.sin_port		=	htons(port);
	sin.sin_addr.s_addr	=	INADDR_ANY;

	/* Set reuse for socket */
	int true = 1;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) < 0){
		fprintf(stderr, "Failed to set socket options!\n");
		close(sock);
		return -1;
	}
	
	/* Bind */
	if(bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0){
		fprintf(stderr, "Failed to bind to port %d!\n", port);
		return -1;
	}

	/* Start listening */
	listen(sock, 50);

	return sock;
}

int
servermux(Server s[], int n)
{
	int i;
	fd_set readfds;

	FD_ZERO(&readfds);
	for(i = 0; i < n; i++)
		FD_SET(s[i].sock, &readfds);

	if(select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
		return -1;

	for(i = 0; i < n; i++)
		if(FD_ISSET(s[i].sock, &readfds))
			return i;

	return -1;
}

int
handleshell(int fd, struct sockaddr *addr, socklen_t size)
{
	if(fork())
		return 0;
	dup2(fd, 2);
	dup2(fd, 1);
	dup2(fd, 0);
	execlp("/bin/sh", NULL, NULL);
	return 0;
}

int
handlecgi(int fd, struct sockaddr *addr, socklen_t size)
{
	FILE *sock = NULL;
	char *line, *request;
	time_t clock;
	line = NULL;
	request = NULL;

	size_t linesize = 0;
	ssize_t linelen;

	sock = fdopen(fd, "r+");
	if(!sock)
		perror(NULL);
	if((linelen = getline(&line, &linesize, sock)) == -1)
		perror(NULL);
	request = malloc(linelen);
	strlcpy(request, line, linelen);
	switch(request[0]){
		case 'G':
			/* Probably GET */
			request += 4;
			break;
		case 'P':
			/* Probably POST */
			request += 5;
			break;
	}
	if(!request){
		fprintf(stdout, "Wasn't POST/GET or no file.\n");
		return 1;
	}
	fprintf(stdout, "Request: %s\n", request);
	/* string bullshit to get ./filename*/
	char *temp;
	temp = strstr(request, "HTTP");
	temp[-1] = '\0';
	temp = malloc(linelen);
	snprintf(temp, linelen, ".%s", request);
	temp[linelen-1] = '\0';
	
	clock = time(NULL);
	fprintf(stdout, "requested %s @ %s\n", temp, ctime(&clock));
	if(!fork()){
		dup2(fd, 2);
		dup2(fd, 1);
		dup2(fd, 0);
		execlp(temp, NULL, NULL);
		free(request);
		free(temp);
	}
	return 0;
}

int
main(int argc, char **argv)
{
	int webport = 8080;
	Server services[2];

	if (argc > 1)
		webport = atoi(argv[1]);

	services[0].sock 	= 	setupsock(webport);
	services[0].handler 	= 	handlecgi;
	services[1].sock	=	setupsock(8008);
	services[1].handler	=	handleshell;

	while(1){
		Server s;
		int i, fd;
		struct sockaddr *addr;
		socklen_t size = 0;

		i = servermux(services, 2);
		if(i < 0)
			continue;
		s = services[i];
		fd = accept(s.sock, addr, &size);
		if(!fork())
			s.handler(fd, addr, size);
		else
			close(fd);
	}
	return 0;
}
