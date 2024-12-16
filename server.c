#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <ctype.h>
#include "logic.h"
#include "cJSON.h"
 
#define PORT 8080
#define MAX_CLIENTS 100
 
Player players[MAX_CLIENTS];
int player_count = 0;
int next_player_id = 1; // Unique ID generator
 
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER; // Mutex to protect player data
pthread_mutex_t command_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t command_cond = PTHREAD_COND_INITIALIZER;
int start_game_flag = 0;  // Flag to indicate when START_GAME is triggered
int question_counter = 0; // Counter for generating unique question IDs
 
void broadcast(const char *message)
{
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < player_count; i++)
    {
        if (players[i].logged_in)
        {
            send(players[i].socket, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&clients_lock);
}
 
void send_start_game_message()
{
    cJSON *start_message = cJSON_CreateObject();
    cJSON_AddStringToObject(start_message, "type", "Start_Game");
 
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message", "Tro choi bat dau");
    cJSON_AddItemToObject(start_message, "data", data);
 
    char *start_str = cJSON_PrintUnformatted(start_message);
 
    broadcast(start_str);
 
    free(start_str);
    cJSON_Delete(start_message);
}
 
void broadcast_question(int question_id)
{
    cJSON *original_question = get_question_by_id(question_id);
    if (!original_question)
    {
        printf("Question with ID %d not found.\n", question_id);
        return;
    }
 
    // Create a custom JSON object for broadcasting
    cJSON *broadcast_message = cJSON_CreateObject();
    cJSON_AddStringToObject(broadcast_message, "type", "Question_Broadcast");
 
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "question_id", question_id);
 
    cJSON *question_text = cJSON_GetObjectItem(original_question, "question");
    if (question_text && cJSON_IsString(question_text))
    {
        cJSON_AddStringToObject(data, "question_text", question_text->valuestring);
    }
 
    cJSON *options = cJSON_GetObjectItem(original_question, "options");
    if (options && cJSON_IsArray(options))
    {
        cJSON_AddItemToObject(data, "options", cJSON_Duplicate(options, 1)); // Deep copy the options array
    }
 
    cJSON_AddItemToObject(broadcast_message, "data", data);
 
    // Convert to string and broadcast
    char *message_str = cJSON_PrintUnformatted(broadcast_message);
    broadcast(message_str); // Use the existing `broadcast` function to send to all clients
 
    free(message_str);
    cJSON_Delete(broadcast_message);
}
 
void *game_controller(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&command_lock);
 
        // Wait for any command signal
        while (start_game_flag == 0)
        {
            pthread_cond_wait(&command_cond, &command_lock);
        }
 
        int command_flag = start_game_flag; // Capture the flag value
        start_game_flag = 0;                // Reset the flag
 
        pthread_mutex_unlock(&command_lock);
 
        if (command_flag == 1) // Start_Game message
        {
            cJSON *start_message = cJSON_CreateObject();
            cJSON_AddStringToObject(start_message, "type", "Start_Game");
 
            cJSON *data = cJSON_CreateObject();
            cJSON_AddStringToObject(data, "message", "Tro choi bat dau");
            cJSON_AddItemToObject(start_message, "data", data);
 
            char *start_str = cJSON_PrintUnformatted(start_message);
 
            // Broadcast to all players
            broadcast(start_str);
 
            free(start_str);
            cJSON_Delete(start_message);
 
            printf("Game started and message broadcasted.\n");
        }
        else if (command_flag == 2) // Send question
        {
            printf("Question broadcasted.\n");
        }
    }
    return NULL;
}
 
void *read_server_input(void *arg)
{
    while (1)
    {
        char command[20];
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0'; // Remove trailing newline
 
        if (strcmp(command, "START_GAME") == 0)
        {
            pthread_mutex_lock(&command_lock);
            start_game_flag = 1; // Set the flag for broadcasting the start message
            pthread_cond_signal(&command_cond);
            pthread_mutex_unlock(&command_lock);
        }
        else if (isdigit(command[0])) // Validate command is a number (question ID)
        {
            int question_id = atoi(command);
            broadcast_question(question_id); // Use the `broadcast_question` function
            printf("Broadcasted question ID %d\n", question_id);
        }
        else
        {
            printf("Unknown command: %s\n", command);
        }
    }
}
 
