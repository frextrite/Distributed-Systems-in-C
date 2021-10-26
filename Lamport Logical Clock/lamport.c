#include<stdio.h>
#include <stdlib.h>
#include <assert.h>

#define NR_PROCESS 2

enum type {LOCAL=0, SEND=1, RECEIVE=2};

void initialize_process(int** process, int num_events, int offset) {
    *process = (int*)malloc(sizeof(int) * num_events);
    for(int i = 0; i < num_events; i++) {
        (*process)[i] = i + offset;
    }
}

int** initialize_dependency_matrix(int num_events[NR_PROCESS]) {
    assert(NR_PROCESS == 2);
    int** dependency_matrix = (int**)malloc(sizeof(int*) * num_events[0]);
    for(int i = 0; i < num_events[0]; i++) {
        dependency_matrix[i] = (int*)malloc(sizeof(int) * num_events[1]);
    }
    return dependency_matrix;
}

void get_dependency_from_user(int** dependency_matrix, int num_events[NR_PROCESS]) {
    assert(NR_PROCESS == 2);
    printf("========== Dependency Matrix ==========\n");
    printf("0 for LOCAL event\n");
    printf("1 for SEND event from p1->p2\n");
    printf("2 for SEND event from p2->p1\n");
    for(int i = 0; i < num_events[0]; i++) {
        for(int j = 0; j < num_events[1]; j++) {
            scanf("%d", &dependency_matrix[i][j]);
        }
    }
}

int max(int x, int y) {
    return x >= y ? x : y;
}

int main() {
    int num_events[NR_PROCESS];
    int offset[NR_PROCESS] = {5, 3};
    int* process[NR_PROCESS];

    printf("Enter the number of events for each process\n");
    
    for(int i = 0; i < NR_PROCESS; i++) {
        printf("Events for process %d:\n", i + 1);
        scanf("%d", &num_events[i]);
        initialize_process(&process[i], num_events[i], offset[i]);
    }

    for(int pid = 0; pid < NR_PROCESS; pid++) {
        printf("Current Lamport logical clock for Process %d:\n", pid + 1);
        for(int i = 0; i < num_events[pid]; i++) {
            printf("%d ", process[pid][i]);
        }
        printf("\n");
    }

    int** dependency_matrix = initialize_dependency_matrix(num_events);

    get_dependency_from_user(dependency_matrix, num_events);

    for(int i = 0; i < num_events[0]; i++) {
        for(int j = 0; j < num_events[1]; j++) {
            if(dependency_matrix[i][j] == SEND) {
                int source = 0;
                int destination = 1;
                process[destination][j] = max(process[destination][j], process[source][i] + 1);
                for(int k = j + 1; k < num_events[destination]; k++) {
                    process[destination][k] = process[destination][k - 1] + 1;
                }
            } else if(dependency_matrix[i][j] == RECEIVE) {
                int source = 1;
                int destination = 0;
                process[destination][i] = max(process[destination][i], process[source][j] + 1);
                for(int k = i + 1; k < num_events[destination]; k++) {
                    process[destination][k] = process[destination][k - 1] + 1;
                }
            }
        }
    }

    for(int pid = 0; pid < NR_PROCESS; pid++) {
        printf("Lamport logical clock for Process %d:\n", pid + 1);
        for(int i = 0; i < num_events[pid]; i++) {
            printf("%d ", process[pid][i]);
        }
        printf("\n");
    }

    return 0;
}