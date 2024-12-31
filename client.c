#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <time.h>
#include "cJSON.h"

#define SERVER_IP "127.0.0.1"
#define PORT 8080

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

    // Get the current timestamp (Unix time)
    time_t current_time = time(NULL);
    if (current_time == -1)
    {
        printf("Error: Unable to get the current time.\n");
        return;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "Answer_Request");

    // Prepare the data object
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "player_id", player_id);
    cJSON_AddNumberToObject(data, "question_id", question_id);
    cJSON_AddNumberToObject(data, "answer", answer);

    // Add the timestamp to the data object
    cJSON_AddNumberToObject(data, "timestamp", (int)current_time);

    cJSON_AddItemToObject(response, "data", data);

    // Send the answer request to the server
    char *json_message = cJSON_PrintUnformatted(response);
    send(sock, json_message, strlen(json_message), 0);
    printf("Sent to server: %s\n", json_message);

    // Clean up
    free(json_message);
    cJSON_Delete(response);
}

// Function to send a skip request to the server
void send_skip_request(int sock)
{
    cJSON *skip_request = cJSON_CreateObject();
    cJSON_AddStringToObject(skip_request, "type", "Skip_Request");
    char *request_string = cJSON_PrintUnformatted(skip_request);
    send(sock, request_string, strlen(request_string), 0);
    printf("Sent skip request to server.\n");
    free(request_string);
    cJSON_Delete(skip_request);
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
        if (!data || !cJSON_IsObject(data))
        {
            printf("Invalid or missing data in Question_Broadcast.\n");
            cJSON_Delete(message);
            return;
        }

        cJSON *question_id_item = cJSON_GetObjectItem(data, "question_id");
        cJSON *question_text_item = cJSON_GetObjectItem(data, "question_text");
        cJSON *options = cJSON_GetObjectItem(data, "options");

        if (!question_id_item || !cJSON_IsNumber(question_id_item) ||
            !question_text_item || !cJSON_IsString(question_text_item) ||
            !options || !cJSON_IsArray(options))
        {
            printf("Invalid question data in Question_Broadcast.\n");
            cJSON_Delete(message);
            return;
        }

        int new_question_id = question_id_item->valueint;

        if (current_question_id == new_question_id)
        {
            printf("Duplicate question received. Ignoring.\n");
            cJSON_Delete(message);
            return;
        }

        current_question_id = new_question_id;
        const char *question_text = question_text_item->valuestring;

        printf("Question ID: %d\n", current_question_id);
        printf("Question: %s\n", question_text);
        for (int i = 0; i < cJSON_GetArraySize(options); i++)
        {
            cJSON *option = cJSON_GetArrayItem(options, i);
            if (option && cJSON_IsString(option))
            {
                printf("%d. %s\n", i + 1, option->valuestring);
            }
        }
        waiting_for_answer = 1;
    }
    else if (strcmp(type, "Answer_Response") == 0)
    {
        cJSON *status_item = cJSON_GetObjectItem(message, "status");
        cJSON *message_item = cJSON_GetObjectItem(message, "message");
        cJSON *score_item = cJSON_GetObjectItem(message, "score");

        if (!status_item || !cJSON_IsString(status_item))
        {
            printf("Invalid 'status' in Answer_Response.\n");
            cJSON_Delete(message);
            return;
        }

        const char *status = status_item->valuestring;
        printf("Answer status: %s\n", status);

        if (message_item && cJSON_IsString(message_item))
        {
            printf("%s\n", message_item->valuestring);
        }

        if (score_item && cJSON_IsNumber(score_item))
        {
            printf("Your current Score: %d\n", score_item->valueint);
        }

        if (strcmp(status, "success") == 0)
        {
            waiting_for_answer = 0; // Reset state
        }
    }

    else if (strcmp(type, "Skip_Response") == 0)
    {
        const char *status = cJSON_GetObjectItem(message, "status")->valuestring;
        if (strcmp(status, "success") == 0)
        {
            int remaining_skips = cJSON_GetObjectItem(message, "skip_count_remaining")->valueint;
            printf("Skip successful. Skip count left: %d\n", remaining_skips);
        }
        else
        {
            const char *message_text = cJSON_GetObjectItem(message, "message")->valuestring;
            printf("Skip failed: %s\n", message_text ? message_text : "Unknown error.");
        }
    }
    else if (strcmp(type, "Game_End") == 0)
    {
        cJSON *data = cJSON_GetObjectItem(message, "data");
        const char *message_text = cJSON_GetObjectItem(data, "message")->valuestring;
        int winner_id = cJSON_GetObjectItem(data, "player_id")->valueint;

        printf("Game over: %s\n", message_text);
        printf("Winner is player_id%d\n", winner_id);

        if (winner_id == player_id)
        {
            printf("Congratulations! You are the winner!\n");
        }
        else
        {
            printf("Better luck next time!\n");
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
            char command[20]; // Khai báo biến command trong vòng lặp
            if (waiting_for_answer)
            {
                printf("Enter your answer: ");
                scanf("%s", command);

                if (strcmp(command, "SKIP") == 0)
                {
                    send_skip_request(sock);
                }
                else
                {
                    int answer = atoi(command);
                    if (answer > 0)
                    {
                        send_answer(sock, current_question_id, answer);
                    }
                    else
                    {
                        printf("Invalid input. Please enter a number.\n");
                    }
                }
            }
            else
            {
                printf("Enter command (REGISTER/LOGIN/LOGOUT): ");
                scanf("%s", command);

                if (strcmp(command, "REGISTER") == 0 || strcmp(command, "LOGIN") == 0)
                {
                    char username[50], password[50];
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
                else if (strcmp(command, "SKIP") == 0)
                {
                    send_skip_request(sock);
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
