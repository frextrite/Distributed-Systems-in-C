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

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

enum MSG_TYPE {REQ, OK, RELEASE, ACK, BUSY};
enum RETRY {DIE, DONT_DIE};

struct Request {
	int flags;
	int res;
	enum MSG_TYPE msg;
};

struct Response {
	int flags;
	char path_to_resource[RESOURCE_LEN];
	enum MSG_TYPE msg;
};

int sendRequest(int sock, struct Request* req, struct sockaddr_in* addr) {
	return sendto(sock, req, REQ_LEN, 0, (struct sockaddr*)addr, sizeof(struct sockaddr));
}

void sendResourceRequest(int res, int sock, struct sockaddr_in* addr) {
	struct Request req;
	req.msg = REQ;
	req.res = res;
	if(sendRequest(sock, &req, addr) < 0) {
		handle_error("sendto(REQ)");
	}
}

void sendReleaseRequest(int res, int sock, struct sockaddr_in* addr) {
	struct Request req;
	req.msg = RELEASE;
	req.res = res;
	if(sendRequest(sock, &req, addr) < 0) {
		handle_error("sendto(RELEASE)");
	}
}

int getServerResponse(int sock, struct Response *resp) {
	return recv(sock, (struct Response*)resp, RESP_LEN, 0);
}

void waitForServerResponse(int sock, struct Response* resp, enum RETRY retry) {
	if(getServerResponse(sock, resp) < 0) {
		if(errno == EAGAIN || errno == EWOULDBLOCK) {
			if(retry == DONT_DIE) {
				errno = -1;
				return waitForServerResponse(sock, resp, DONT_DIE);
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
	char temp;
	scanf("%c", &temp);
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

int main(int argc, char **argv) {
	int server_port = (argc > 1) ? atoi(argv[1]) : (1<<13);
	int port = (argc > 2) ? (server_port<<1)+atoi(argv[2]) : (server_port<<1);
	
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

	enableTimeout(sock);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr_in));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = server_port;
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	char choice[8];
	char temp;
	for(;;) {
		int res;
		printf("Which resource would you like to work with? (1/2)\n");
		scanf("%d", &res);

		printf("Attempting to get exclusive access on %d...\n", res);

		struct Response server_resp;
		sendResourceRequest(res, sock, &server_addr);
		waitForServerResponse(sock, &server_resp, DIE);

		if(server_resp.msg == BUSY) {
			printf("Server reported resource busy. Waiting for follow-up response...\n");
			waitForServerResponse(sock, &server_resp, DONT_DIE);
		}

		assert(server_resp.msg == OK);
		char* resource = server_resp.path_to_resource;

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

		printf("Attempting to release resource %s\n", resource);

		sendReleaseRequest(res, sock, &server_addr);
		waitForServerResponse(sock, &server_resp, DIE);

		assert(server_resp.msg == ACK);
		printf("Successfully released resource %d\n", res);

		printf("Do you wish to continue? (y/N)\n");
		scanf("%s", choice);
		scanf("%c", &temp);
		if(strcmp(choice, "n") == 0 || strcmp(choice, "N") == 0) break;
	}

	close(sock);
	
	return 0;
}