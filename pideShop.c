#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <semaphore.h>
#include <complex.h>

//constants for defining maximum orders, oven size, delivery bag size, and times for preparation and cooking
#define MAX_ORDERS 1000
#define MAX_OVEN_SIZE 6
#define MAX_DELIVERY_BAG 3

//structure for hold order info
typedef struct {
    int order_id;
    int x, y; //coordinates of the delivery address
    time_t order_time; //time the order was placed
    int status; //status of the order (0: ordered, 1: preparing, 2: cooking, 3: ready for delivery, 4: out for delivery, 5: delivered, 6: canceled)
    int client_socket; //socket to communicate with the client
    int canceled_flag; // flag to indicate if the order was canceled
} Order;

//structure for cook
typedef struct {
    pthread_t thread; //thread ID
    int id; //cook ID
    Order *order; //order being prepared by the cook
    int prepared_orders; //number of orders prepared by the cook
} Cook;

//structure fordelivery person
typedef struct {
    pthread_t thread; //thread ID
    int id; //delivery person ID
    int speed; //speed of the delivery person
    Order *bag[MAX_DELIVERY_BAG]; //bag to hold orders for delivery
    int bag_count; //number of orders in the bag
    int delivered_orders; //number of orders delivered by the delivery person
} DeliveryPerson;

// Global variables
int port; //server port
int cook_pool_size ;//number of cooks, and number of delivery persons
int delivery_pool_size; //number of delivery persons
Cook *cooks; //array of cooks
DeliveryPerson *delivery_persons; //array of delivery persons
Order orders[MAX_ORDERS]; //array of all orders
Order *prep_queue[MAX_ORDERS]; //queue for waiting for prepared
Order *cook_queue[MAX_ORDERS]; //queue for waiting for cooked
Order *delivery_queue[MAX_ORDERS]; //queue for waiting for delivered
int order_count = 0; //total number of orders
int prep_queue_start = 0, prep_queue_end = 0; //indices for the preparation queue
int cook_queue_start = 0, cook_queue_end = 0; //indices for the cooking queue
int delivery_queue_start = 0, delivery_queue_end = 0; //indices for the delivery queue
int delivered_count = 0; //count of delivered orders

pthread_mutex_t order_mutex = PTHREAD_MUTEX_INITIALIZER; //mutex to protect order-related operations
pthread_cond_t order_cond = PTHREAD_COND_INITIALIZER; //condition variable for order processing
pthread_cond_t delivery_cond = PTHREAD_COND_INITIALIZER; //condition variable for delivery processing
sem_t oven_sem; //semaphore to manage the oven capacity

FILE *log_file; //log file to record order status

