#include<stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

#define NR_PROCESS 2
#define MAX_ADJACENT 8
#define MAX_NODES 16

enum type {LOCAL=0, SEND=1, RECEIVE=2};

struct WirelessNode {
    int id;
    int capability;
    int num_adj;
    int adj[MAX_ADJACENT];
};

struct WirelessNode* initialize_wireless_nodes(int num_nodes) {
    struct WirelessNode* nodes = (struct WirelessNode*)malloc(sizeof(struct WirelessNode) * num_nodes);
    for(int i = 0; i < num_nodes; i++) {
        nodes[i].id = i;
        nodes[i].capability = 0;
        nodes[i].num_adj = 0;
    }
    return nodes;
}

int max(int x, int y) {
    return x >= y ? x : y;
}

void get_node_capability_from_user(struct WirelessNode* nodes, int num_nodes) {
    printf("Enter wireless capabilities for each node, where, ");
    printf("a higher positive number denotes a node with greater wireless capability\n");
    
    for(int i = 0; i < num_nodes; i++) {
        scanf("%d", &nodes[i].capability);
    }
}

void get_node_graph_from_user(struct WirelessNode* nodes, int num_nodes) {
    int num_edges = 0;
    printf("Enter number of edges: ");
    scanf("%d", &num_edges);

    printf("Enter %d edges denoting an edge between nodes with id u and v [0, %d)\n", num_edges, num_nodes);
    for(int i = 0; i < num_edges; i++) {
        int u, v;
        scanf("%d %d", &u, &v);
        nodes[u].adj[nodes[u].num_adj++] = v;
        nodes[v].adj[nodes[v].num_adj++] = u;
    }
}

int get_capability(struct WirelessNode* nodes, int node) {
    return nodes[node].capability;
}

int get_the_chosen_one(int num_nodes) {
    return rand() % num_nodes;
}

int get_id(struct WirelessNode* nodes, int node) {
    return nodes[node].id;
}

int dfs_capability(int u, int num_nodes, struct WirelessNode* nodes, int* visited) {
    visited[u] = 1;
    printf("[Node %d] Received election request. Finding node with maximum capability\n", u);
    int max_capability_node = u;
    int max_capability = nodes[u].capability;
    for(int i = 0; i < nodes[u].num_adj; i++) {
        int v = nodes[u].adj[i];
        if(visited[v] == 0) {
            int other = dfs_capability(v, num_nodes, nodes, visited);
            int other_capability = get_capability(nodes, other);
            if(other_capability > max_capability) {
                max_capability = other_capability;
                max_capability_node = other;
            }
        }
    }
    printf("[Node %d] Found node %d with capability %d\n", u, max_capability_node, max_capability);
    return max_capability_node;
}

int hold_elections(int chosen_one, int num_nodes, struct WirelessNode* nodes) {
    int* visited = (int*)malloc(sizeof(int) * num_nodes);
    return dfs_capability(chosen_one, num_nodes, nodes, visited);
}

int main() {
    srand(time(0));
    int num_nodes = 0;
    struct WirelessNode* wireless_nodes = NULL;

    printf("Enter number of wireless nodes present in the network: ");
    scanf("%d", &num_nodes);
    
    wireless_nodes = initialize_wireless_nodes(num_nodes);

    get_node_capability_from_user(wireless_nodes, num_nodes);
    get_node_graph_from_user(wireless_nodes, num_nodes);

    printf("\nPinging the current co-ordinator...\n");

    printf("Uh oh, looks like the current co-ordinator crashed...\n");
    
    int chosen_one = get_the_chosen_one(num_nodes);
    printf("Choosing node %d for holding elections\n", chosen_one);
    
    printf("Starting elections...\n");

    int max_capability_node = hold_elections(chosen_one, num_nodes, wireless_nodes);
    int max_capability = get_capability(wireless_nodes, max_capability_node);
    
    printf("Elections held successfully...\n");
    printf("Found node %d having maximum capability %d to be the next co-ordinator. Broadcasting...\n", max_capability_node, max_capability);
    
    return 0;
}
