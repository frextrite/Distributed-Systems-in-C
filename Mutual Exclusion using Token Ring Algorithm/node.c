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

#define NR_RESOURCES 2
#define REQ_LEN sizeof(struct Request)
#define RESP_LEN sizeof(struct Response)
#define RESOURCE_LEN 64
#define RESOURCE_NAME "/tmp/resource_alpha"

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

enum MSG_TYPE {REQ, OK, RELEASE, ACK, BUSY, GRANT};
enum RETRY {DIE, DONT_DIE};

struct Request {
	int flags;
	int res;
	enum MSG_TYPE msg;
};

struct Response {
	uint64_t TOKEN;
	char path_to_resource[RESOURCE_LEN];
	enum MSG_TYPE msg;
};

int sendRequest(int sock, struct Request* req, struct sockaddr_in* addr) {
	return sendto(sock, req, REQ_LEN, 0, (struct sockaddr*)addr, sizeof(struct sockaddr));
}

int sendResponse(int sock, struct Response* resp, struct sockaddr_in* addr) {
	return sendto(sock, resp, RESP_LEN, 0, (struct sockaddr*)addr, sizeof(struct sockaddr));
}

void sendResourceRequest(int res, int sock, struct sockaddr_in* addr) {
	struct Request req;
	req.msg = REQ;
	req.res = res;
	if(sendRequest(sock, &req, addr) < 0) {
		handle_error("sendto(REQ)");
	}
}

void sendGrantResponse(int sock, struct Response *resp, struct sockaddr_in* addr) {
	if(sendResponse(sock, resp, addr) < 0) {
		handle_error("sendto(GRANT)");
	}
}

int getServerResponse(int sock, struct Response *resp) {
	return recv(sock, (struct Response*)resp, RESP_LEN, 0);
}

void waitForNodeResponse(int sock, struct Response* resp, enum RETRY retry) {
	if(getServerResponse(sock, resp) < 0) {
		if(errno == EAGAIN || errno == EWOULDBLOCK) {
			if(retry == DONT_DIE) {
				errno = -1;
				return waitForNodeResponse(sock, resp, DONT_DIE);
			}
			printf("Co-ordinator down, exiting...\n");
			exit(EXIT_FAILURE);
		}
		handle_error("recv()");
	}
}

void displayFileContents(FILE *fptr) {
	char file_buff[32];
	printf("Displaying file contents...\n");
	fscanf(fptr, "%[^\n]", file_buff);
	// char temp;
	// scanf("%c", &temp);
	printf("%s\nEOF\n", file_buff);
}

void writeFileContents(FILE *fptr) {
	char file_buff[32];
	printf("Enter updated file contents\n");
	scanf("%[^\n]", file_buff);
	char temp;
	scanf("%c", &temp);
	printf("Writing updated contents to file...\n");
	fprintf(fptr, "%s\n", file_buff);
}

void openAndReadResource(char* resource) {
	FILE *fptr;
	if((fptr = fopen(resource, "r+")) == NULL) {
		printf("File doesn't exist, let us first write to it!\n");
		return;
	}
	displayFileContents(fptr);
	fclose(fptr);
}

void openAndUpdateResource(char* resource) {
	FILE *fptr;
	if((fptr = fopen(resource, "w+")) == NULL) {
		perror("Error creating file. Exiting...\n");
		exit(EXIT_FAILURE);
	}
	writeFileContents(fptr);
	fclose(fptr);
}

void setTimeout(int sock, int duration) {
	struct timeval to;      
    to.tv_sec = duration;
    to.tv_usec = 0;
	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&to, sizeof(struct timeval)) < 0) {
		handle_error("setsockopt()");
	}
}

void enableTimeout(int sock) {
	setTimeout(sock, 5);
}

void disableTimeout(int sock) {
	setTimeout(sock, 0);
}

uint64_t generateToken() {
	return 0xEEEEEEEEEEEEEEE;
}

void initializeNodeResponse(struct Response* resp, int TOKEN, char* resource) {
	resp->TOKEN = TOKEN;
	strcpy(resp->path_to_resource, resource);
	resp->msg = GRANT;
}

void initializeResource(char *dest, char* src) {
	strcpy(dest, src);
}

void intializeNewResource(char *resource) {
	initializeResource(resource, RESOURCE_NAME);
}

int main(int argc, char **argv) {
	int initial_port = (argc > 1) ? atoi(argv[1]) : (1<<13);
	int port = (argc > 2) ? (initial_port)+atoi(argv[2]) : (initial_port);
	int successor_port = (argc > 3) ? (initial_port)+atoi(argv[3]) : (initial_port)+1;
	int initiator = (argc > 4) ? (strcmp(argv[4], "y") == 0 || strcmp(argv[4], "Y") == 0 ) : 0;

	uint64_t TOKEN;
	char resource[RESOURCE_LEN];

	if(initiator > 0) {
		TOKEN = generateToken();
		intializeNewResource(resource);
	}
	
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

	struct sockaddr_in successor_addr;
	memset(&successor_addr, 0, sizeof(struct sockaddr_in));
	successor_addr.sin_family = AF_INET;
	successor_addr.sin_port = successor_port;
	successor_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	char choice[8];
	char temp;
	struct Response node_resp;
	for(;;) {
		if(initiator > 0) {
			printf("Generating token %ld for resource %s...\n", TOKEN, resource);
			initializeNodeResponse(&node_resp, TOKEN, resource);
			initiator = 0;
		} else {
			printf("Attempting to get exclusive access...\n");

			waitForNodeResponse(sock, &node_resp, DONT_DIE);

			assert(node_resp.msg == GRANT);
			initializeResource(resource, node_resp.path_to_resource);
		}

		printf("Got exclusive access to file %s\n", resource);

		printf("Entering Critical Section\n");

		openAndReadResource(resource);

		printf("Do you wish to edit %s? (y/N)\n", resource);
		scanf("%s", choice);
		scanf("%c", &temp);
		if(strcmp(choice, "y") == 0 || strcmp(choice, "Y") == 0) {
			openAndUpdateResource(resource);
		}
		
		printf("Exiting Critical Section.\n");

		printf("Sending TOKEN to %d...\n", successor_port);

		sendGrantResponse(sock, &node_resp, &successor_addr);

		printf("Do you wish to continue? (y/N)\n");
		scanf("%s", choice);
		scanf("%c", &temp);
		if(strcmp(choice, "n") == 0 || strcmp(choice, "N") == 0) break;
	}

	for(;;) {
		waitForNodeResponse(sock, &node_resp, DONT_DIE);
		sendGrantResponse(sock, &node_resp, &successor_addr);
	}

	close(sock);
	
	return 0;
}