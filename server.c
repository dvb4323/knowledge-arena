#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <ctype.h>
#include <time.h>
#include "logic.h"
#include "cJSON.h"

#define PORT 8081
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

// Player players[MAX_CLIENTS];
// int player_count = 0;
// int next_player_id = 1; // Unique ID generator

cJSON *players_data = NULL;
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER; // Mutex to protect player data
pthread_mutex_t command_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t command_cond = PTHREAD_COND_INITIALIZER;
int start_game_flag = 0; // Flag to indicate when START_GAME is triggered

void broadcast(const char *message)
{
    int sockets[100]; // Assuming a max of 100 clients
    int count = 0;

    pthread_mutex_lock(&clients_lock);

    // Fetch all logged-in clients
    get_logged_in_clients(sockets, &count);

    for (int i = 0; i < count; i++)
    {
        int bytes_sent = send(sockets[i], message, strlen(message), 0);
        if (bytes_sent < 0)
        {
            perror("Broadcast send failed");
        }
    }

    pthread_mutex_unlock(&clients_lock);
}

// void broadcast_question(int question_id)
// {
//     cJSON *original_question = get_question_by_id(question_id);
//     if (!original_question)
//     {
//         printf("Question with ID %d not found.\n", question_id);
//         return;
//     }

//     // Get the current timestamp as the question start time
//     time_t current_time = time(NULL);
//     if (current_time == -1)
//     {
//         printf("Error: Unable to get the current time.\n");
//         return;
//     }

//     // Update the start time for all players in players.json

//     // Create a custom JSON object for broadcasting
//     cJSON *broadcast_message = cJSON_CreateObject();
//     cJSON_AddStringToObject(broadcast_message, "type", "Question_Broadcast");

//     cJSON *data = cJSON_CreateObject();
//     cJSON_AddNumberToObject(data, "question_id", question_id);

//     cJSON *question_text = cJSON_GetObjectItem(original_question, "question");
//     if (question_text && cJSON_IsString(question_text))
//     {
//         cJSON_AddStringToObject(data, "question_text", question_text->valuestring);
//     }

//     cJSON *options = cJSON_GetObjectItem(original_question, "options");
//     if (options && cJSON_IsArray(options))
//     {
//         cJSON_AddItemToObject(data, "options", cJSON_Duplicate(options, 1)); // Deep copy the options array
//     }

//     cJSON_AddItemToObject(broadcast_message, "data", data);

//     // Convert to string and broadcast
//     char *message_str = cJSON_PrintUnformatted(broadcast_message);
//     broadcast(message_str); // Use the existing `broadcast` function to send to all clients

//     free(message_str);
//     cJSON_Delete(broadcast_message);
// }

