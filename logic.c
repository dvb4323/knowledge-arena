#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/socket.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "logic.h"
#include "cJSON.h"

#define BUFFER_SIZE 1024

// Static variables for storing data
static cJSON *questions = NULL;
extern cJSON *players_data;
extern int total_answers_received;
extern int total_active_players;
extern pthread_mutex_t answer_mutex;

// Read entire file into memory
char *read_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Failed to open file");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *data = malloc(size + 1);
    if (!data)
    {
        fclose(file);
        perror("Failed to allocate memory");
        return NULL;
    }

    fread(data, 1, size, file);
    data[size] = '\0';
    fclose(file);

    return data;
}

// Write data into a file
bool write_file(const char *filename, const char *data)
{
    FILE *file = fopen(filename, "w");
    if (!file)
    {
        perror("Failed to open file for writing");
        return false;
    }

    fwrite(data, 1, strlen(data), file);
    fclose(file);
    return true;
}

// Function to get logged-in clients
void get_logged_in_clients(int *sockets, int *count)
{
    *count = 0;

    FILE *file = fopen("players.json", "r");
    if (!file)
    {
        fprintf(stderr, "Failed to open players.json for reading.\n");
        return;
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
        return;
    }

    cJSON *player = NULL;
    cJSON_ArrayForEach(player, players_data)
    {
        cJSON *logged_in = cJSON_GetObjectItem(player, "logged_in");
        cJSON *socket = cJSON_GetObjectItem(player, "socket");

        if (logged_in && cJSON_IsTrue(logged_in) &&
            socket && cJSON_IsNumber(socket))
        {
            sockets[*count] = socket->valueint;
            (*count)++;
        }
    }

    cJSON_Delete(players_data);
}

// Load questions
bool load_questions()
{
    char *data = read_file(QUESTIONS_FILE);
    if (!data)
        return false;

    questions = cJSON_Parse(data);
    free(data);

    if (!questions || !cJSON_IsArray(questions))
    {
        fprintf(stderr, "Failed to parse questions JSON\n");
        return false;
    }

    return true;
}

// Load or initialize players data
bool load_players()
{
    char *data = read_file(PLAYERS_FILE);
    if (data)
    {
        players_data = cJSON_Parse(data);
        free(data);

        if (!players_data || !cJSON_IsArray(players_data))
        {
            fprintf(stderr, "Invalid players.json format\n");
            players_data = cJSON_CreateArray();
        }
    }
    else
    {
        players_data = cJSON_CreateArray();
    }

    return true;
}

// Save players data without mutex
bool save_players()
{
    FILE *file = fopen(PLAYERS_FILE, "r");
    cJSON *existing_data = NULL;

    // Load existing data
    if (file)
    {
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        rewind(file);

        char *buffer = malloc(file_size + 1);
        fread(buffer, 1, file_size, file);
        buffer[file_size] = '\0';
        fclose(file);

        existing_data = cJSON_Parse(buffer);
        free(buffer);

        if (!existing_data || !cJSON_IsArray(existing_data))
        {
            existing_data = cJSON_CreateArray();
        }
    }
    else
    {
        existing_data = cJSON_CreateArray();
    }

    // Update or add entries without duplication
    cJSON *player = NULL;
    cJSON *existing_player = NULL;

    cJSON_ArrayForEach(player, players_data)
    {
        bool found = false;

        cJSON_ArrayForEach(existing_player, existing_data)
        {
            cJSON *existing_username = cJSON_GetObjectItem(existing_player, "username");
            cJSON *player_username = cJSON_GetObjectItem(player, "username");

            if (existing_username && player_username &&
                strcmp(existing_username->valuestring, player_username->valuestring) == 0)
            {
                // Update the existing player
                cJSON_ReplaceItemInObject(existing_player, "password",
                                          cJSON_Duplicate(cJSON_GetObjectItem(player, "password"), 1));
                cJSON_ReplaceItemInObject(existing_player, "score",
                                          cJSON_Duplicate(cJSON_GetObjectItem(player, "score"), 1));
                cJSON_ReplaceItemInObject(existing_player, "logged_in",
                                          cJSON_Duplicate(cJSON_GetObjectItem(player, "logged_in"), 1));
                cJSON_ReplaceItemInObject(existing_player, "eliminated",
                                          cJSON_Duplicate(cJSON_GetObjectItem(player, "eliminated"), 1));
                cJSON_ReplaceItemInObject(existing_player, "player_id",
                                          cJSON_Duplicate(cJSON_GetObjectItem(player, "player_id"), 1));
                found = true;
                break;
            }
        }

        // If not found, add the new player
        if (!found)
        {
            cJSON_AddItemToArray(existing_data, cJSON_Duplicate(player, 1));
        }
    }

    // Write back to the file
    file = fopen(PLAYERS_FILE, "w");
    if (!file)
    {
        perror("Failed to open players file for writing");
        cJSON_Delete(existing_data);
        return false;
    }

    char *data = cJSON_Print(existing_data);
    fwrite(data, 1, strlen(data), file);
    fclose(file);

    free(data);
    cJSON_Delete(existing_data);
    return true;
}