// Function prototypes
void *cook_routine(void *arg);
void *delivery_routine(void *arg);
void manager(int socket);
void log_order_status(Order *order, int status, int thread_id);
int calculate_delivery_time(int x, int y, int speed);
void signal_handler(int signal);
void enqueue_preparation(Order *order);
Order *dequeue_preparation();
void enqueue_cooking(Order *order);
Order *dequeue_cooking();
void enqueue_delivery(Order *order);
Order *dequeue_delivery();
void simulate_computation_delay_prep();
void simulate_computation_delay_cook();
void notify_clients_all_orders_completed();
void cancel_order(Order *order);
void print_most_efficient_workers();

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    if (signal == SIGINT) {
        pthread_mutex_lock(&order_mutex);
        printf("\nRIP PIDE SHOP...\n");
        for (int i = 0; i < order_count; i++) {
            if (orders[i].status != 5) {
                orders[i].status = 6;
                log_order_status(&orders[i], 6, -1);
            }
        }
        print_most_efficient_workers();
        pthread_mutex_unlock(&order_mutex);
        fclose(log_file);
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        printf("Usage: %s <ip_address> <port> <cook_pool_size> <delivery_pool_size> <delivery_speed>\n", argv[0]);
        return 1;
    }

    char *ip_address = argv[1]; //IP address 
    port = atoi(argv[2]); //port number
    cook_pool_size = atoi(argv[3]); //number of cooks
    delivery_pool_size = atoi(argv[4]); //number of delivery persons
    int delivery_speed = atoi(argv[5]); //speed of delivery

    signal(SIGINT, signal_handler); //setup signal handler for graceful shutdown
    signal(SIGPIPE, SIG_IGN); //ignore SIGPIPE signals

    cooks = malloc(cook_pool_size * sizeof(Cook)); //allocate memory for cooks
    delivery_persons = malloc(delivery_pool_size * sizeof(DeliveryPerson)); //allocate memory for delivery persons

    log_file = fopen("pide_shop.log", "w"); //open log file
    if (log_file == NULL) {
        printf("Failed to open log file\n");
        return 1;
    }

    sem_init(&oven_sem, 0, MAX_OVEN_SIZE); //initialize semaphore for oven capacity

    //create cook threads
    for (int i = 0; i < cook_pool_size; i++) {
        cooks[i].id = i;
        pthread_create(&cooks[i].thread, NULL, cook_routine, &cooks[i]);
    }

    //create delivery person threads
    for (int i = 0; i < delivery_pool_size; i++) {
        delivery_persons[i].id = i;
        delivery_persons[i].speed = delivery_speed;
        delivery_persons[i].bag_count = 0;
        pthread_create(&delivery_persons[i].thread, NULL, delivery_routine, &delivery_persons[i]);
    }

    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;

    server_socket = socket(AF_INET, SOCK_STREAM, 0); //create server socket
    if (server_socket < 0) {
        perror("Failed to create socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Failed to set SO_REUSEADDR");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;//IP adres for local network(dynamik) and static 
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        return 1;
    }
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind socket");
        return 1;
    }

    listen(server_socket, 5); //start listening for incoming connections
    printf("Pide Shop server listening on %s address and %d port\n", ip_address, port);

    while (1) {
        addr_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len); //accept a new client 
        if (client_socket < 0) {
            perror("Failed to accept connection");
            continue;
        }

        printf("New customer connected\n");
        manager(client_socket); //handle the new customer
    }

    return 0;
}

//routine for cooks to prepare and cook orders
void *cook_routine(void *arg) {
    Cook *cook = (Cook *)arg;

    while (1) {
        pthread_mutex_lock(&order_mutex);

        Order *order = dequeue_preparation(); //get the next order to prepare
        while (order == NULL) {
            pthread_cond_wait(&order_cond, &order_mutex);
            order = dequeue_preparation();
        }

        log_order_status(order, 1, cook->id); //log that the order is being prepared
        order->status = 1;
        if (send(order->client_socket, &order->status, sizeof(int), 0) == -1) {
            printf("%d th order canceled.\n", order->order_id);
            order->canceled_flag = 1;
            cancel_order(order);
            pthread_mutex_unlock(&order_mutex);
            continue;
        }
        pthread_mutex_unlock(&order_mutex);
        simulate_computation_delay_prep(); //simulate preparation time
        sem_wait(&oven_sem); //wait for an oven to become available
        pthread_mutex_lock(&order_mutex);
        log_order_status(order, 2, cook->id); //log that the order is being cooked
        order->status = 2;
        if (send(order->client_socket, &order->status, sizeof(int), 0) == -1) {
            if (order->canceled_flag == 0) {
                printf("%d th order canceled.\n", order->order_id);
                order->canceled_flag = 1;
                cancel_order(order);
            }
            pthread_mutex_unlock(&order_mutex);
            sem_post(&oven_sem); //release the oven
            continue;
        }
        pthread_mutex_unlock(&order_mutex);
        simulate_computation_delay_cook(); //simulate cooking time

        pthread_mutex_lock(&order_mutex);
        log_order_status(order, 3, cook->id); //log that the order is ready for delivery
        order->status = 3;
        if (send(order->client_socket, &order->status, sizeof(int), 0) == -1) {
            if (order->canceled_flag == 0) {
                printf("%d th order canceled.\n", order->order_id);
                order->canceled_flag = 1;
                cancel_order(order);
            }
            pthread_mutex_unlock(&order_mutex);
            sem_post(&oven_sem); //release the oven
            continue;
        }
        enqueue_delivery(order); //add the order to the delivery queue
        cook->prepared_orders++;
        pthread_cond_signal(&delivery_cond); //signal delivery persons
        pthread_mutex_unlock(&order_mutex);

        sem_post(&oven_sem); //release the oven
    }

    return NULL;
}