// Send questions
void broadcast_question(int question_id)
{
    // Load the question
    cJSON *original_question = get_question_by_id(question_id);
    if (!original_question)
    {
        printf("Question with ID %d not found.\n", question_id);
        return;
    }

    // Read players.json manually
    FILE *file = fopen(PLAYERS_FILE, "r");
    if (!file)
    {
        perror("Failed to open players.json");
        return;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = (char *)malloc(length + 1);
    if (!content)
    {
        fclose(file);
        printf("Memory allocation error.\n");
        return;
    }

    fread(content, 1, length, file);
    fclose(file);
    content[length] = '\0';

    cJSON *players_data = cJSON_Parse(content);
    free(content);
    if (!players_data || !cJSON_IsArray(players_data))
    {
        printf("Error: Players data not found or invalid.\n");
        cJSON_Delete(players_data);
        return;
    }

    // Get current timestamp
    time_t current_time = time(NULL);

    // Update or add question_start_time for logged-in and non-eliminated players
    cJSON *player;
    cJSON_ArrayForEach(player, players_data)
    {
        cJSON *logged_in = cJSON_GetObjectItem(player, "logged_in");
        cJSON *eliminated = cJSON_GetObjectItem(player, "eliminated");
        if (cJSON_IsTrue(logged_in) && !cJSON_IsTrue(eliminated))
        {
            // Add or update question_start_time
            cJSON *start_time = cJSON_GetObjectItem(player, "question_start_time");
            if (!start_time) // If the field does not exist, add it
            {
                cJSON_AddNumberToObject(player, "question_start_time", (double)current_time);
            }
            else // If it exists, update it
            {
                cJSON_ReplaceItemInObject(player, "question_start_time", cJSON_CreateNumber((double)current_time));
            }
        }
    }

    // Save updated players.json (pretty-printed for readability)
    char *updated_data = cJSON_Print(players_data); // Pretty print for readability
    file = fopen(PLAYERS_FILE, "w");
    if (file)
    {
        fwrite(updated_data, 1, strlen(updated_data), file);
        fclose(file);
    }
    else
    {
        perror("Failed to write players.json");
    }

    free(updated_data);
    cJSON_Delete(players_data);

    // Create the broadcast message
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

    // Handling questions for non_eliminated
    // Broadcast the question only to non-eliminated and logged-in players
    // cJSON *player;
    // cJSON_ArrayForEach(player, players_data)
    // {
    //     // Check if the player is logged in and not eliminated
    //     cJSON *logged_in = cJSON_GetObjectItem(player, "logged_in");
    //     cJSON *eliminated = cJSON_GetObjectItem(player, "eliminated");
    //     cJSON *socket_item = cJSON_GetObjectItem(player, "socket");

    //     if (!cJSON_IsTrue(logged_in) || cJSON_IsTrue(eliminated) || !cJSON_IsNumber(socket_item))
    //     {
    //         continue; // Skip players who are not eligible
    //     }

    //     int sock = socket_item->valueint;

    //     // Ensure the socket descriptor is valid before sending
    //     if (sock > 0)
    //     {
    //         if (send(sock, message_str, strlen(message_str), 0) == -1)
    //         {
    //             perror("Failed to send question to player");
    //         }
    //     }
    // }

    // // Cleanup
    // free(message_str);
    // cJSON_Delete(broadcast_message);
    // cJSON_Delete(players_data);

    // printf("Broadcasted question ID %d to active players.\n", question_id);
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

void select_main_player_and_broadcast()
{
    // Open players.json to retrieve players' data
    FILE *file = fopen(PLAYERS_FILE, "r");
    if (!file)
    {
        perror("Failed to open players.json");
        return;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = (char *)malloc(length + 1);
    if (!content)
    {
        fclose(file);
        printf("Memory allocation error.\n");
        return;
    }

    fread(content, 1, length, file);
    fclose(file);
    content[length] = '\0';

    cJSON *players_data = cJSON_Parse(content);
    free(content);
    if (!players_data || !cJSON_IsArray(players_data))
    {
        printf("Error: Players data not found or invalid.\n");
        cJSON_Delete(players_data);
        return;
    }

    cJSON *player = NULL;
    int selected_player_id = -1;
    double min_elapsed_time = __DBL_MAX__;

    // Iterate through players to find the main player
    cJSON_ArrayForEach(player, players_data)
    {
        cJSON *logged_in = cJSON_GetObjectItem(player, "logged_in");
        cJSON *eliminated = cJSON_GetObjectItem(player, "eliminated");
        cJSON *player_id = cJSON_GetObjectItem(player, "player_id");
        cJSON *elapsed_time = cJSON_GetObjectItem(player, "elapsed_time");
        cJSON *answer_correct = cJSON_GetObjectItem(player, "answer_correct");

        if (cJSON_IsTrue(logged_in) && !cJSON_IsTrue(eliminated) &&
            cJSON_IsNumber(elapsed_time) && cJSON_IsTrue(answer_correct))
        {
            double time = elapsed_time->valuedouble;
            if (time < min_elapsed_time)
            {
                min_elapsed_time = time;
                selected_player_id = player_id->valueint;
            }
        }
    }

    if (selected_player_id != -1)
    {
        // Update players.json to mark the main player
        cJSON_ArrayForEach(player, players_data)
        {
            cJSON *player_id = cJSON_GetObjectItem(player, "player_id");
            if (player_id && player_id->valueint == selected_player_id)
            {
                cJSON_ReplaceItemInObject(player, "main_player", cJSON_CreateBool(true));
                cJSON_ReplaceItemInObject(player, "skip_count", cJSON_CreateNumber(2));
            }
            else
            {
                cJSON_ReplaceItemInObject(player, "main_player", cJSON_CreateBool(false));
            }
        }

        // Save updated players.json
        FILE *save_file = fopen(PLAYERS_FILE, "w");
        if (save_file)
        {
            char *updated_content = cJSON_Print(players_data);
            fwrite(updated_content, 1, strlen(updated_content), save_file);
            fclose(save_file);
            free(updated_content);
        }
        else
        {
            perror("Failed to save updated players.json");
            cJSON_Delete(players_data);
            return;
        }

        // Broadcast message to all clients
        cJSON *broadcast_message = cJSON_CreateObject();
        cJSON_AddStringToObject(broadcast_message, "type", "Select main player");

        cJSON *data = cJSON_CreateObject();
        char message[100];
        snprintf(message, sizeof(message), "player_id%d has been selected as the main player", selected_player_id);
        cJSON_AddStringToObject(data, "message", message);
        cJSON_AddItemToObject(broadcast_message, "data", data);

        char *message_str = cJSON_PrintUnformatted(broadcast_message);
        broadcast(message_str);

        free(message_str);
        cJSON_Delete(broadcast_message);

        printf("Main player selected: player_id%d\n", selected_player_id);
    }
    else
    {
        printf("No eligible player found to be the main player.\n");
    }

    cJSON_Delete(players_data);
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
        else if (strcmp(command, "SELECT_MAIN_PLAYER") == 0)
        {
            // Gọi hàm để chọn người chơi chính
            printf("Executing SELECT_MAIN_PLAYER command...\n");
            select_main_player_and_broadcast();
        }
        else
        {
            printf("Unknown command: %s\n", command);
        }
    }
}
void check_winner_and_broadcast()
{
    char *data = read_file(PLAYERS_FILE);
    if (!data)
    {
        perror("Failed to read players.json");
        return;
    }

    cJSON *players_data = cJSON_Parse(data);
    free(data);
    if (!players_data || !cJSON_IsArray(players_data))
    {
        printf("Error parsing players.json.\n");
        cJSON_Delete(players_data);
        return;
    }

    int active_player_count = 0;
    cJSON *winner_player = NULL;

    cJSON *player = NULL;
    cJSON_ArrayForEach(player, players_data)
    {
        cJSON *eliminated_item = cJSON_GetObjectItem(player, "eliminated");
        if (eliminated_item && !cJSON_IsTrue(eliminated_item))
        {
            // Tăng số lượng người chơi không bị loại
            active_player_count++;
            winner_player = player;
        }
    }

    if (active_player_count == 1 && winner_player)
    {
        cJSON *player_id_item = cJSON_GetObjectItem(winner_player, "player_id");
        if (player_id_item && cJSON_IsNumber(player_id_item))
        {
            // Tạo thông báo chiến thắng
            cJSON *response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "type", "Game_End");
            cJSON *data = cJSON_CreateObject();

            char winner_message[100];
            snprintf(winner_message, sizeof(winner_message), "player_id%d have a winner", player_id_item->valueint);

            cJSON_AddStringToObject(data, "message", winner_message);
            cJSON_AddNumberToObject(data, "player_id", player_id_item->valueint);
            cJSON_AddItemToObject(response, "data", data);

            char *response_str = cJSON_PrintUnformatted(response);
            printf("Broadcasting winner: %s\n", response_str);
            broadcast(response_str);

            free(response_str);
            cJSON_Delete(response);
        }
    }

    cJSON_Delete(players_data);
}


void *handle_client(void *arg)
{
    int sock = *(int *)arg;
    char buffer[BUFFER_SIZE];
    char username[50] = {0};
    bool logged_in = false;

    memset(buffer, 0, BUFFER_SIZE);

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

        if (strcmp(type, "Register_Request") == 0)
        {
            cJSON_AddStringToObject(response, "type", "Register_Response");
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
            cJSON_AddStringToObject(response, "type", "Login_Response");
            const char *login_username = cJSON_GetObjectItem(data, "username")->valuestring;
            const char *login_password = cJSON_GetObjectItem(data, "password")->valuestring;

            // Authenticate user
            if (authenticate_user(login_username, login_password, sock))
            {
                logged_in = true;
                strcpy(username, login_username);

                // Find player_id from players.json
                int player_id = -1; // Default invalid ID
                FILE *file = fopen("players.json", "r");
                if (file)
                {
                    fseek(file, 0, SEEK_END);
                    long file_size = ftell(file);
                    fseek(file, 0, SEEK_SET);

                    char *file_content = malloc(file_size + 1);
                    fread(file_content, 1, file_size, file);
                    fclose(file);

                    file_content[file_size] = '\0'; // Null-terminate string

                    cJSON *players = cJSON_Parse(file_content);
                    free(file_content);

                    if (players)
                    {
                        cJSON *player = NULL;
                        cJSON_ArrayForEach(player, players)
                        {
                            const char *username = cJSON_GetObjectItem(player, "username")->valuestring;
                            if (strcmp(username, login_username) == 0)
                            {
                                player_id = cJSON_GetObjectItem(player, "player_id")->valueint;
                                break; // Found the user, no need to continue searching
                            }
                        }
                        cJSON_Delete(players);
                    }
                }

                // Sending Response
                cJSON_AddStringToObject(response, "status", "success");
                cJSON_AddStringToObject(response, "message", "Login successful");

                // Add player_id to the response
                cJSON_AddNumberToObject(response, "player_id", player_id);
            }
            else
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "account does not exist or the passeord is wrong");
            }
        }
        else if (strcmp(type, "Logout_Request") == 0)
        {
            cJSON_AddStringToObject(response, "type", "Logout_Response");

            if (logged_in)
            {
                logged_in = false;

                // Update the players.json file to set logged_in = false
                FILE *file = fopen("players.json", "r");
                if (!file)
                {
                    fprintf(stderr, "Failed to open players.json for reading.\n");
                    cJSON_AddStringToObject(response, "status", "failed");
                    cJSON_AddStringToObject(response, "message", "Internal error");
                    break;
                }

                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);

                char *file_content = malloc(file_size + 1);
                fread(file_content, 1, file_size, file);
                fclose(file);
                file_content[file_size] = '\0';

                cJSON *players_data = cJSON_Parse(file_content);
                free(file_content);

                if (!players_data)
                {
                    fprintf(stderr, "Failed to parse players.json.\n");
                    cJSON_AddStringToObject(response, "status", "failed");
                    cJSON_AddStringToObject(response, "message", "Internal error");
                    break;
                }

                // Update the 'logged_in' field for the logged-out player
                cJSON *player = NULL;
                cJSON_ArrayForEach(player, players_data)
                {
                    cJSON *name = cJSON_GetObjectItem(player, "username");
                    if (name && strcmp(name->valuestring, username) == 0)
                    {
                        // Set logged_in = false
                        cJSON_ReplaceItemInObject(player, "logged_in", cJSON_CreateBool(false));
                        // Remove 'socket' field
                        cJSON_DeleteItemFromObject(player, "socket");
                        break;
                    }
                }

                // Save updated data back to file
                FILE *save_file = fopen("players.json", "w");
                if (save_file)
                {
                    char *updated_content = cJSON_Print(players_data);
                    fwrite(updated_content, 1, strlen(updated_content), save_file);
                    fclose(save_file);
                    free(updated_content);
                }
                else
                {
                    fprintf(stderr, "Failed to save updated players data.\n");
                    cJSON_Delete(players_data);
                    cJSON_AddStringToObject(response, "status", "failed");
                    cJSON_AddStringToObject(response, "message", "Internal error");
                    break;
                }

                cJSON_Delete(players_data);

                // Clear username in memory
                memset(username, 0, sizeof(username));

                // Send success response
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
            cJSON *response = cJSON_CreateObject(); // Đảm bảo luôn tạo mới response
            if (!data || !cJSON_IsObject(data))
            {
                printf("Invalid 'data' in Answer_Request.\n");
                cJSON_AddStringToObject(response, "type", "Answer_Response");
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "Invalid request format.");

                char *response_str = cJSON_PrintUnformatted(response);
                if (response_str)
                {
                    send(sock, response_str, strlen(response_str), 0);
                    free(response_str);
                }
                cJSON_Delete(response);
                return NULL;
            }

            cJSON *player_id_item = cJSON_GetObjectItem(data, "player_id");
            cJSON *question_id_item = cJSON_GetObjectItem(data, "question_id");
            cJSON *answer_item = cJSON_GetObjectItem(data, "answer");
            cJSON *answer_time_item = cJSON_GetObjectItem(data, "timestamp");

            if (player_id_item && cJSON_IsNumber(player_id_item) &&
                question_id_item && cJSON_IsNumber(question_id_item) &&
                answer_item && cJSON_IsNumber(answer_item) &&
                answer_time_item && cJSON_IsNumber(answer_time_item))
            {
                int player_id = player_id_item->valueint;
                int question_id = question_id_item->valueint;
                int selected_option = answer_item->valueint;
                int answer_time = answer_time_item->valueint;

                // Cập nhật và ghi vào file
                validate_answer(player_id, question_id, selected_option, answer_time);

                // Chờ 1 giây để đảm bảo file được ghi đầy đủ
                sleep(1);

                // Đọc lại players.json để kiểm tra answer_correct
                char *data = read_file(PLAYERS_FILE);
                if (data)
                {
                    cJSON *players_data = cJSON_Parse(data);
                    free(data);

                    cJSON *player = NULL;
                    cJSON_ArrayForEach(player, players_data)
                    {
                        cJSON *id = cJSON_GetObjectItem(player, "player_id");
                        if (id && id->valueint == player_id)
                        {
                            cJSON *answer_correct_item = cJSON_GetObjectItem(player, "answer_correct");
                            if (answer_correct_item && cJSON_IsBool(answer_correct_item) && cJSON_IsTrue(answer_correct_item))
                            {
                                cJSON_AddStringToObject(response, "type", "Answer_Response");
                                cJSON_AddStringToObject(response, "status", "success");
                                cJSON_AddStringToObject(response, "message", "Correct answer! Well done.");
                            }
                            else
                            {
                                cJSON_AddStringToObject(response, "type", "Answer_Response");
                                cJSON_AddStringToObject(response, "status", "failed");
                                cJSON_AddStringToObject(response, "message", "Wrong answer! You have been eliminated.");
                            }
                            break;
                        }
                    }

                    cJSON_Delete(players_data);
                }
                else
                {
                    cJSON_AddStringToObject(response, "status", "failed");
                    cJSON_AddStringToObject(response, "message", "Error reading players data.");
                }
            }
            else
            {
                cJSON_AddStringToObject(response, "type", "Answer_Response");
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "Invalid request format.");
            }

            check_winner_and_broadcast();

            // Gửi phản hồi lại cho client
            char *response_str = cJSON_PrintUnformatted(response);
            if (response_str)
            {
                send(sock, response_str, strlen(response_str), 0);
                free(response_str);
            }
            cJSON_Delete(response);
        }

        // skippppp
        else if (strcmp(type, "Skip_Request") == 0)
        {
            cJSON *response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "type", "Skip_Response");

            // Kiểm tra người chơi chính và skip_count
            FILE *file = fopen("players.json", "r");
            if (!file)
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "Could not access player data");
                char *response_string = cJSON_PrintUnformatted(response);
                send(sock, response_string, strlen(response_string), 0);
                free(response_string);
                cJSON_Delete(response);
                return NULL;
            }

            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            rewind(file);

            char *file_content = malloc(file_size + 1);
            fread(file_content, 1, file_size, file);
            fclose(file);
            file_content[file_size] = '\0';

            cJSON *players_data = cJSON_Parse(file_content);
            free(file_content);

            if (!players_data || !cJSON_IsArray(players_data))
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "Invalid player data");
                char *response_string = cJSON_PrintUnformatted(response);
                send(sock, response_string, strlen(response_string), 0);
                free(response_string);
                cJSON_Delete(response);
                return NULL;
            }

            // Tìm người chơi chính
            cJSON *main_player = NULL;
            cJSON *player = NULL;

            // Duyệt qua danh sách người chơi để tìm main_player
            cJSON_ArrayForEach(player, players_data)
            {
                cJSON *is_main_player = cJSON_GetObjectItem(player, "main_player");

                // Nếu tìm thấy main_player
                if (cJSON_IsTrue(is_main_player))
                {
                    main_player = player;
                    break;
                }
            }
            if (!main_player)
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "Only the main player can skip questions.");
                send(sock, cJSON_PrintUnformatted(response), strlen(cJSON_PrintUnformatted(response)), 0);
                cJSON_Delete(response);
                return NULL;
            }

            // Kiểm tra skip_count
            cJSON *skip_count_item = cJSON_GetObjectItem(main_player, "skip_count");
            if (!skip_count_item || skip_count_item->valueint <= 0)
            {
                cJSON_AddStringToObject(response, "status", "failed");
                cJSON_AddStringToObject(response, "message", "No skip attempts remaining");
                char *response_string = cJSON_PrintUnformatted(response);
                send(sock, response_string, strlen(response_string), 0);
                free(response_string);
                cJSON_Delete(response);
                cJSON_Delete(players_data);
                return NULL;
            }

            // Giảm skip_count
            cJSON_ReplaceItemInObject(main_player, "skip_count", cJSON_CreateNumber(skip_count_item->valueint - 1));

            // Lưu vào players.json
            FILE *save_file = fopen("players.json", "w");
            if (save_file)
            {
                char *updated_data = cJSON_Print(players_data);
                fwrite(updated_data, 1, strlen(updated_data), save_file);
                fclose(save_file);
                free(updated_data);
            }

            cJSON *updated_skip_count_item = cJSON_GetObjectItem(main_player, "skip_count");
            if (updated_skip_count_item && cJSON_IsNumber(updated_skip_count_item))
            {
                cJSON_AddStringToObject(response, "status", "success");
                cJSON_AddNumberToObject(response, "skip_count_remaining", updated_skip_count_item->valueint);
            }

            char *response_string = cJSON_PrintUnformatted(response);
            send(sock, response_string, strlen(response_string), 0);

            free(response_string);
            cJSON_Delete(response);
            cJSON_Delete(players_data);
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

    // Load players data
    if (!load_players())
    {
        fprintf(stderr, "Failed to load players\n");
        return 1;
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
