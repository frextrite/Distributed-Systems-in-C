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
#include <time.h>

#define NR_RESOURCES 1
#define REQ_LEN sizeof(struct Request)
#define RESP_LEN sizeof(struct Response)
#define DATA_LEN 16
#define DATASTORE_LEN 64
#define FILE_LEN 16

#define NO_OWNER (1<<2)

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

enum MSG_TYPE {REQ, OK, RELEASE, ACK, BUSY, SYN, SYNACK};
enum RETRY {DIE, DONT_DIE};

char path_to_datastore[DATASTORE_LEN];

struct DataStore {
	int likes;
} DATA;

struct Request {
	int flags;
	int res;
	enum MSG_TYPE msg;
};

struct Response {
	int flags;
	struct sockaddr owner;
	struct DataStore data;
	enum MSG_TYPE msg;
};

int deserializeDatastore(char *data) {
	return atoi(data);
}

char* serializeDatastore(struct DataStore *datastore) {
	char* data = (char *)malloc(sizeof(char) * DATA_LEN);
	sprintf(data, "%d", datastore->likes);
	return data;
}

void createLocalReplica() {
	srand(time(NULL));
	char path[DATASTORE_LEN] = "/tmp/";
	int offset = 5;
	for(int i = 0; i < FILE_LEN; i++) {
		int alphabet = rand() % 26;
		path[i + offset] = (char) (alphabet + 'a');
	}
	strcpy(path_to_datastore, path);
}

int sendRequest(int sock, struct Request* req, struct sockaddr_in* addr) {
	return sendto(sock, req, REQ_LEN, 0, (struct sockaddr*)addr, sizeof(struct sockaddr));
}

int sendResponse(int sock, struct Response* resp, struct sockaddr_in* addr) {
	return sendto(sock, resp, RESP_LEN, 0, (struct sockaddr*)addr, sizeof(struct sockaddr));
}

void sendSynchronizationRequest(int res, int sock, struct sockaddr_in* addr) {
	struct Request req;
	req.msg = SYN;
	req.res = res;
	if(sendRequest(sock, &req, addr) < 0) {
		handle_error("sendto(SYN)");
	}
}