// Routine for delivery persons to deliver orders
void *delivery_routine(void *arg) {
    DeliveryPerson *delivery_person = (DeliveryPerson *)arg;

    while (1) {
        pthread_mutex_lock(&order_mutex);

        //fill the delivery bag with orders
        while (delivery_person->bag_count < MAX_DELIVERY_BAG && delivery_queue_start != delivery_queue_end) {
            Order *order = dequeue_delivery();
            if (order != NULL) {
                delivery_person->bag[delivery_person->bag_count++] = order;
                log_order_status(order, 4, delivery_person->id); //log that the order is out for delivery
                order->status = 4;
                if (send(order->client_socket, &order->status, sizeof(int), 0) == -1) {
                    if (order->canceled_flag == 0) {
                        printf("%d th order canceled.\n", order->order_id);
                        order->canceled_flag = 1;
                        cancel_order(order);
                    }
                    pthread_mutex_unlock(&order_mutex);
                    continue;
                }
            }
        }

        //if there are orders in the bag deliver them
        if (delivery_person->bag_count > 0) {
            pthread_mutex_unlock(&order_mutex);
            for (int i = 0; i < delivery_person->bag_count; i++) {
                Order *order = delivery_person->bag[i];
                int delivery_time = calculate_delivery_time(order->x, order->y, delivery_person->speed); //calculate delivery time
                //sleep(delivery_time);
                usleep(delivery_time);
                pthread_mutex_lock(&order_mutex);
                log_order_status(order, 5, delivery_person->id); //log that the order was delivered
                order->status = 5;
                if (send(order->client_socket, &order->status, sizeof(int), 0) == -1) {
                    if (order->canceled_flag == 0) {
                        printf("%d th order canceled.\n", order->order_id);
                        order->canceled_flag = 1;
                        cancel_order(order);
                    }
                    pthread_mutex_unlock(&order_mutex);
                    continue;
                }
                delivered_count++;
                delivery_person->delivered_orders++;
                if (delivered_count == order_count) {
                    notify_clients_all_orders_completed(); //notify all clients if all orders are delivered
                }
                pthread_mutex_unlock(&order_mutex);
            }
            delivery_person->bag_count = 0; //empty the bag
        } else {
            pthread_mutex_unlock(&order_mutex);
            sleep(1); //sleep for before checking again
        }
    }

    return NULL;
}

// Handle a new customer order
void manager(int socket) {
    int x, y;
    if (recv(socket, &x, sizeof(int), 0) <= 0 || recv(socket, &y, sizeof(int), 0) <= 0) {
        printf("Failed to receive customer coordinates\n");
        close(socket);
        return;
    }

    pthread_mutex_lock(&order_mutex);

    if (order_count < MAX_ORDERS) {
        orders[order_count].order_id = order_count + 1;
        orders[order_count].x = x;
        orders[order_count].y = y;
        orders[order_count].order_time = time(NULL);
        orders[order_count].status = 0; //order received
        orders[order_count].client_socket = socket;
        orders[order_count].canceled_flag = 0; //flag not canceled
        log_order_status(&orders[order_count], 0, -1);
        enqueue_preparation(&orders[order_count]); //add order to preparation queue
        order_count++;
        pthread_cond_signal(&order_cond); //signal cooks
    } else {
        printf("Maximum orders reached. Cannot accept new order.\n");
        close(socket);
    }

    pthread_mutex_unlock(&order_mutex);
}

//log the status of an order
void log_order_status(Order *order, int status, int thread_id) {
    const char *status_str;
    switch (status) {
        case 0:
            status_str = "Order received";
            break;
        case 1:
            status_str = "Preparing";
            break;
        case 2:
            fprintf(log_file, "Order for client %d is get order into aparatus\n", order->order_id);
            fflush(log_file);
            status_str = "Cooking";
            break;
        case 3:
            fprintf(log_file, "Order for client %d is get order into aparatus\n", order->order_id);
            fflush(log_file);
            status_str = "Ready for delivery";
            break;
        case 4:
            status_str = "Out for delivery";
            break;
        case 5:
            status_str = "Delivered";
            break;
        case 6:
            status_str = "Canceled";
            break;
        default:
            status_str = "Unknown status";
    }

    fprintf(log_file, "Order %d at (%d, %d): %s by thread %d at %s", order->order_id, order->x, order->y, status_str, thread_id, ctime(&order->order_time));
    fflush(log_file);
}

