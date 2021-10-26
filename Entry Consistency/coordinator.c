#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>

#define PORT ((1<<13)+5)
#define NR_RESOURCES 2
#define CLIENT_DATA_LEN sizeof(struct ClientResponse)
#define REQ_LEN sizeof(struct ClientRequest)
#define RESOURCE_LEN 64

#define NO_OWNER (1<<2)

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

enum MSG_TYPE {REQ, OK, RELEASE, ACK, BUSY, SYN, SYNACK};
enum RES_STATE {RES_AVAIL, RES_BUSY, RES_DOWN};

struct sockaddr* previous_owner = NULL;

struct Resource {
	int id;
	char* path_to_resource;
	struct sockaddr* locked_by;
} RESOURCES[NR_RESOURCES];

void initializeResource(int id, const char* path_to_resource) {
	struct Resource* temp = &RESOURCES[id];
	temp->id = id;
	temp->locked_by = NULL;
	temp->path_to_resource = (char*)malloc(sizeof(char)*RESOURCE_LEN);
	strcpy(temp->path_to_resource, path_to_resource);
}

void initializeResources() {
	const char* RESOURCE[] = {"/tmp/resource_data_primary", "/tmp/resource_data_secondary"};
	for(int i = 0; i < NR_RESOURCES; i++) {
		initializeResource(i, RESOURCE[i]);
	}
}

struct Resource* getResource(int res) {
	if(res <= 0 || res > NR_RESOURCES) {
		return NULL;
	}
	return &RESOURCES[res-1];
}

struct QueueNode {
	struct sockaddr* addr;
	struct QueueNode* next;
};

struct Queue {
	int size;
	struct QueueNode* front;
	struct QueueNode* back;
};

struct Queue qptr[NR_RESOURCES];

struct ClientRequest {
	int flags;
	int res;
	enum MSG_TYPE msg;
};

struct DataStore {
	int likes;
};

struct ClientResponse {
	int flags;
	struct sockaddr owner;
	struct DataStore data;
	enum MSG_TYPE msg;
};

void setPreviousOwner(struct sockaddr* addr) {
	previous_owner = addr;
}

struct Queue* createQueue() {
	struct Queue* q = malloc(sizeof(struct Queue));
	q->size = 0;
	q->front = NULL;
	q->back = NULL;
	return q;
}

struct QueueNode* createQueueNode() {
	struct QueueNode* node = (struct QueueNode*)malloc(sizeof(struct QueueNode));
	node->addr = NULL;
	node->next = NULL;
	return node;
}

void push(struct Queue* q, struct sockaddr* addr) {
	struct QueueNode* node = createQueueNode();
	node->addr = addr;
	q->size++;
	if(q->front == NULL) {
		q->front = node;
		q->back = node;
	} else {
		q->back->next = node;
		q->back = node;
	}
}

void pop(struct Queue* q) {
	q->size--;
	struct QueueNode* temp = q->front;
	q->front = q->front->next;
	if(q->front == NULL) {
		q->back = NULL;
	}
	free(temp->addr);
	free(temp);
}

int size(struct Queue* q) {
	return q->size;
}

int empty(struct Queue* q) {
	return size(q) == 0;
}

struct Queue* getResourceQueue(int res) {
	return &qptr[res-1];
}

struct sockaddr front(struct Queue* q) {
	return *(struct sockaddr*)(q->front->addr);
}

int sendResponse(int sock, struct ClientResponse* resp, struct sockaddr* addr) {
	return sendto(sock, resp, CLIENT_DATA_LEN, 0, addr, sizeof(struct sockaddr));
}

int sendStatusResponse(enum MSG_TYPE msg, int sock, struct sockaddr* addr) {
	struct ClientResponse resp;
	resp.msg = msg;
	return sendResponse(sock, &resp, addr);
}

void reportRequestGranted(int res, int sock, struct sockaddr* addr) {
	struct ClientResponse resp;
	if(previous_owner == NULL) {
		resp.flags |= NO_OWNER;
	} else {
		memcpy(&resp.owner, previous_owner, sizeof(struct sockaddr));
	}
	setPreviousOwner(addr);
	resp.msg = OK;
	if(sendResponse(sock, &resp, addr) < 0) {
		handle_error("sendto(OK)");
	}
}

void reportAck(int sock, struct sockaddr* addr) {
	if(sendStatusResponse(ACK, sock, addr) < 0) {
		handle_error("sendto(ACK");
	}
}

