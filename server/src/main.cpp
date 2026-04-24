// main.cpp (server)
#include <enet/enet.h>
#include <iostream>
#include <unistd.h>

int main() {
    // check init
    if (enet_initialize() != 0) {
        fprintf(stderr, "Failed to initialize ENet\n");
        exit(EXIT_FAILURE);
    }

    // bind ports
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = 7777;

    // make server
    ENetHost* server = enet_host_create(
        &addr,
        32, // max clients
        2,  // chanels
        0,  // incoming bandwith
        0   // outgoing bandwith
    );
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        exit(EXIT_FAILURE);
    }

    // start server loop
    while (true) {
        ENetEvent event;
    }
}