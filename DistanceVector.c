#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#define MAX_ROUTERS 100
#define MAX_NAME_LEN 50
#define INFINITY_VAL 999999

typedef struct {
    char name[MAX_NAME_LEN];
    int index;
} Router;

typedef struct {
    int cost;
    bool exists;
} Link;

typedef struct {
    Router routers[MAX_ROUTERS];
    int num_routers;
    Link adj_matrix[MAX_ROUTERS][MAX_ROUTERS];
    int distance_table[MAX_ROUTERS][MAX_ROUTERS][MAX_ROUTERS]; // [router][dest][via]
    int routing_table[MAX_ROUTERS][MAX_ROUTERS]; // [router][dest] = next_hop
    int routing_cost[MAX_ROUTERS][MAX_ROUTERS];  // [router][dest] = cost
} Network;

Network network;