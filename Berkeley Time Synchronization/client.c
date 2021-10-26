#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#define CLIENT_DATA_LEN sizeof(struct ClientMessage)
#define REQ_LEN sizeof(struct ClientMessage)

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

enum MSG_TYPE {SYN, SYNACK, ACK};

int TIMESTAMP;

struct ClientMessage {
	int flags;
	int time;
	int delta;
	enum MSG_TYPE msg;
};

int sendResponse(int sock, struct ClientMessage* resp, struct sockaddr* addr) {
	return sendto(sock, resp, CLIENT_DATA_LEN, 0, addr, sizeof(struct sockaddr));
}

int sendStatusResponse(enum MSG_TYPE msg, int sock, struct sockaddr* addr) {
	struct ClientMessage resp;
	resp.msg = msg;
	return sendResponse(sock, &resp, addr);
}

int sendTimeResponse(int sock, struct sockaddr* addr) {
	struct ClientMessage resp;
	resp.time = TIMESTAMP;
	resp.msg = SYNACK;
	return sendResponse(sock, &resp, addr);
}

int waitForMessage(int sock, struct ClientMessage* resp) {
	return recv(sock, resp, sizeof(struct ClientMessage), 0);
}

int waitForTimeRequest(int sock, struct ClientMessage* resp) {
	return waitForMessage(sock, resp);
}

int waitForTimeUpdates(int sock, struct ClientMessage* resp) {
	return waitForMessage(sock, resp);
}

int sendTimeUpdates(int sock, struct sockaddr* addr, int delta) {
	struct ClientMessage resp;
	resp.msg = ACK;
	resp.delta = delta;
	return sendResponse(sock, &resp, addr);
}

void reportAck(int sock, struct sockaddr* addr) {
	if(sendStatusResponse(ACK, sock, addr) < 0) {
		handle_error("sendto(ACK)");
	}
}

int convert_to_timestamp(char* hhmmss) {
	assert(strlen(hhmmss) == 8);
	char hh[3], mm[3], ss[3];
	strncpy(hh, hhmmss, 2);
	strncpy(mm, hhmmss+3, 2);
	strncpy(ss, hhmmss+6, 2);
	int hours = atoi(hh);
	int minutes = atoi(mm);
	int seconds = atoi(ss);
	return hours*60*60+minutes*60+seconds;
}

char* to_string(int time) {
	char* strtime = (char*)malloc(sizeof(char)*2);
	sprintf(strtime, "%02d", time);
	return strtime;
}

char* convert_from_timestamp(int time) {
	char* hhmmss = (char*)malloc(sizeof(char)*8);
	int num_precision = 3;
	int time_segments[num_precision];
	for(int i = 0; i < num_precision; i++) {
		time_segments[i] = time % 60;
		time /= 60;
	}
	sprintf(hhmmss, "%02d:%02d:%02d", time_segments[2], time_segments[1], time_segments[0]);
	return hhmmss;
}

void synchronize(int delta) {
	TIMESTAMP += delta;
}

// ./client SERVER_PORT CLIENT_OFFSET hh:mm:ss
int main(int argc, char **argv) {
	int server_port = (argc > 1) ? atoi(argv[1]) : (1<<13);
	int port = (argc > 2) ? (server_port<<1)+atoi(argv[2]) : (server_port<<1);
	TIMESTAMP = (argc > 3) ? convert_to_timestamp(argv[3]) : 0;
	
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

	printf("Current client time is %s\n", convert_from_timestamp(TIMESTAMP));

	struct ClientMessage message;
	if(waitForTimeRequest(sock, &message) < 0) {
		handle_error("recv(SYN)");
	}

	assert(message.msg == SYN);

	if(sendTimeResponse(sock, (struct sockaddr*)&server_addr) < 0) {
		handle_error("sendto(SYNACK)");
	}

	if(waitForTimeUpdates(sock, &message) < 0) {
		handle_error("recv(ACK)");
	}

	assert(message.msg == ACK);

	printf("Received a delta of %d seconds\n", message.delta);

	synchronize(message.delta);

	printf("Time after synchronization is %s\n", convert_from_timestamp(TIMESTAMP));

	close(sock);

	return 0;
}
