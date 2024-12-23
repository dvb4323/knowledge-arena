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
// Check if an answer is correct
bool validate_answer(int question_id, int selected_option)
{
    cJSON *question = get_question_by_id(question_id);
    if (!question)
        return false;

    // Lấy danh sách options
    cJSON *options = cJSON_GetObjectItem(question, "options");
    if (!options || !cJSON_IsArray(options))
    {
        printf("Invalid or missing options for question ID %d\n", question_id);
        cJSON_Delete(question);
        return false;
    }

    // Lấy correct_option
    cJSON *correct_option = cJSON_GetObjectItem(question, "correct_option");
    if (!correct_option || !cJSON_IsString(correct_option))
    {
        printf("Invalid or missing correct_option for question ID %d\n", question_id);
        cJSON_Delete(question);
        return false;
    }

    // Kiểm tra selected_option có hợp lệ
    int options_size = cJSON_GetArraySize(options);
    if (selected_option < 1 || selected_option > options_size)
    {
        printf("Selected option %d is out of range for question ID %d\n", selected_option, question_id);
        cJSON_Delete(question);
        return false;
    }

    // Lấy chuỗi tương ứng với selected_option
    cJSON *selected_option_value = cJSON_GetArrayItem(options, selected_option - 1);
    if (!selected_option_value || !cJSON_IsString(selected_option_value))
    {
        printf("Invalid selected option %d for question ID %d\n", selected_option, question_id);
        cJSON_Delete(question);
        return false;
    }

    // So sánh selected_option với correct_option
    bool is_correct = strcmp(selected_option_value->valuestring, correct_option->valuestring) == 0;

    cJSON_Delete(question); // Giải phóng bộ nhớ
    return is_correct;
}


// Process a client's answer and broadcast the result
void process_answer(int sock, int question_id, int selected_option, int player_id, Player *players, int player_count)
{
    // Find the player by player_id
    Player *current_player = NULL;
    for (int i = 0; i < player_count; i++)
    {
        if (players[i].player_id == player_id)
        {
            current_player = &players[i];
            break;
        }
    }

    if (!current_player)
    {
        char error_response[256];
        snprintf(error_response, sizeof(error_response),
                 "{\"type\":\"Answer_Response\",\"status\":\"failed\",\"message\":\"Player not found\"}");
        send(sock, error_response, strlen(error_response), 0);
        return;
    }

    // Kiểm tra câu trả lời
    if (validate_answer(question_id, selected_option))
    {
        // Nếu trả lời đúng, cộng điểm
        current_player->score += 10;

        char response[256];
        snprintf(response, sizeof(response),
                 "{\"type\":\"Answer_Response\",\"status\":\"success\",\"message\":\"Correct answer!\",\"score\":%d}",
                 current_player->score);
        send(sock, response, strlen(response), 0);
    }
    else
    {
        // Nếu trả lời sai, loại người chơi
        current_player->logged_in = false;

        char response[256];
        snprintf(response, sizeof(response),
                 "{\"type\":\"Answer_Response\",\"status\":\"failed\",\"message\":\"Wrong answer! You are eliminated.\"}");
        send(sock, response, strlen(response), 0);
    }

    // Giải phóng dữ liệu câu hỏi đã sao chép
    cJSON_Delete(question_id);
}
