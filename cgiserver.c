#define _WITH_GETLINE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>

#define HEADER_SIZE 10240L

int
handle(int fd, struct sockaddr *addr, socklen_t size)
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

	/* string bullshit to get ./filename*/
	char *temp;
	temp = strstr(request, "HTTP");
	temp[-1] = '\0';
	temp = malloc(sizeof(request+1));
	snprintf(temp, sizeof(temp), ".%s", request);
	
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
	int websock, port;

	port = 8080;
	if (argc > 1)
		port = atoi(argv[1]);
	
	/* Setup Socket */
	struct sockaddr_in sin;
	websock			= 	socket(AF_INET, SOCK_STREAM, 0);
	sin.sin_family 		=	AF_INET;
	sin.sin_port		=	htons(port);
	sin.sin_addr.s_addr	=	INADDR_ANY;

	/* Set reuse for socket */
	int true = 1;
	if(setsockopt(websock, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) < 0){
		fprintf(stderr, "Failed to set socket options!\n");
		close(websock);
		return -1;
	}
	
	/* Bind */
	if(bind(websock, (struct sockaddr *)&sin, sizeof(sin)) < 0){
		fprintf(stderr, "Failed to bind to port %d!\n", port);
		return -1;
	}

	/* Start listening */
	listen(websock, 50);
	
	while(1){
		int fd;
		struct sockaddr *addr;
		socklen_t size = 0;

		fd = accept(websock, addr, &size);
		if(!fork())
			handle(fd, addr, size);
		else
			close(fd);
	}
	return 0;
}
