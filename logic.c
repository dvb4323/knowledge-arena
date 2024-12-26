#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/stat.h>
#include "logic.h"
#include "cJSON.h"

#define BUFFER_SIZE 1024

// Static variables for storing data
static cJSON *questions = NULL;
static cJSON *players_data = NULL;

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
    cJSON_AddNumberToObject(new_player, "score", 0);
    cJSON_AddBoolToObject(new_player, "logged_in", false);
    cJSON_AddBoolToObject(new_player, "eliminated", false);
    cJSON_AddNumberToObject(new_player, "player_id", cJSON_GetArraySize(players_data) + 1);

    cJSON_AddItemToArray(players_data, new_player);

    return save_players();
}

// Authenticate a user
bool authenticate_user(const char *username, const char *password)
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

// Validate player answer
bool validate_answer(int question_id, int selected_option, int player_id)
{
    cJSON *player = get_player_by_id(player_id);
    if (!player || cJSON_IsTrue(cJSON_GetObjectItem(player, "eliminated")))
    {
        return false;
    }

    cJSON *question = get_question_by_id(question_id);
    if (!question)
    {
        return false;
    }

    int correct_option = cJSON_GetObjectItem(question, "correct_option")->valueint;
    if (selected_option == correct_option)
    {
        int score = cJSON_GetObjectItem(player, "score")->valueint;
        cJSON_ReplaceItemInObject(player, "score", cJSON_CreateNumber(score + 10));
    }
    else
    {
        cJSON_ReplaceItemInObject(player, "eliminated", cJSON_CreateBool(true));
    }

    save_players();
    cJSON_Delete(question);
    return true;
}
