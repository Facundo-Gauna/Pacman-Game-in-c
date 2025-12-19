#include "rank.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

static FILE *open_score_file(const char *mode) {
    char *prefPath = SDL_GetBasePath();
    if (!prefPath) {
        fprintf(stderr, "SDL_GetPrefPath failed: %s\n", SDL_GetError());
        return NULL;
    }

    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s%s", prefPath, SCORE_FILE);
    SDL_free(prefPath);

    FILE *f = fopen(fullpath, mode);
    if (!f) {
        perror("fopen scores.dat");
    }
    return f;
}


void init_scoreboard(ScoreBoard *board) {
    memset(board, 0, sizeof(ScoreBoard));
}

void load_scores(ScoreBoard *board) {
    FILE *f = open_score_file("rb");
    if (!f) {
        board->count = 0;
        return;
    }

    fread(&board->count, sizeof(int), 1, f);
    if (board->count > MAX_SCORES)
        board->count = MAX_SCORES;

    fread(board->scores, sizeof(MAX_SCORES), board->count, f);
    fclose(f);
}

void save_scores(const ScoreBoard *board) {
    FILE *f = open_score_file("wb");
    if (!f) return;

    fwrite(&board->count, sizeof(int), 1, f);
    fwrite(board->scores, sizeof(MAX_SCORES), board->count, f);
    fclose(f);
}

void add_score(ScoreBoard* board, const char name[MAX_NAME_LEN+1] , uint16_t score){
    if (!name) return;

    /* Insert sort  */
    uint8_t i = board->count;

    if (i < MAX_SCORES)
        board->count++;

    while (i > 0 && board->scores[i - 1].score < score) {
        if (i < MAX_SCORES)
            board->scores[i] = board->scores[i - 1];
        i--;
    }

    if (i < MAX_SCORES) {
        strncpy(board->scores[i].name, name, MAX_NAME_LEN);
        board->scores[i].name[MAX_NAME_LEN] = '\0';
        board->scores[i].score = score;
    }
}
