#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define CREDENTIALS_FILE "users.txt"

typedef struct {
    int socket;
    char username[50];
    bool logged_in;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t lock;

bool user_exists(const char *username) {
    FILE *file = fopen(CREDENTIALS_FILE, "r");
    if (!file) return false;

    char line[100];
    while (fgets(line, sizeof(line), file)) {
        char stored_username[50];
        sscanf(line, "%s", stored_username);
        if (strcmp(username, stored_username) == 0) {
            fclose(file);
            return true;
        }
    }

    fclose(file);
    return false;
}

bool register_user(const char *username, const char *password) {
    pthread_mutex_lock(&lock);
    if (user_exists(username)) {
        pthread_mutex_unlock(&lock);
        return false;
    }

    FILE *file = fopen(CREDENTIALS_FILE, "a");
    if (!file) {
        pthread_mutex_unlock(&lock);
        return false;
    }

    fprintf(file, "%s %s\n", username, password);
    fclose(file);
    pthread_mutex_unlock(&lock);
    return true;
}

bool authenticate_user(const char *username, const char *password) {
    FILE *file = fopen(CREDENTIALS_FILE, "r");
    if (!file) return false;

    char line[100], stored_username[50], stored_password[50];
    while (fgets(line, sizeof(line), file)) {
        sscanf(line, "%s %s", stored_username, stored_password);
        if (strcmp(username, stored_username) == 0 && strcmp(password, stored_password) == 0) {
            fclose(file);
            return true;
        }
    }

    fclose(file);
    return false;
}

void *handle_client(void *arg) {
    int sock = *(int *)arg;
    char buffer[1024];
    char username[50] = {0};
    bool logged_in = false;

    while (recv(sock, buffer, 1024, 0) > 0) {
        buffer[strcspn(buffer, "\n")] = '\0';
        char command[20], arg1[50], arg2[50];
        sscanf(buffer, "%s %s %s", command, arg1, arg2);

        if (strcmp(command, "REGISTER") == 0) {
            if (register_user(arg1, arg2)) {
                send(sock, "Registration successful\n", 24, 0);
            } else {
                send(sock, "Registration failed: User already exists\n", 41, 0);
            }
        } else if (strcmp(command, "LOGIN") == 0) {
            if (authenticate_user(arg1, arg2)) {
                logged_in = true;
                strcpy(username, arg1);
                send(sock, "Login successful\n", 18, 0);
            } else {
                send(sock, "Login failed: Invalid credentials\n", 34, 0);
            }
        } else if (strcmp(command, "LOGOUT") == 0) {
            if (logged_in) {
                logged_in = false;
                memset(username, 0, sizeof(username));
                send(sock, "Logout successful\n", 19, 0);
            } else {
                send(sock, "Logout failed: Not logged in\n", 30, 0);
            }
        } else {
            send(sock, "Unknown command\n", 17, 0);
        }

        memset(buffer, 0, sizeof(buffer));
    }

    close(sock);
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    pthread_mutex_init(&lock, NULL);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    listen(server_socket, MAX_CLIENTS);
    printf("Server listening on port %d\n", PORT);

    while ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len))) {
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, (void *)&client_socket);
    }

    close(server_socket);
    pthread_mutex_destroy(&lock);
    return 0;
}
