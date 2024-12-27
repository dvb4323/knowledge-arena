#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include "cJSON.h"

#define SERVER_IP "127.0.0.1"
#define PORT 8081

int player_id = -1;
int waiting_for_answer = 0;
int current_question_id = -1;

// Function to send a JSON request to the server
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
    send(sock, json_message, strlen(json_message), 0);
    printf("Sent to server: %s\n", json_message);

    free(json_message);
    cJSON_Delete(request);
}

// Function to send an answer to the server
void send_answer(int sock, int question_id, int answer)
{
    if (player_id <= 0)
    {
        printf("Error: Invalid player ID (%d). Cannot send answer.\n", player_id);
        return;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "Answer_Request");

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "player_id", player_id);
    cJSON_AddNumberToObject(data, "question_id", question_id);
    cJSON_AddNumberToObject(data, "answer", answer);
    cJSON_AddItemToObject(response, "data", data);

    char *json_message = cJSON_PrintUnformatted(response);
    send(sock, json_message, strlen(json_message), 0);
    printf("Sent to server: %s\n", json_message);

    free(json_message);
    cJSON_Delete(response);
}

// Function to process server messages
void process_server_message(char *buffer)
{
    cJSON *message = cJSON_Parse(buffer);
    if (!message)
    {
        printf("Invalid JSON message from server.\n");
        return;
    }

    const char *type = cJSON_GetObjectItem(message, "type")->valuestring;

    if (strcmp(type, "Login_Response") == 0)
    {
        const char *status = cJSON_GetObjectItem(message, "status")->valuestring;
        if (strcmp(status, "success") == 0)
        {
            // Ensure player_id is extracted as an integer
            player_id = cJSON_GetObjectItem(message, "player_id")->valueint;
            printf("Login successful. Player ID: %d\n", player_id);
        }
        else
        {
            printf("Login failed: %s\n", cJSON_GetObjectItem(message, "message")->valuestring);
        }
    }
    else if (strcmp(type, "Question_Broadcast") == 0)
    {
        cJSON *data = cJSON_GetObjectItem(message, "data");
        int new_question_id = cJSON_GetObjectItem(data, "question_id")->valueint;

        if (current_question_id == new_question_id)
        {
            printf("Duplicate question received. Ignoring.\n");
            cJSON_Delete(message);
            return;
        }

        current_question_id = new_question_id;
        const char *question_text = cJSON_GetObjectItem(data, "question_text")->valuestring;
        cJSON *options = cJSON_GetObjectItem(data, "options");

        printf("Question ID: %d\n", current_question_id);
        printf("Question: %s\n", question_text);
        for (int i = 0; i < cJSON_GetArraySize(options); i++)
        {
            printf("%d. %s\n", i + 1, cJSON_GetArrayItem(options, i)->valuestring);
        }
        waiting_for_answer = 1;
    }
    else if (strcmp(type, "Answer_Response") == 0)
    {
        const char *status = cJSON_GetObjectItem(message, "status")->valuestring;
        printf("Answer status: %s\n", status);
        if (strcmp(status, "success") == 0)
        {
            waiting_for_answer = 0; // Reset state
        }
    }

    cJSON_Delete(message);
}

int main()
{
    int sock;
    struct sockaddr_in server_addr;
    fd_set read_fds;
    char buffer[1024];

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

    // Set socket to non-blocking mode
    fcntl(sock, F_SETFL, O_NONBLOCK);

    // Main loop
    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("select error");
            break;
        }

        // Check if there is data from the server
        if (FD_ISSET(sock, &read_fds))
        {
            memset(buffer, 0, sizeof(buffer)); // Clear the buffer before receiving new data
            int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received > 0)
            {
                buffer[bytes_received] = '\0';
                printf("\nMessage from server: %s\n", buffer);
                process_server_message(buffer);
            }
            else if (bytes_received == 0)
            {
                printf("Server disconnected.\n");
                break;
            }
        }

        // Check if there is user input
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            if (waiting_for_answer)
            {
                int answer;
                if (scanf("%d", &answer) == 1)
                {
                    send_answer(sock, current_question_id, answer);
                }
                else
                {
                    printf("Invalid input. Please enter a number.\n");
                    while (getchar() != '\n')
                        ; // Clear input buffer
                }
            }
            else
            {
                char command[20], username[50], password[50];
                printf("Enter command (REGISTER/LOGIN/LOGOUT): ");
                scanf("%s", command);

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
                    printf("Invalid command.\n");
                }
            }
        }
    }

    close(sock);
    return 0;
}