// Register a new user
bool register_user(const char *username, const char *password)
{
    if (!players_data)
        players_data = cJSON_CreateArray();

    // Check if username exists
    cJSON *player = NULL;
    cJSON_ArrayForEach(player, players_data)
    {
        cJSON *name = cJSON_GetObjectItem(player, "username");
        if (name && strcmp(name->valuestring, username) == 0)
        {
            return false; // Username already exists
        }
    }

    // Add new player
    cJSON *new_player = cJSON_CreateObject();
    cJSON_AddStringToObject(new_player, "username", username);
    cJSON_AddStringToObject(new_player, "password", password);
    cJSON_AddNumberToObject(new_player, "player_id", cJSON_GetArraySize(players_data) + 1);
    cJSON_AddNumberToObject(new_player, "score", 0);
    cJSON_AddBoolToObject(new_player, "main_player", false);
    cJSON_AddNumberToObject(new_player, "skip_count", 0);
    cJSON_AddBoolToObject(new_player, "logged_in", false);
    cJSON_AddBoolToObject(new_player, "eliminated", false);
    cJSON_AddNumberToObject(new_player, "elapsed_time", 0);
    cJSON_AddBoolToObject(new_player, "answer_correct", false);

    cJSON_AddItemToArray(players_data, new_player);

    return save_players();
}

