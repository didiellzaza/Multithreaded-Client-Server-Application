#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>

#define maxClients 3
#define maxMsgSize 300
#define serverKey 12345

// Request types
#define requestConnect 1
#define requestData 2
#define requestDisconnect 3
#define requestBroadcast 4

// Structure for the message
struct message {
    long mtype;
    int clientId;
    int requestType;
    char mtext[maxMsgSize];
};

struct Client {
    int clientId;
    int msgQueueId;
};

struct Client clients[maxClients];
int clientCount = 0;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;
int msgQueueId;

void processRequest(int clientId, int requestType, const char* mtext) {
    struct message response;
    response.mtype = clientId;
    response.clientId = clientId;
    response.requestType = requestType;

    switch (requestType) {
        case requestConnect:
            snprintf(response.mtext, sizeof(response.mtext), "You have been connected to the server");
            break;
        case requestData:
            snprintf(response.mtext, sizeof(response.mtext), "Client message: %s", mtext);
            break;
        case requestDisconnect:
            snprintf(response.mtext, sizeof(response.mtext), "You have been disconnected from the server");
            break;
        case requestBroadcast:
            snprintf(response.mtext, sizeof(response.mtext), "Broadcast message from Client %d: %s", clientId, mtext);
            break;
        default:
            snprintf(response.mtext, sizeof(response.mtext), "Unknown request type");
            break;
    }

    msgsnd(clients[clientId].msgQueueId, &response, sizeof(response) - sizeof(long), 0);
}

void* client_thread(void* arg) {
    int clientId = *((int*)arg);
    struct message request;

    while (1) {
        msgrcv(clients[clientId].msgQueueId, &request, sizeof(request) - sizeof(long), clientId, 0);

        switch (request.requestType) {
            case requestConnect:
                printf("Client %d connected\n", clientId);
                processRequest(clientId, request.requestType, request.mtext);
                break;
            case requestData:
                printf("Received request from Client %d: %s\n", clientId, request.mtext);
                processRequest(clientId, request.requestType, request.mtext);
                break;
            case requestDisconnect:
                printf("Client %d disconnected\n", clientId);
                pthread_mutex_lock(&clientsMutex);
                clientCount--;
                pthread_mutex_unlock(&clientsMutex);
                pthread_exit(NULL);
                break;
            case requestBroadcast:
                printf("Client %d sent a broadcast message\n", clientId);
                for (int i = 0; i < maxClients; i++) {
                    if (clients[i].clientId != -1 && clients[i].clientId != clientId) {
                        msgsnd(clients[i].msgQueueId, &request, sizeof(request) - sizeof(long), 0);
                    }
                }
                break;
            default:
                printf("Invalid request type received from Client %d\n", clientId);
                break;
        }
    }
}

int main() {
    int clientId = 0;
    struct message connectRequest;
    pthread_t threads[maxClients];

    // Create message queue
    msgQueueId = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
    if (msgQueueId == -1) {
        perror("Failed to create message queue");
        exit(1);
    }

    printf("Server started\n");

    while (1) {
        // Wait for connection request
        msgrcv(msgQueueId, &connectRequest, sizeof(connectRequest) - sizeof(long), 0, 0);

        // Check if the maximum number of clients has been reached
        if (clientCount == maxClients) {
            printf("Max client limit reached. Connection request from Client %d rejected\n", clientId);
            continue;
        }

        pthread_mutex_lock(&clientsMutex);

        // Find an available slot for the new client
        while (clients[clientId].clientId != -1) {
            clientId = (clientId + 1) % maxClients;
        }

        // Add the new client to the list
        clients[clientId].clientId = clientId;
        clients[clientId].msgQueueId = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
        clientCount++;

        pthread_mutex_unlock(&clientsMutex);

        // Send the client ID and message queue ID to the client
        struct message connectResponse;
        connectResponse.mtype = clientId;
        connectResponse.clientId = clientId;
        connectResponse.requestType = requestConnect;
        snprintf(connectResponse.mtext, sizeof(connectResponse.mtext), "Connected to server. Your ID is %d", clientId);
        msgsnd(clients[clientId].msgQueueId, &connectResponse, sizeof(connectResponse) - sizeof(long), 0);

        // Create a new thread to handle the client
        pthread_create(&threads[clientId], NULL, client_thread, (void*)&clientId);
    }

    // Cleanup
    for (int i = 0; i < maxClients; i++) {
        if (clients[i].clientId != -1) {
            pthread_join(threads[i], NULL);
            msgctl(clients[i].msgQueueId, IPC_RMID, NULL);
        }
    }
    msgctl(msgQueueId, IPC_RMID, NULL);

    return 0;
}

