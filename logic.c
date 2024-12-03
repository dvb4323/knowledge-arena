#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include "logic.h"

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

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
