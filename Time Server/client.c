#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define server_port (2<<13)
#define MSG_SIZE 64

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

int main(int argc, char **argv) {
	int port = (argc > 1) ? (server_port<<1)+atoi(argv[1]) : (server_port<<1);
	
	int sock;
	if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		handle_error("socket()");
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = port;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if(bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0) {
		handle_error("bind()");
	}

	printf("Started client on port %d\n", port);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr_in));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = server_port;
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	int i_want_time = 1;

	if(sendto(sock, (int*)&i_want_time, sizeof(int), 0, (struct sockaddr*)&server_addr, sizeof(struct sockaddr)) < 0) {
		handle_error("sendto()");
	}

	char *current_time = malloc(sizeof(MSG_SIZE));
	if(recv(sock, (char *)current_time, MSG_SIZE, 0) < 0) {
		handle_error("recv()");
	}

	if(i_want_time) {
		printf("Current time is %s\n", current_time);
	}

	close(sock);

	return 0;
}
