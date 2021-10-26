#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define port (2<<13)
#define MSG_SIZE 64

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

char* get_current_time() {
	time_t t = time(NULL);
	struct tm *local = localtime(&t);
	char *ftime = (char *)malloc(sizeof(MSG_SIZE));
	strftime(ftime, MSG_SIZE, "%c", local);
	return ftime;
}

int main() {
	int sock;
	if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		handle_error("socket()");
	}

	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(struct sockaddr);
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = port;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	printf("Attempting to start server on port %d\n", port);

	if(bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0) {
		handle_error("bind()");
	}

	printf("Listening on port %d...\n", port);

	struct sockaddr client_addr;
	int buf;
	for(;;) {
		if(recvfrom(sock, &buf, sizeof(int), 0, &client_addr, &addrlen) < 0) {
			handle_error("recvfrom()");
		}

		char *current_time = get_current_time();
		printf("Client %d requesting for current updates. Sending...\n", ((struct sockaddr_in*)&client_addr)->sin_port);
		if(buf == 1 && sendto(sock, current_time, MSG_SIZE, 0, &client_addr, sizeof(struct sockaddr)) < 0) {
			handle_error("sendto()");
		}

		free(current_time);
	}

	return 0;
}
