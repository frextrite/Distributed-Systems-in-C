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

#define PORT ((1<<13)+5)
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

int sendTimeRequest(int sock, struct sockaddr* addr) {
	return sendStatusResponse(SYN, sock, addr);
}

int getTimeResponse(int sock, struct ClientMessage* resp) {
	return recv(sock, resp, sizeof(struct ClientMessage), 0);
}

int sendTimeUpdates(int sock, struct sockaddr* addr, int delta) {
	struct ClientMessage resp;
	resp.msg = ACK;
	resp.delta = delta;
	return sendResponse(sock, &resp, addr);
}

void reportAck(int sock, struct sockaddr* addr) {
	if(sendStatusResponse(ACK, sock, addr) < 0) {
		handle_error("sendto(ACK");
	}
}

void handleClientRequest(int sock, struct ClientMessage* client_req, struct sockaddr* addr) {
	enum MSG_TYPE msg = client_req->msg;
	printf("%d\n", msg);
	int time = client_req->time;
	int flags = client_req->flags;
	int client_port = ((struct sockaddr_in*)addr)->sin_port;
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

int* collect_client_timestamps(int sock, int num_clients, struct sockaddr** client_addrs) {
	struct ClientMessage client_resp;
	int* client_timestamps = (int*)malloc(sizeof(int)*num_clients);

	for(int i = 0; i < num_clients; i++) {
		printf("Sending SYN request to client %d\n", ((struct sockaddr_in*)client_addrs[i])->sin_port);

		if(sendTimeRequest(sock, client_addrs[i]) < 0) {
			handle_error("sendto(SYN)");
		}

		if(getTimeResponse(sock, &client_resp) < 0) {
			handle_error("recv(SYNACK)");
		}

		assert(client_resp.msg == SYNACK);

		client_timestamps[i] = client_resp.time;

		printf("Received SYNACK response from client %d having time %s\n",
				((struct sockaddr_in*)client_addrs[i])->sin_port, convert_from_timestamp(client_resp.time));
	}

	return client_timestamps;
}

int synchronize_time(int* client_timestamps, int num_clients) {
	int time_sum = TIMESTAMP;
	for(int i = 0; i < num_clients; i++) {
		time_sum += client_timestamps[i];
	}
	return time_sum / (num_clients + 1);
}

int calculate_delta(int synchronized_time, int timestamp) {
	return synchronized_time - timestamp;
}

int* calculate_client_deltas(int synchronized_time, int* client_timestamps, int num_clients) {
	int* client_deltas = (int*)malloc(sizeof(int)*num_clients);
	for(int i = 0; i < num_clients; i++) {
		client_deltas[i] = calculate_delta(synchronized_time, client_timestamps[i]);
	}
	return client_deltas;
}

int send_deltas(int sock, int num_clients, struct sockaddr** client_addrs, int* client_deltas) {
	for(int i = 0; i < num_clients; i++) {
		if(sendTimeUpdates(sock, client_addrs[i], client_deltas[i]) < 0) {
			handle_error("sendto(ACK)");
		}
	}
	return 0;
}

struct sockaddr_in* get_new_sockaddr() {
	struct sockaddr_in* addr = (struct sockaddr_in*)malloc(sizeof(struct sockaddr));
	socklen_t addrlen = sizeof(struct sockaddr);
	memset(addr, 0, sizeof(struct sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return addr;
}

struct sockaddr_in* create_sockaddr(int port) {
	struct sockaddr_in* addr = get_new_sockaddr();
	addr->sin_port = port;
	return addr;
}

// ./server hh:mm:ss p1 p2 p3 p4 ... pn
int main(int argc, char **argv) {
	TIMESTAMP = argc > 1 ? convert_to_timestamp(argv[1]) : 0;

    int num_clients = (argc > 2) ? argc - 2 : 0;
	struct sockaddr* client_addrs[num_clients];
    for(int i = 0; i < num_clients; i++) {
        int port = atoi(argv[i+2]);
		client_addrs[i] = (struct sockaddr*)create_sockaddr(port);
    }

	int sock;
	if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		handle_error("socket()");
	}

	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(struct sockaddr);
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = PORT;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	printf("Attempting to start daemon on port %d\n", PORT);

	if(bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0) {
		handle_error("bind()");
	}

	printf("Server time is %s\n", convert_from_timestamp(TIMESTAMP));

	struct ClientMessage client_resp;
	do {
		int* client_timestamps = collect_client_timestamps(sock, num_clients, client_addrs);

		int synchronized_time = synchronize_time(client_timestamps, num_clients);

		printf("Synchronized time is %s\n", convert_from_timestamp(synchronized_time));

		int* client_deltas = calculate_client_deltas(synchronized_time, client_timestamps, num_clients);
		
		printf("Sending calculated deltas to clients...\n");

		if(send_deltas(sock, num_clients, client_addrs, client_deltas)) {
			exit(EXIT_FAILURE);
		}

		TIMESTAMP += calculate_delta(synchronized_time, TIMESTAMP);

		printf("Updated server time is %s\n", convert_from_timestamp(TIMESTAMP));
	} while(0);

	close(sock);
	
	return 0;
}