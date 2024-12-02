#ifndef LOGIC_H
#define LOGIC_H

#include <stdbool.h>

#define CREDENTIALS_FILE "users.txt"

// Kiểm tra xem người dùng đã tồn tại chưa
bool user_exists(const char *username);

// Đăng ký người dùng mới
bool register_user(const char *username, const char *password);

// Xác thực thông tin đăng nhập
bool authenticate_user(const char *username, const char *password);

#endif // LOGIC_H
