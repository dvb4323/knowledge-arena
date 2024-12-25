#ifndef LOGIC_H
#define LOGIC_H
#include <stdbool.h>
#include "cJSON.h"

// Đường dẫn file dữ liệu
#define QUESTIONS_FILE "questions.json"
#define PLAYERS_FILE "players.json"

// Kiểm tra xem người dùng đã tồn tại chưa
bool user_exists(const char *username);

// Đăng ký người dùng mới
bool register_user(const char *username, const char *password);

// Xác thực thông tin đăng nhập
bool authenticate_user(const char *username, const char *password);

// Quản lý dữ liệu người chơi
bool load_players();                     // Tải dữ liệu người chơi từ file JSON
bool save_players();                     // Lưu dữ liệu người chơi vào file JSON
cJSON *get_player(const char *username); // Truy xuất thông tin người chơi dựa trên tên đăng nhập

// Xử lý câu hỏi
bool load_questions();                      // Tải câu hỏi từ file JSON
cJSON *get_question_by_id(int question_id); // Truy xuất câu hỏi theo ID

// Xử lý câu trả lời của người chơi
bool validate_answer(int question_id, int selected_option, int player_id);

// Định nghĩa cấu trúc dữ liệu người chơi
typedef struct
{
    int socket;
    int player_id;
    char username[50];
    bool logged_in;
    int score;
    bool eliminated; // Thêm trạng thái bị loại
} Player;

// Quản lý điểm số
bool update_score(const char *username, int score_delta);

#endif // LOGIC_H