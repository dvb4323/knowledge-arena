#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "cJSON.h" // Include thư viện cJSON

#define SERVER_IP "127.0.0.1"
#define PORT 8080

// Function to send a request to the server
void send_request(int sock, const char *type, const char *username, const char *password)
{
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "type", type);

    if (username != NULL && password != NULL)
    {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "username", username);
        cJSON_AddStringToObject(data, "password", password);
        cJSON_AddItemToObject(request, "data", data);
    }

    char *json_message = cJSON_PrintUnformatted(request);
    printf("Sent to server: %s\n", json_message);
    send(sock, json_message, strlen(json_message), 0);

    free(json_message);
    cJSON_Delete(request);
}

// Thread to listen for messages from the server
void *listen_to_server(void *arg)
{
    int sock = *(int *)arg;
    char buffer[1024];

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0)
        {
            buffer[bytes_received] = '\0';
            printf("\nMessage from server: %s\n", buffer);
        }
        else if (bytes_received == 0)
        {
            printf("Server disconnected.\n");
            break;
        }
        else
        {
            perror("recv failed");
            break;
        }
    }
    return NULL;
}

int main()
{
    int sock;
    struct sockaddr_in server_addr;
    char command[20], username[50], password[50];
    pthread_t listener_thread;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("Could not create socket");
        return 1;
    }

    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    printf("Connected to server\n");

    // Start the listener thread to continuously receive server messages
    if (pthread_create(&listener_thread, NULL, listen_to_server, &sock) != 0)
    {
        perror("Failed to create listener thread");
        close(sock);
        return 1;
    }

    // Main loop to interact with the server
    while (1)
    {
        printf("Enter command (REGISTER/LOGIN/LOGOUT): ");
        scanf("%s", command);

        // Handle commands
        if (strcmp(command, "REGISTER") == 0 || strcmp(command, "LOGIN") == 0)
        {
            printf("Enter username: ");
            scanf("%s", username);
            printf("Enter password: ");
            scanf("%s", password);

            send_request(sock, strcmp(command, "REGISTER") == 0 ? "Register_Request" : "Login_Request", username, password);
        }
        else if (strcmp(command, "LOGOUT") == 0)
        {
            send_request(sock, "Logout_Request", NULL, NULL);
        }
        else
        {
            printf("Invalid command. Please try again.\n");
            continue;
        }
    }

    // Cleanup
    pthread_cancel(listener_thread);
    pthread_join(listener_thread, NULL);
    close(sock);
    return 0;
}
