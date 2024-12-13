#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/socket.h>
#include "logic.h"
#include "cJSON.h"

#define QUESTIONS_FILE "questions.json"

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static cJSON *questions = NULL;

// Load questions from the JSON file into memory
bool load_questions()
{
    FILE *file = fopen(QUESTIONS_FILE, "r");
    if (!file)
    {
        perror("Failed to open questions file");
        return false;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    char *buffer = malloc(file_size + 1);
    if (!buffer)
    {
        fclose(file);
        perror("Failed to allocate memory");
        return false;
    }

    fread(buffer, 1, file_size, file);
    fclose(file);

    questions = cJSON_Parse(buffer);
    free(buffer);

    if (!questions)
    {
        perror("Failed to parse questions JSON");
        return false;
    }

    return true;
}

// Retrieve a question by its ID
cJSON *get_question_by_id(int question_id)
{
    if (question_id < 1 || question_id > 40) // Replace 40 with the actual maximum number of questions
    {
        printf("Invalid question ID: %d\n", question_id);
        return NULL;
    }

    if (!questions || !cJSON_IsArray(questions))
    {
        printf("Questions data is not initialized or is not an array.\n");
        return NULL;
    }

    cJSON *question = NULL;
    cJSON_ArrayForEach(question, questions)
    {
        cJSON *id = cJSON_GetObjectItem(question, "id");
        if (id && cJSON_IsNumber(id) && id->valueint == question_id)
        {
            // Deep copy the question to ensure memory safety
            cJSON *question_copy = cJSON_Duplicate(question, 1);
            if (!question_copy)
            {
                printf("Failed to duplicate question ID %d.\n", question_id);
            }
            return question_copy;
        }
    }

    printf("Question with ID %d not found.\n", question_id);
    return NULL;
}
// Authentication
bool user_exists(const char *username)
{
    FILE *file = fopen(CREDENTIALS_FILE, "r");
    if (!file)
        return false;

    char line[100];
    while (fgets(line, sizeof(line), file))
    {
        char stored_username[50];
        sscanf(line, "%s", stored_username);
        if (strcmp(username, stored_username) == 0)
        {
            fclose(file);
            return true;
        }
    }

    fclose(file);
    return false;
}

bool register_user(const char *username, const char *password)
{
    pthread_mutex_lock(&lock);
    if (user_exists(username))
    {
        pthread_mutex_unlock(&lock);
        return false;
    }

    FILE *file = fopen(CREDENTIALS_FILE, "a");
    if (!file)
    {
        pthread_mutex_unlock(&lock);
        return false;
    }

    fprintf(file, "%s %s\n", username, password);
    fclose(file);
    pthread_mutex_unlock(&lock);
    return true;
}

bool authenticate_user(const char *username, const char *password)
{
    FILE *file = fopen(CREDENTIALS_FILE, "r");
    if (!file)
        return false;

    char line[100], stored_username[50], stored_password[50];
    while (fgets(line, sizeof(line), file))
    {
        sscanf(line, "%s %s", stored_username, stored_password);
        if (strcmp(username, stored_username) == 0 && strcmp(password, stored_password) == 0)
        {
            fclose(file);
            return true;
        }
    }

    fclose(file);
    return false;
}

// Game Processing
bool validate_answer(int question_id, int selected_option)
{
    cJSON *question = get_question_by_id(question_id);
    if (!question)
        return false;

    cJSON *correct_option = cJSON_GetObjectItem(question, "correct_option");
    return correct_option && correct_option->valueint == selected_option;
}

void process_answer(int sock, int question_id, int selected_option)
{
    if (validate_answer(question_id, selected_option))
    {
        send(sock, "{\"type\":\"Answer\",\"status\":\"correct\"}\n", 38, 0);
    }
    else
    {
        send(sock, "{\"type\":\"Answer\",\"status\":\"incorrect\"}\n", 42, 0);
    }
}
