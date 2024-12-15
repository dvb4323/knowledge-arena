#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "selectmainplayer.h"
#include "cJSON.h"
#include "server.h"

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static Answer fastest_answer;

void send_question_with_timer()
{
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "type", "Answer_Request");

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "question_id", "Q001");
    cJSON_AddNumberToObject(data, "time_limit", 30);
    cJSON_AddItemToObject(message, "data", data);

    char *message_str = cJSON_PrintUnformatted(message);
    broadcast(message_str);

    free(message_str);
    cJSON_Delete(message);
}

void send_main_player_message(const char *player_id, const char *player_name)
{
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "type", "Select_Main_Player");

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "main_player_id", player_id);
    cJSON_AddStringToObject(data, "main_player_name", player_name);

    char announcement[100];
    snprintf(announcement, sizeof(announcement), "%s has been selected as the main player", player_name);
    cJSON_AddStringToObject(data, "message", announcement);

    cJSON_AddItemToObject(message, "data", data);

    char *message_str = cJSON_PrintUnformatted(message);
    broadcast(message_str);

    free(message_str);
    cJSON_Delete(message);
}

void handle_answer_response(const cJSON *data)
{
    const char *player_id = cJSON_GetObjectItem(data, "player_id")->valuestring;
    const char *answer = cJSON_GetObjectItem(data, "answer")->valuestring;
    const char *timestamp = cJSON_GetObjectItem(data, "timestamp")->valuestring;

    pthread_mutex_lock(&lock);
    if (!fastest_answer.valid || strcmp(timestamp, fastest_answer.timestamp) < 0)
    {
        strcpy(fastest_answer.player_id, player_id);
        strcpy(fastest_answer.answer, answer);
        strcpy(fastest_answer.timestamp, timestamp);
        fastest_answer.valid = true;
    }
    pthread_mutex_unlock(&lock);
}

void determine_main_player()
{
    sleep(30); // Đợi 30 giây cho người chơi trả lời

    pthread_mutex_lock(&lock);
    if (fastest_answer.valid)
    {
        printf("Main player: %s\n", fastest_answer.player_id);

        char main_player_name[50] = "Unknown";
        for (int i = 0; i < player_count; i++)
        {
            if (strcmp(players[i].username, fastest_answer.player_id) == 0)
            {
                strcpy(main_player_name, players[i].username);
                break;
            }
        }
        send_main_player_message(fastest_answer.player_id, main_player_name);
    }
    else
    {
        printf("No valid answers received.\n");
    }
    pthread_mutex_unlock(&lock);
}