void reportResourceBusy(int sock, struct sockaddr* addr) {
	if(sendStatusResponse(BUSY, sock, addr) < 0) {
		handle_error("sendto(BUSY)");
	}
}

struct sockaddr* getResourceOwner(int res) {
	return getResource(res)->locked_by;
}

int isResourceBusy(int res) {
	return getResourceOwner(res) != NULL;
}

enum RES_STATE getResourceState(int res) {
	return isResourceBusy(res);
}

void addClientToQueue(int res, struct sockaddr* addr) {
	struct Queue* q = getResourceQueue(res);
	push(q, addr);
}

void lockResource(int res, struct sockaddr* addr) {
	if(getResourceOwner(res) == addr) {
		return;
	}
	getResource(res)->locked_by = addr;
}

void releaseResource(int res) {
	getResource(res)->locked_by = NULL;
}

int getClientPort(struct sockaddr* addr) {
	return ((struct sockaddr_in*)addr)->sin_port;
}

struct sockaddr* handleResourceRelease(int res) {
	struct Queue* q = getResourceQueue(res);
	struct sockaddr* addr = NULL;
	if(!empty(q)) {
		struct sockaddr old_addr = front(q);
		pop(q);
		addr = malloc(sizeof(struct sockaddr));
		memcpy(addr, &old_addr, sizeof(struct sockaddr));
		lockResource(res, addr);
	} else {
		struct sockaddr* owner = getResourceOwner(res);
		releaseResource(res);
		assert(getResourceOwner(res) == NULL);
		// free(owner);
	}
	return addr;
}

void printQueueDetails(int res) {
	struct Queue* q = getResourceQueue(res);
	printf("Resource %d Queue size: %d\n", res, q->size);
}

int areClientsSame(struct sockaddr* addr1, struct sockaddr* addr2) {
	return getClientPort(addr1) == getClientPort(addr2);
}

void handleClientRequest(int sock, struct ClientRequest* client_req, struct sockaddr* addr) {
	enum MSG_TYPE msg = client_req->msg;
	printf("%d\n", msg);
	int res = client_req->res;
	int flags = client_req->flags;
	int client_port = ((struct sockaddr_in*)addr)->sin_port;
	enum RES_STATE state = getResourceState(res);
	switch(msg) {
		case REQ:
			printf("Client %d requested resource %s having id %d\n", ((struct sockaddr_in*)addr)->sin_port, getResource(res)->path_to_resource, res);
			if(state == RES_BUSY) {
				printf("Resource already busy. Responding with BUSY signal...\n");
				addClientToQueue(res, addr);
				printQueueDetails(res);
				reportResourceBusy(sock, addr);
			} else if(state == RES_AVAIL) {
				printf("Granting access to client %d\n", client_port);
				lockResource(res, addr);
				// printf("[DEBUG] %d\n", ((struct sockaddr_in*)addr)->sin_port);
				reportRequestGranted(res, sock, addr);
			}
			break;
		case RELEASE:
			printf("Client requested to release (state: %d) resource %d\n", state, res);
			reportAck(sock, addr);
			if(state == RES_AVAIL) {
				printf("[ERROR] Trying to release an already free resource.\n");
				break;
			}
			if(!areClientsSame(getResourceOwner(res), addr)) {
				// printf("[DEBUG] %d\n", ((struct sockaddr_in*)addr)->sin_port);
				printf("[ERROR] Trying to release resource not owned by client.\n");
				break;
			}
			if(state == RES_BUSY) {
				printf("Releasing resource %d requested by %d\n", res, client_port);
				struct sockaddr* next_client = handleResourceRelease(res);
				if(next_client != NULL) {
					printf("Granting access to next client %d\n", ((struct sockaddr_in*)next_client)->sin_port);
					reportRequestGranted(res, sock, next_client);
					printQueueDetails(res);
				}
			}
			break;
		default:
			break;
	}
}

int main(int argc, char **argv) {
	initializeResources();

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

	printf("Attempting to start server on port %d\n", PORT);

	if(bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0) {
		handle_error("bind()");
	}

	printf("Listening on port %d...\n", PORT);

	struct ClientRequest client_req;
	int requests = 0;
	for(;;) {
		struct sockaddr* client_addr = malloc(sizeof(struct sockaddr));
		if(recvfrom(sock, &client_req, REQ_LEN, 0, client_addr, &addrlen) < 0) {
			handle_error("recvfrom()");
		}
		requests++;
		printf("Handling client request number %d with message ", requests);
		handleClientRequest(sock, &client_req, client_addr);
	}
	
	return 0;
}