// Authenticate a user
bool authenticate_user(const char *username, const char *password, int sock)
{
    // Reload the latest players data from file
    FILE *file = fopen("players.json", "r");
    if (!file)
    {
        fprintf(stderr, "Failed to open players.json for reading.\n");
        return false;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *file_content = malloc(file_size + 1);
    fread(file_content, 1, file_size, file);
    fclose(file);
    file_content[file_size] = '\0';

    // Parse the updated JSON data
    cJSON *updated_players_data = cJSON_Parse(file_content);
    free(file_content);

    if (!updated_players_data)
    {
        fprintf(stderr, "Failed to parse players.json.\n");
        return false;
    }

    cJSON *player = NULL;

    cJSON_ArrayForEach(player, updated_players_data)
    {
        cJSON *name = cJSON_GetObjectItem(player, "username");
        cJSON *pass = cJSON_GetObjectItem(player, "password");
        cJSON *logged_in = cJSON_GetObjectItem(player, "logged_in");

        // Ensure fields are valid
        if (!name || !pass || !logged_in)
        {
            continue;
        }

        if (strcmp(name->valuestring, username) == 0 && strcmp(pass->valuestring, password) == 0)
        {
            // Update 'logged_in' status
            cJSON_ReplaceItemInObject(player, "logged_in", cJSON_CreateBool(true));

            // Add 'socket' field
            cJSON_AddItemToObject(player, "socket", cJSON_CreateNumber(sock));

            // Save changes back to file
            FILE *save_file = fopen("players.json", "w");
            if (save_file)
            {
                char *updated_content = cJSON_Print(updated_players_data);
                fwrite(updated_content, 1, strlen(updated_content), save_file);
                fclose(save_file);
                free(updated_content);
            }
            else
            {
                fprintf(stderr, "Failed to save updated players data.\n");
                cJSON_Delete(updated_players_data);
                return false;
            }

            cJSON_Delete(updated_players_data); // Free memory
            return true;
        }
    }

    cJSON_Delete(updated_players_data); // Free memory
    return false;                       // Authentication failed
}

// Retrieve question by ID
cJSON *get_question_by_id(int question_id)
{
    if (!questions || !cJSON_IsArray(questions))
        return NULL;

    cJSON *question = NULL;
    cJSON_ArrayForEach(question, questions)
    {
        cJSON *id = cJSON_GetObjectItem(question, "id");
        if (id && id->valueint == question_id)
        {
            return cJSON_Duplicate(question, 1);
        }
    }

    return NULL;
}

// Get player by ID
cJSON *get_player_by_id(int id)
{
    if (!players_data || !cJSON_IsArray(players_data))
        return NULL;

    cJSON *player = NULL;
    cJSON_ArrayForEach(player, players_data)
    {
        cJSON *player_id = cJSON_GetObjectItem(player, "player_id");
        if (player_id && player_id->valueint == id)
        {
            return player;
        }
    }

    return NULL;
}

bool validate_answer(int player_id, int question_id, int selected_option, time_t received_timestamp)
{
    // pthread_mutex_lock(&answer_mutex);
    // Reload the players.json file
    FILE *file = fopen(PLAYERS_FILE, "r");
    if (!file)
    {
        perror("Failed to open players.json");
        return false;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = (char *)malloc(length + 1);
    fread(content, 1, length, file);
    fclose(file);
    content[length] = '\0';

    cJSON *players_data = cJSON_Parse(content);
    free(content);
    if (!players_data || !cJSON_IsArray(players_data))
    {
        printf("Error: Failed to parse players.json.\n");
        cJSON_Delete(players_data);
        return false;
    }

    // Find the player
    cJSON *player = NULL;
    cJSON_ArrayForEach(player, players_data)
    {
        cJSON *id = cJSON_GetObjectItem(player, "player_id");
        if (id && id->valueint == player_id)
        {
            break;
        }
    }

    if (!player)
    {
        printf("Player with ID %d not found.\n", player_id);
        cJSON_Delete(players_data);
        return false;
    }

    // Ensure player is not eliminated
    cJSON *eliminated = cJSON_GetObjectItem(player, "eliminated");
    if (cJSON_IsTrue(eliminated))
    {
        printf("Player %d is eliminated.\n", player_id);
        cJSON_Delete(players_data);
        return false;
    }

    // Validate question_start_time
    cJSON *start_time_item = cJSON_GetObjectItem(player, "question_start_time");
    if (!start_time_item || !cJSON_IsNumber(start_time_item))
    {
        printf("Start time missing for player %d.\n", player_id);
        cJSON_Delete(players_data);
        return false;
    }

    time_t start_time = (time_t)start_time_item->valuedouble;
    double elapsed_time = difftime(received_timestamp, start_time);
    printf("Elapsed time: %.2f seconds\n", elapsed_time);

    // Update elapsed_time in players.json
    cJSON_ReplaceItemInObject(player, "elapsed_time", cJSON_CreateNumber(elapsed_time));

    // Check if is main player or not
    // cJSON *mainPlayer = cJSON_GetObjectItem(player, "main_player");
    // if (cJSON_IsTrue(mainPlayer))
    // {
    //     QUESTION_TIME_LIMIT = 60;
    // }

    // Check if the answer is too late
    int time_limit = cJSON_IsTrue(cJSON_GetObjectItem(player, "main_player")) ? 60 : 30; // 60 giây cho người chơi chính
    if (elapsed_time > time_limit)
    {
        // Eliminate player for exceeding the time limit
        cJSON_ReplaceItemInObject(player, "eliminated", cJSON_CreateBool(true));
        printf("Player %d eliminated for exceeding time limit.\n", player_id);

        // Save the updated players.json file
        char *updated_data = cJSON_Print(players_data);
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
        return false;
    }

    // Validate the answer
    cJSON *question = get_question_by_id(question_id);
    if (!question)
    {
        printf("Question %d not found.\n", question_id);
        cJSON_Delete(players_data);
        return false;
    }

    int correct_option = cJSON_GetObjectItem(question, "correct_option")->valueint;
    if (selected_option == correct_option)
    {
        // Update score
        cJSON *score_item = cJSON_GetObjectItem(player, "score");
        int score = score_item->valueint + 10;
        cJSON_ReplaceItemInObject(player, "score", cJSON_CreateNumber(score));
        cJSON_ReplaceItemInObject(player, "answer_correct", cJSON_CreateBool(true));
    }
    else
    {
        // Mark player as eliminated and set answer_correct to false
        cJSON_ReplaceItemInObject(player, "eliminated", cJSON_CreateBool(true));
        cJSON_ReplaceItemInObject(player, "answer_correct", cJSON_CreateBool(false));
    }

    // Increment the counter
    total_answers_received++;
    // pthread_mutex_unlock(&answer_mutex);

    // Save the updated players.json file
    char *updated_data = cJSON_Print(players_data);
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

    return true;
}

void update_main_player_score()
{
    FILE *file = fopen("players.json", "r+");
    if (!file)
    {
        perror("Failed to open players.json");
        return;
    }

    // Read JSON data
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *data = malloc(length + 1);
    fread(data, 1, length, file);
    data[length] = '\0';

    cJSON *players = cJSON_Parse(data);
    free(data);

    if (!players)
    {
        fclose(file);
        perror("Failed to parse JSON.");
        return;
    }

    int main_player_id = -1;
    int bonus_points = 0;

    // Debug log
    printf("Calculating scores...\n");

    // Calculate bonuses based on current answer results
    for (int i = 0; i < cJSON_GetArraySize(players); i++)
    {
        cJSON *player = cJSON_GetArrayItem(players, i);

        bool is_logged_in = cJSON_IsTrue(cJSON_GetObjectItem(player, "logged_in"));
        bool is_main = cJSON_IsTrue(cJSON_GetObjectItem(player, "main_player"));
        bool is_correct = cJSON_IsTrue(cJSON_GetObjectItem(player, "answer_correct"));
        bool is_eliminated = cJSON_IsTrue(cJSON_GetObjectItem(player, "eliminated"));

        // Skip players not logged in
        if (!is_logged_in)
        {
            continue;
        }

        int player_id = cJSON_GetObjectItem(player, "player_id")->valueint;

        printf("Player %d -> correct: %d, eliminated: %d\n", player_id, is_correct, is_eliminated);

        if (is_main)
        {
            main_player_id = player_id; // Save main player ID
        }
        // else if (!is_correct && !is_eliminated) // Count wrong answers
        else if (!is_correct)
        {
            bonus_points += 10;
            printf("Player %d answered wrong. Adding 10 bonus points.\n", player_id);
        }
    }

    // Update main player's score
    for (int i = 0; i < cJSON_GetArraySize(players); i++)
    {
        cJSON *player = cJSON_GetArrayItem(players, i);
        int player_id = cJSON_GetObjectItem(player, "player_id")->valueint;

        // Update score for main player
        if (player_id == main_player_id)
        {
            int score = cJSON_GetObjectItem(player, "score")->valueint;
            cJSON_ReplaceItemInObject(player, "score", cJSON_CreateNumber(score + bonus_points));
            printf("Main player %d received bonus points: %d (Total score: %d)\n",
                   main_player_id, bonus_points, score + bonus_points);
        }

        // Reset answer_correct for all players
        // cJSON_ReplaceItemInObject(player, "answer_correct", cJSON_CreateBool(false));
    }

    // Write updated JSON data back to file
    fseek(file, 0, SEEK_SET);
    char *updated_data = cJSON_PrintUnformatted(players);
    fwrite(updated_data, 1, strlen(updated_data), file);
    ftruncate(fileno(file), strlen(updated_data)); // Trim excess content
    fclose(file);

    free(updated_data);
    cJSON_Delete(players);
}