//calculate the delivery time 
int calculate_delivery_time(int x, int y, int speed) {
    double distance = sqrt(x * x + y * y);
    return (int)(distance / speed * 60); // Convert distance to time based on speed
}

//enqueue an order for preparation
void enqueue_preparation(Order *order) {
    prep_queue[prep_queue_end++] = order;
    if (prep_queue_end == MAX_ORDERS) prep_queue_end = 0;
}

//dequeue an order for preparation
Order *dequeue_preparation() {
    if (prep_queue_start == prep_queue_end) return NULL;
    Order *order = prep_queue[prep_queue_start++];
    if (prep_queue_start == MAX_ORDERS) prep_queue_start = 0;
    return order;
}

//enqueue an order for cooking
void enqueue_cooking(Order *order) {
    cook_queue[cook_queue_end++] = order;
    if (cook_queue_end == MAX_ORDERS) cook_queue_end = 0;
}

//dequeue an order for cooking
Order *dequeue_cooking() {
    if (cook_queue_start == cook_queue_end) return NULL;
    Order *order = cook_queue[cook_queue_start++];
    if (cook_queue_start == MAX_ORDERS) cook_queue_start = 0;
    return order;
}

//enqueue an order for delivery
void enqueue_delivery(Order *order) {
    delivery_queue[delivery_queue_end++] = order;
    if (delivery_queue_end == MAX_ORDERS) delivery_queue_end = 0;
}

//dequeue an order for delivery
Order *dequeue_delivery() {
    if (delivery_queue_start == delivery_queue_end) return NULL;
    Order *order = delivery_queue[delivery_queue_start++];
    if (delivery_queue_start == MAX_ORDERS) delivery_queue_start = 0;
    return order;
}

//simulate a delay for preparation(30 a 40)
void simulate_computation_delay_prep() {
    int n = 30, m = 40;
    double complex matrix[30][40];
    double complex result[30][30];

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            matrix[i][j] = rand() / (double)RAND_MAX + I * (rand() / (double)RAND_MAX);
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            result[i][j] = 0;
            for (int k = 0; k < m; k++) {
                result[i][j] += conj(matrix[i][k]) * matrix[j][k];
            }
        }
    }
}

//simulate a delay for cooking half of it (15 e 40)
void simulate_computation_delay_cook() {
    int n = 15, m = 40;
    double complex matrix[15][40];
    double complex result[15][15];

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            matrix[i][j] = rand() / (double)RAND_MAX + I * (rand() / (double)RAND_MAX);
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            result[i][j] = 0;
            for (int k = 0; k < m; k++) {
                result[i][j] += conj(matrix[i][k]) * matrix[j][k];
            }
        }
    }
}

//notify all clients that all orders are completed
void notify_clients_all_orders_completed() {
    for (int i = 0; i < order_count; i++) {
        if (orders[i].client_socket != -1) {
            close(orders[i].client_socket);
        }
    }
}

//cancel an order and update its status
void cancel_order(Order *order) {
    order->status = 6;
    log_order_status(order, 6, -1);
    if (send(order->client_socket, &order->status, sizeof(int), 0) == -1) {
        close(order->client_socket);
        order->client_socket = -1;
    }
}

//print the most efficient workers 
void print_most_efficient_workers() {
    int max_prepared_orders = 0;
    int most_efficient_cook_id = -1;
    for (int i = 0; i < cook_pool_size; i++) {
        if (cooks[i].prepared_orders > max_prepared_orders) {
            max_prepared_orders = cooks[i].prepared_orders;
            most_efficient_cook_id = cooks[i].id;
        }
    }
    if (most_efficient_cook_id != -1) {
        printf("Most efficient cook: Cook %d with %d orders prepared\n", most_efficient_cook_id, max_prepared_orders);
    }

    int max_delivered_orders = 0;
    int most_efficient_delivery_person_id = -1;
    for (int i = 0; i < delivery_pool_size; i++) {
        if (delivery_persons[i].delivered_orders > max_delivered_orders) {
            max_delivered_orders = delivery_persons[i].delivered_orders;
            most_efficient_delivery_person_id = delivery_persons[i].id;
        }
    }
    if (most_efficient_delivery_person_id != -1) {
        printf("Most efficient delivery person: Delivery Person %d with %d orders delivered\n", most_efficient_delivery_person_id, max_delivered_orders);
    }
}