void sendSynchronizationResponse(int res, int sock, struct sockaddr_in* addr) {
	struct Response resp;
	resp.msg = SYNACK;
	memcpy(&resp.data, &DATA, sizeof(struct DataStore));
	if(sendResponse(sock, &resp, addr) < 0) {
		handle_error("sendto(SYNACK)");
	}
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

int getResponse(int sock, struct Response* resp, struct sockaddr* addr) {
	socklen_t addrlen = sizeof(struct sockaddr);
	return recvfrom(sock, (struct Response*)resp, RESP_LEN, 0, addr, &addrlen);
}

int getRequest(int sock, struct Request* req, struct sockaddr* addr) {
	socklen_t addrlen = sizeof(struct sockaddr);
	return recvfrom(sock, (struct Request*)req, RESP_LEN, 0, addr, &addrlen);
}

void waitForResponse(int sock, struct Response* resp, struct sockaddr* addr) {
	if(getResponse(sock, resp, addr) < 0) {
		handle_error("waitForResponse()");
		exit(EXIT_FAILURE);
	}
}

void waitForRequest(int sock, struct Request* req, struct sockaddr* addr) {
	if(getRequest(sock, req, addr) < 0) {
		handle_error("waitForRequest()");
		exit(EXIT_FAILURE);
	}
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

void reclaimMemory(char *ptr) {
	free(ptr);
}

char* getFileContents(FILE *fptr) {
	char* file_buff = (char *)malloc(sizeof(char) * 32);
	fscanf(fptr, "%[^\n]", file_buff);
	return file_buff;
}

void displayFileContents(FILE *fptr) {
	printf("Displaying file contents...\n");
	char* file_buff = getFileContents(fptr);
	printf("%s\nEOF\n", file_buff);
	reclaimMemory(file_buff);
}

FILE* openForRead(char* resource) {
	FILE *fptr;
	if((fptr = fopen(resource, "r")) == NULL) {
		perror("File doesn't exist, let us first write to it!\n");
		exit(EXIT_FAILURE);
	}
	return fptr;
}

FILE* openToWrite(char* resource) {
	FILE *fptr;
	if((fptr = fopen(resource, "w+")) == NULL) {
		perror("Error creating file. Exiting...\n");
		exit(EXIT_FAILURE);
	}
	return fptr;
}

char* readFromReplica(char* resource) {
	FILE *fptr = openForRead(resource);
	char *file_buff = getFileContents(fptr);
	fclose(fptr);
	return file_buff;
}

void writeFileContents(FILE *fptr, char* file_buff) {
	if(file_buff == NULL) {
		printf("Enter updated file contents\n");
		char temp;
		file_buff = (char *)malloc(sizeof(char) * 32);
		scanf("%[^\n]", file_buff);
		scanf("%c", &temp);
	}
	// printf("Writing updated contents to file...\n");
	fprintf(fptr, "%s\n", file_buff);
}

void synchronizeDatastore(char *resource) {
	char* file_buff = readFromReplica(resource);
	DATA.likes = deserializeDatastore(file_buff);
	reclaimMemory(file_buff);
}

void writeToReplica(char* resource, char* file_buff) {
	FILE *fptr = openToWrite(resource);
	writeFileContents(fptr, file_buff);
	fclose(fptr);
}

void synchronizeReplica(char* resource, char* file_buff) {
	writeToReplica(resource, file_buff);
	synchronizeDatastore(resource);
}

char* requestUpdates(int res, int sock, struct sockaddr* owner) {
	sendSynchronizationRequest(res, sock, (struct sockaddr_in*)owner);
	struct Response response;
	struct sockaddr addr;
	waitForServerResponse(sock, &response, DONT_DIE);
	assert(response.msg == SYNACK);
	return serializeDatastore(&response.data);
}

void updateLocalReplica(char* resource) {
	FILE *fptr = openToWrite(resource);
	char* file_buff = serializeDatastore(&DATA);
	writeFileContents(fptr, file_buff);
	reclaimMemory(file_buff);
	fclose(fptr);
}

void openAndReadResource(char* resource) {
	FILE *fptr = openForRead(resource);
	displayFileContents(fptr);
	fclose(fptr);
}

void openAndUpdateResource(char* resource) {
	FILE *fptr = openToWrite(resource);
	writeFileContents(fptr, NULL);
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

void updateLikes(int delta) {
	DATA.likes += delta;
}

int getLikes() {
	return DATA.likes;
}

int main(int argc, char **argv) {
	int server_port = (argc > 1) ? atoi(argv[1]) : (1<<13);
	int port = (argc > 2) ? (server_port<<1)+atoi(argv[2]) : (server_port<<1);

	createLocalReplica();
	printf("Initialized local replica: %s\n", path_to_datastore);
	
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

	// enableTimeout(sock);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr_in));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = server_port;
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	char choice[8];
	struct Response server_resp;
	struct Request client_syn_req;
	struct sockaddr resp_addr;
	char* resource = path_to_datastore;

	int res = 1;
	int delta = 0;

	for(;;) {
		printf("Attempting to get exclusive access on %d...\n", res);

		sendResourceRequest(res, sock, &server_addr);
		waitForServerResponse(sock, &server_resp, DONT_DIE);

		if(server_resp.msg == BUSY) {
			printf("Server reported resource busy. Waiting for follow-up response...\n");
			waitForServerResponse(sock, &server_resp, DONT_DIE);
		}

		assert(server_resp.msg == OK);
		printf("Got exclusive access to file\n");

		printf("Entering Critical Section\n");

		struct sockaddr* previous_owner = &server_resp.owner;
		if(!(server_resp.flags & NO_OWNER)) {
			printf("Starting synchronization...\n");

			printf("Local replicated likes count: %d\n", getLikes());

			printf("Sending SYN request to previous owner %d\n", ((struct sockaddr_in *)previous_owner)->sin_port);
					
			char* new_data = requestUpdates(res, sock, previous_owner);
			synchronizeReplica(resource, new_data);

			printf("Synchronization successful...\n");

			printf("Likes after Synchronization: %d\n", getLikes());
		}

		printf("Enter delta increase in likes\n");
		scanf("%d", &delta);

		printf("Adding %d to total likes and updating local replica...\n", delta);
		updateLikes(delta);
		updateLocalReplica(resource);
		printf("Total likes: %d\n", getLikes());

		printf("Exiting Critical Section.\n");

		printf("Attempting to release resource %s\n", resource);

		sendReleaseRequest(res, sock, &server_addr);
		waitForServerResponse(sock, &server_resp, DONT_DIE);

		assert(server_resp.msg == ACK);
		printf("Successfully released resource %d\n", res);

		waitForRequest(sock, &client_syn_req, &resp_addr);

		assert(client_syn_req.msg == SYN);
		printf("Received SYN request from %d. Sending updates (Likes: %d)...\n", ((struct sockaddr_in*)&resp_addr)->sin_port, getLikes());
		sendSynchronizationResponse(res, sock, (struct sockaddr_in*)&resp_addr);

		printf("Do you wish to continue? (y/N)\n");
		scanf("%s", choice);
		if(strcmp(choice, "n") == 0 || strcmp(choice, "N") == 0) break;
	}

	close(sock);
	
	return 0;
}