#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[])
{
	int port = (argc > 1) ? atoi(argv[1]) : 6000;
	int sockfd;
	socklen_t clilen;
	struct sockaddr_in cli_addr, serv_addr;

	int i;
	char buf[4096];

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("Cannot create socket\n");
		exit(0);
	}

	int opt = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	serv_addr.sin_family      = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port        = htons(port);

	if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Cannot bind\n");
		exit(0);
	}

	listen(sockfd, 5);
	printf("Test server listening on port %d...\n", port);

	while (1) {
		clilen = sizeof(cli_addr);
		int newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);

		for (i = 0; i < 4096; i++)
			buf[i] = '\0';

		recv(newsockfd, buf, 4095, 0);
		printf("%s\n", buf);

		const char* msg =
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 19\r\n"
			"Connection: close\r\n"
			"\r\n"
			"Message from server";
		send(newsockfd, msg, strlen(msg), 0);

		close(newsockfd);
	}

	close(sockfd);
	return 0;
}
