#ifndef SELECTMAINPLAYER_H
#define SELECTMAINPLAYER_H

#include "cJSON.h"

typedef struct
{
    char player_id[10];
    char answer[5];
    char timestamp[30];
    bool valid;
} Answer;

void send_question_with_timer();
void send_main_player_message(const char *player_id, const char *player_name);
void handle_answer_response(const cJSON *data);
void determine_main_player();

#endif
