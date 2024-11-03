#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

#define MAX_CLIENTS 1000

int client_sockets[MAX_CLIENTS]; //array to hold client socket descriptors

//handling shutdown signals (SIGINT, SIGQUIT)
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGQUIT) {
        printf("Client shutting down...\n");
        //loop through all client sockets and send cancel signal if active
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] != -1) {
                printf("Order %d is canceled\n", i + 1);
                int cancel_status = 6; //status code for canceling the order
                send(client_sockets[i], &cancel_status, sizeof(int), 0); //send cancel status to client
                close(client_sockets[i]); //close the socket
                client_sockets[i] = -1; //mark socket as closed
            }
        }
        exit(0); 
    }
}

int main(int argc, char *argv[]) {
    if (argc != 6) { //check if the correct number of arguments is provided
        printf("Usage: %s <server_ip> <port> <num_clients> <town_size_x> <town_size_y>\n", argv[0]);
        return 1;
    }

    char *server_ip = argv[1]; //server IP address
    int port = atoi(argv[2]); //server port number
    int num_clients = atoi(argv[3]); //number of clients

    if (num_clients > MAX_CLIENTS) { //limit the number of clients if it exceeds the maximum
        printf("Warning: Limiting number of clients to %d\n", MAX_CLIENTS);
        num_clients = MAX_CLIENTS;
    }

    int town_size_x = atoi(argv[4]); //town size x
    int town_size_y = atoi(argv[5]); //town size y

    srand(time(NULL)); 

    signal(SIGINT, signal_handler); //register signal handler for SIGINT
    signal(SIGQUIT, signal_handler); //register signal handler for SIGQUIT

    //initialize client sockets array to -1 (indicating no connection)
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = -1;
    }

    // Loop to initialize and connect clients
    for (int i = 0; i < num_clients; i++) {
        int x = rand() % town_size_x; 
        int y = rand() % town_size_y; 
        client_sockets[i] = socket(AF_INET, SOCK_STREAM, 0); //create a new socket for the client
        if (client_sockets[i] < 0) { //check if socket creation failed
            printf("Failed to create socket for client %d\n", i + 1);
            continue;
        }

        struct sockaddr_in server_addr; //server address structure
        memset(&server_addr, 0, sizeof(server_addr)); //clear the server address structure
        server_addr.sin_family = AF_INET; //set address family to AF_INET
        server_addr.sin_port = htons(port); //set server port
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr); //convert and set server IP address

        //attempt to connect to the server
        if (connect(client_sockets[i], (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            printf("Failed to connect client to server\n");
            close(client_sockets[i]); //close the socket if connection fails
            client_sockets[i] = -1; //mark socket as closed
            raise(SIGINT); //raise SIGINT signal to shutdown
        }

        //send coordinates to the server
        if (send(client_sockets[i], &x, sizeof(int), 0) < 0 || send(client_sockets[i], &y, sizeof(int), 0) < 0) {
            printf("Failed to send order coordinates for client %d\n", i + 1);
            close(client_sockets[i]); //close the socket if sending fails
            client_sockets[i] = -1; //mark socket as closed
        } else {
            printf("Order placed at (%d, %d) for client %d\n", x, y, i + 1); //confirm order placement
        }
    }

    fd_set readfds; //set of file descriptors for select
    int canceled_flag = 0; //flag to indicate if an order was canceled

    //handle communication with the server
    while (1) {
        FD_ZERO(&readfds); //clear the set of file descriptors

        //add client sockets to the set
        for (int i = 0; i < num_clients; i++) {
            if (client_sockets[i] != -1) {
                FD_SET(client_sockets[i], &readfds);
            }
        }

        int activity = select(FD_SETSIZE, &readfds, NULL, NULL, NULL); //wait for activity on any socket

        if (activity < 0) { //check if select failed
            perror("select");
            exit(1);
        }

        //loop through clients to check for activity
        for (int i = 0; i < num_clients; i++) {
            if (client_sockets[i] != -1 && FD_ISSET(client_sockets[i], &readfds)) { //check if the socket has activity
                int order_status;
                int recv_result = recv(client_sockets[i], &order_status, sizeof(int), 0); //receive order status

                if (recv_result > 0) { //check if data was received
                    switch (order_status) {
                        case 0:
                            printf("Order placed for client %d\n", i + 1);
                            break;
                        case 1:
                            printf("Order for client %d is being prepared\n", i + 1);
                            break;
                        case 2:
                            printf("Order for client %d is get order into apparatus\n", i + 1);
                            printf("Order for client %d is being cooked\n", i + 1);
                            break;
                        case 3:
                            printf("Order for client %d is get order into apparatus\n", i + 1);
                            printf("Order for client %d is ready for delivery\n", i + 1);
                            break;
                        case 4:
                            printf("Order for client %d is out for delivery\n", i + 1);
                            break;
                        case 5:
                            printf("Order for client %d has been delivered\n", i + 1);
                            close(client_sockets[i]); //close the socket after delivery
                            client_sockets[i] = -1; //mark socket as closed
                            break;
                        case 6:
                            printf("Order for client %d has been canceled\n", i + 1);
                            close(client_sockets[i]); //close the socket after cancellation
                            client_sockets[i] = -1; //mark socket as closed
                            break;
                        default:
                            printf("Unknown status for order of client %d\n", i + 1);
                            break;
                    }
                } else if (recv_result == 0) { //check if the connection was closed
                    close(client_sockets[i]); //close the socket
                    client_sockets[i] = -1; //mark socket as closed
                    canceled_flag = 1; //set the cancel flag
                } else { //chek error
                    perror("recv");
                    close(client_sockets[i]); //close the socket
                    client_sockets[i] = -1; //mark socket as closed
                }
            }
        }

        if (canceled_flag == 1) { //check if any order was canceled
            printf("RIP PIDE SHOP ...\n");
            raise(SIGINT); //raise SIGINT signal to shutdown
        }

        //check if all sockets are closed
        bool all_sockets_closed = true;
        for (int i = 0; i < num_clients; i++) {
            if (client_sockets[i] != -1) {
                all_sockets_closed = false;
                break;
            }
        }

        if (all_sockets_closed) { //if all sockets are closed, shut down the client
            printf("All orders processed. Shutting down client.\n");
            raise(SIGINT); //raise SIGINT signal to shutdown
        }
    }

    //close all client sockets before exiting
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != -1) {
            close(client_sockets[i]);
        }
    }

    return 0;
}