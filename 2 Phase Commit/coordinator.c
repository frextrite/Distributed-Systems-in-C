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

#define PORT ((1<<13)+5)
#define VOTE_LEN sizeof(struct VoteMessage)

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

enum MSG_TYPE {START_2PC, INIT, VOTE_REQUEST, VOTE_COMMIT, VOTE_ABORT, GLOBAL_COMMIT, GLOBAL_ABORT, ACK};
enum RETRY {DIE, DONT_DIE};

enum MSG_TYPE STATE = INIT;

struct sockaddr** client_addrs;
int num_clients;

struct VoteMessage {
	int flags;
	enum MSG_TYPE msg;
};

char* getMessageTag(enum MSG_TYPE msg) {
    if(msg == START_2PC) {
        return "START2PC";
    } else if(msg == INIT) {
        return "INIT";
    } else if(msg == VOTE_REQUEST) {
        return "VOTE_REQUEST";
    } else if(msg == VOTE_COMMIT) {
        return "VOTE_COMMIT";
    } else if(msg == VOTE_ABORT) {
        return "VOTE_ABORT";
    } else if(msg == GLOBAL_COMMIT) {
        return "GLOBAL_COMMIT";
    } else if(msg == GLOBAL_ABORT) {
        return "GLOBAL_ABORT";
    } else if(msg == ACK) {
        return "ACK";
    } else {
        return "UNK";
    }
}

char* getMessageDetails(enum MSG_TYPE msg) {
    if(msg == START_2PC) {
        return "Starting 2 Phase Commit...";
    } else if(msg == INIT) {
        return "Initializing for 2PC...";
    } else if(msg == VOTE_REQUEST) {
        return "Sending VOTE_REQUEST to all participants...";
    } else if(msg == VOTE_COMMIT) {
        return "Received VOTE_COMMIT from all participants...";
    } else if(msg == VOTE_ABORT) {
        return "Received VOTE_ABORT from a participant...";
    } else if(msg == GLOBAL_COMMIT) {
        return "Sending GLOBAL_COMMIT to all participants...";
    } else if(msg == GLOBAL_ABORT) {
        return "Sending GLOBAL_ABORT to all participants...";
    } else if(msg == ACK) {
        return "ACK";
    } else {
        return "UNK";
    }
}

void LOG(enum MSG_TYPE msg) {
    printf("[%s] %s\n", getMessageTag(msg), getMessageDetails(msg));
}

void consoleLogSend(enum MSG_TYPE msg, struct sockaddr* addr) {
    printf("[LOG] Sending %s to client %d\n", getMessageTag(msg), ((struct sockaddr_in*)addr)->sin_port);
}

void consoleLogReceive(enum MSG_TYPE msg, struct sockaddr* addr) {
    printf("[LOG] Received %s from client %d\n", getMessageTag(msg), ((struct sockaddr_in*)addr)->sin_port);
}

int sendClientVote(int sock, struct VoteMessage* vote, struct sockaddr* addr) {
	return sendto(sock, vote, VOTE_LEN, 0, addr, sizeof(struct sockaddr));
}

void sendVote(int sock, enum MSG_TYPE msg, struct sockaddr* addr) {
    struct VoteMessage vote;
    vote.msg = msg;
    if(sendClientVote(sock, &vote, addr) < 0) {
        handle_error("sendVote()");
    }
}

int getClientVote(int sock, struct VoteMessage* vote, struct sockaddr* addr) {
    if(addr == NULL) {
        return recv(sock, (struct VoteMessage*)vote, VOTE_LEN, 0);
    } else {
        socklen_t addrlen = sizeof(struct sockaddr);
	    return recvfrom(sock, (struct VoteMessage*)vote, VOTE_LEN, 0, addr, &addrlen);
    }
}

void multicastVoteMessage(int sock, enum MSG_TYPE msg) {
    for(int i = 0; i < num_clients; i++) {
        consoleLogSend(msg, client_addrs[i]);
        sendVote(sock, msg, client_addrs[i]);
    }
}

void waitForVote(int sock, struct VoteMessage* vote, struct sockaddr* addr, enum RETRY retry) {
	int status = getClientVote(sock, vote, addr);
    if(status < 0) {
		if(errno == EAGAIN || errno == EWOULDBLOCK) {
			if(retry == DONT_DIE) {
				errno = -1;
				return waitForVote(sock, vote, addr, DONT_DIE);
			}
            LOG(GLOBAL_ABORT);
            multicastVoteMessage(sock, GLOBAL_ABORT);
		}
        exit(EXIT_FAILURE);
	}
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
	setTimeout(sock, 120);
}

void disableTimeout(int sock) {
	setTimeout(sock, 0);
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

int waitForVotes(int sock) {
    int count_vote_commit = 0;
    int count_vote_abort = 0;
    int clients = num_clients;
    struct VoteMessage vote;
    struct sockaddr addr;
    while(clients--) {
        waitForVote(sock, &vote, &addr, DIE);
        consoleLogReceive(vote.msg, &addr);
        if(vote.msg == VOTE_COMMIT) {
            count_vote_commit++;
        } else if(vote.msg == VOTE_ABORT) {
            count_vote_abort++;
        }
    }
    assert(count_vote_commit + count_vote_abort == num_clients);
    return count_vote_commit - num_clients;
}

int main(int argc, char **argv) {
    num_clients = (argc > 1) ? argc - 1 : 0;
    client_addrs = (struct sockaddr**)malloc(sizeof(struct sockaddr*) * num_clients);
    for(int i = 0; i < num_clients; i++) {
        int port = atoi(argv[i+1]);
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

	enableTimeout(sock);

	struct VoteMessage vote;
	do {
		LOG(START_2PC);

        multicastVoteMessage(sock, VOTE_REQUEST);

        int status = waitForVotes(sock);

        char* vote_decision = (char*)malloc(sizeof(char) * 8);
        if(status == 0) {
            printf("All participants responded with VOTE_COMMIT.\nEnter COMMIT to proceed with voting or ABORT to abort: ");
            scanf("%s", vote_decision);
        }

        if(status == 0 && strcmp(vote_decision, "COMMIT") == 0) {
            LOG(GLOBAL_COMMIT);
            multicastVoteMessage(sock, GLOBAL_COMMIT);
        } else {
            LOG(GLOBAL_ABORT);
            multicastVoteMessage(sock, GLOBAL_ABORT);
        }
	} while(0);

	close(sock);
	
	return 0;
}