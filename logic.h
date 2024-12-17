#ifndef LOGIC_H
#define LOGIC_H

#include <stdbool.h>
#include "cJSON.h"

#define CREDENTIALS_FILE "users.txt"

// Kiểm tra xem người dùng đã tồn tại chưa
bool user_exists(const char *username);

// Đăng ký người dùng mới
bool register_user(const char *username, const char *password);

// Xác thực thông tin đăng nhập
bool authenticate_user(const char *username, const char *password);

// Score-related functions
bool update_score(const char *username, int score_delta);

// Other utility functions
typedef struct
{
    int id;
    char question[256];
    char options[4][100];
    char answer[100];
} Question;

// Define a player structure
typedef struct Player
{
    int player_id;
    int socket;
    char username[50];
    bool logged_in;
    int score;
} Player;

// Question-related functions
bool load_questions();
cJSON *get_question_by_id(int question_id);

void process_answer(int sock, int question_id, int selected_option, int player_id, Player *players, int player_count);

typedef struct
{
    int sock;            // Socket for sending responses
    int question_id;     // ID of the question
    int selected_option; // Selected answer option
    int player_id;       // ID of the player
} AnswerData;

#endif // LOGIC_H