void *handle_client(void *arg)
{
    int sock = *(int *)arg;
    char buffer[1024];
    char username[50] = {0};
    bool logged_in = false;
    int player_id = -1;
 
    while (recv(sock, buffer, sizeof(buffer), 0) > 0)
    {
        printf("Received from client (%d): %s\n", sock, buffer);
 
        cJSON *request = cJSON_Parse(buffer);
        if (!request)
        {
            send(sock, "{\"type\":\"Error\",\"status\":\"failed\",\"message\":\"Invalid JSON format\"}\n", 67, 0);
            continue;
        }
 
        const char *type = cJSON_GetObjectItem(request, "type")->valuestring;
        cJSON *data = cJSON_GetObjectItem(request, "data");
 
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", type);
 
        if (strcmp(type, "Register_Request") == 0)
        {
            const char *reg_username = cJSON_GetObjectItem(data, "username")->valuestring;
            const char *reg_password = cJSON_GetObjectItem(data, "password")->valuestring;
 
            if (register_user(reg_username, reg_password))
            {
                cJSON_AddStringToObject(response, "status", "success");
                cJSON_AddStringToObject(response, "message", "Registration successful");
            }
            else
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "User already exists");
            }
        }
        else if (strcmp(type, "Login_Request") == 0)
        {
            const char *login_username = cJSON_GetObjectItem(data, "username")->valuestring;
            const char *login_password = cJSON_GetObjectItem(data, "password")->valuestring;
 
            if (authenticate_user(login_username, login_password))
            {
                logged_in = true;
                strcpy(username, login_username);
 
                pthread_mutex_lock(&clients_lock);
                players[player_count].socket = sock;
                strcpy(players[player_count].username, username);
                players[player_count].logged_in = true;
                players[player_count].score = 0;
                player_count++;
                pthread_mutex_unlock(&clients_lock);
 
                cJSON_AddStringToObject(response, "status", "success");
                cJSON_AddStringToObject(response, "message", "Login successful");
            }
            else
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "Invalid credentials");
            }
        }
        else if (strcmp(type, "Logout_Request") == 0)
        {
            if (logged_in)
            {
                logged_in = false;
                char temp_username[50];
                strcpy(temp_username, username); // Preserve username for lookup
                memset(username, 0, sizeof(username));
 
                pthread_mutex_lock(&clients_lock);
                for (int i = 0; i < player_count; i++)
                {
                    if (strcmp(players[i].username, temp_username) == 0)
                    {
                        for (int j = i; j < player_count - 1; j++)
                        {
                            players[j] = players[j + 1];
                        }
                        player_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&clients_lock);
 
                cJSON_AddStringToObject(response, "status", "success");
                cJSON_AddStringToObject(response, "message", "Logout successful");
            }
            else
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "Not logged in");
            }
        }
        else if (strcmp(type, "Answer_Request") == 0)
        {
            if (logged_in)
            {
                int question_id = cJSON_GetObjectItem(data, "question_id")->valueint;
                int selected_option = cJSON_GetObjectItem(data, "answer")->valueint;
 
                process_answer(sock, question_id, selected_option, username, players, player_count);
                cJSON_AddStringToObject(response, "status", "success");
                cJSON_AddStringToObject(response, "message", "Answer processed");
            }
            else
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "Not logged in");
            }
        }
        else
        {
            cJSON_AddStringToObject(response, "status", "failed");
            cJSON_AddStringToObject(response, "message", "Unknown command");
        }
 
        char *response_string = cJSON_PrintUnformatted(response);
        send(sock, response_string, strlen(response_string), 0);
 
        free(response_string);
        cJSON_Delete(response);
        cJSON_Delete(request);
        memset(buffer, 0, sizeof(buffer));
    }
 
    // Cleanup on disconnect
    if (logged_in)
    {
        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < player_count; i++)
        {
            if (players[i].socket == sock)
            {
                for (int j = i; j < player_count - 1; j++)
                {
                    players[j] = players[j + 1];
                }
                player_count--;
                break;
            }
        }
        pthread_mutex_unlock(&clients_lock);
    }
 
    printf("Client (%d) disconnected.\n", sock);
    close(sock);
    return NULL;
}
 
int main()
{
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
 
    if (!load_questions())
    {
        fprintf(stderr, "Failed to load questions. Exiting...\n");
        exit(EXIT_FAILURE);
    }
 
    if (server_socket == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
 
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
 
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
 
    listen(server_socket, MAX_CLIENTS);
    printf("Server listening on port %d\n", PORT);
 
    pthread_t input_thread;
    pthread_create(&input_thread, NULL, read_server_input, NULL);
 
    pthread_t game_thread;
    pthread_create(&game_thread, NULL, game_controller, NULL);
 
    while ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len)))
    {
        printf("New client connected: %d\n", client_socket);
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, (void *)&client_socket);
    }
 
    close(server_socket);
    return 0;
}
