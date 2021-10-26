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

#define VOTE_LEN sizeof(struct VoteMessage)

#define handle_error(msg) \
	do {perror(msg); exit(EXIT_FAILURE); } while (0)

enum MSG_TYPE {START_2PC, INIT, VOTE_REQUEST, VOTE_COMMIT, VOTE_ABORT, GLOBAL_COMMIT, GLOBAL_ABORT, ACK};
enum RETRY {DIE, DONT_DIE};

enum MSG_TYPE STATE = INIT;

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
        return "Received VOTE_REQUEST from coordinator...";
    } else if(msg == VOTE_COMMIT) {
        return "Sending VOTE_COMMIT to coordinator...";
    } else if(msg == VOTE_ABORT) {
        return "Sending VOTE_ABORT to coordinator...";
    } else if(msg == GLOBAL_COMMIT) {
        return "Received GLOBAL_COMMIT from coordinator...";
    } else if(msg == GLOBAL_ABORT) {
        return "Received GLOBAL_ABORT from coordinator...";
    } else if(msg == ACK) {
        return "ACK";
    } else {
        return "UNK";
    }
}

void LOG(enum MSG_TYPE msg) {
    printf("[%s] %s\n", getMessageTag(msg), getMessageDetails(msg));
}

int sendVote(int sock, struct VoteMessage* vote, struct sockaddr_in* addr) {
	return sendto(sock, vote, VOTE_LEN, 0, (struct sockaddr*)addr, sizeof(struct sockaddr));
}

void performVoting(int sock, enum MSG_TYPE msg, struct sockaddr_in* addr) {
    struct VoteMessage vote;
    vote.msg = msg;
    if(sendVote(sock, &vote, addr) < 0) {
        handle_error("performVoting()");
    }
}

int getServerVote(int sock, struct VoteMessage *vote) {
	return recv(sock, (struct VoteMessage*)vote, VOTE_LEN, 0);
}

int getVote(int sock, struct VoteMessage* vote, struct sockaddr* addr) {
    socklen_t addrlen = sizeof(struct sockaddr);
	return recvfrom(sock, (struct VoteMessage*)vote, VOTE_LEN, 0, addr, &addrlen);
}

void waitForVote(int sock, struct VoteMessage* vote, struct sockaddr* addr) {
	if(getVote(sock, vote, addr) < 0) {
		handle_error("waitForVote()");
		exit(EXIT_FAILURE);
	}
}

int waitForServerVote(int sock, struct VoteMessage* vote, enum RETRY retry) {
	int status = getServerVote(sock, vote);
    if(status < 0) {
		if(errno == EAGAIN || errno == EWOULDBLOCK) {
			if(retry == DONT_DIE) {
				errno = -1;
				return waitForServerVote(sock, vote, DONT_DIE);
			}
            LOG(VOTE_ABORT);
			exit(EXIT_FAILURE);
		}
        return status;
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

int main(int argc, char **argv) {
	int server_port = (argc > 1) ? atoi(argv[1]) : (1<<13)+5;
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

	struct VoteMessage vote;
	do {
		LOG(INIT);

        if(waitForServerVote(sock, &vote, DIE) < 0) {
            LOG(VOTE_ABORT);
            exit(EXIT_FAILURE);
        }

        assert(vote.msg == VOTE_REQUEST);
        LOG(VOTE_REQUEST);

        char* process_decision = (char*)malloc(sizeof(char) * 8);
        printf("Enter COMMIT to proceed with voting or ABORT to abort: ");
        scanf("%s", process_decision);
        
        if(strcmp(process_decision, "COMMIT") == 0) {
            LOG(VOTE_COMMIT);
            performVoting(sock, VOTE_COMMIT, &server_addr);

            if(waitForServerVote(sock, &vote, DIE) < 0) {
                // multicast DECISION_REQUEST
            }

            enum MSG_TYPE DECISION = vote.msg;
            if(DECISION == GLOBAL_COMMIT) {
                LOG(GLOBAL_COMMIT);
            } else {
                LOG(GLOBAL_ABORT);
            }
        } else {
            LOG(VOTE_ABORT);
            performVoting(sock, VOTE_ABORT, &server_addr);
        }
	} while(0);

	close(sock);
	
	return 0;
}