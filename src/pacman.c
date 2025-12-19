#define SDL_MAIN_HANDLED
#include "pacman.h"


// ------------- HELPERS --------------

static void join_path(const char *base, const char *rel, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (!base || !rel) {
        out[0] = '\0';
        return;
    }

    char norm_rel[1024];
    size_t ri = 0;
    for (size_t i = 0; rel[i] != '\0' && ri + 1 < sizeof(norm_rel); ++i) {
        char c = rel[i];
        if (c == '/' || c == '\\') {
            norm_rel[ri++] = PATH_SEP[0];
        } else {
            norm_rel[ri++] = c;
        }
    }
    norm_rel[ri] = '\0';

    size_t bl = strlen(base);
    int need_sep = 0;
    if (bl > 0) {
        char last = base[bl - 1];
        need_sep = (last != '/' && last != '\\');
    }

    if (need_sep) {
        snprintf(out, out_sz, "%s%s%s", base, PATH_SEP, norm_rel);
    } else {
        snprintf(out, out_sz, "%s%s", base, norm_rel);
    }
}

static void show_error_and_quit(const char *title, const char *msg, AppContext *app) {
    if (title == NULL) title = "Error";
    if (msg == NULL) msg = "Unknown error";

    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
    } else {
        /* fallback  stderr */
        fprintf(stderr, "%s: %s\n", title, msg);
    }

    if (app) quit_game_application(app);

    exit(EXIT_FAILURE);
}


static inline void assertGame(bool cond, const char *msg, AppContext *app) {
    if (cond) return;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s\nSDL Error: %s", msg ? msg : "Fatal error", SDL_GetError());
    show_error_and_quit("Fatal", buf, app);
}

static inline void assert_texture(SDL_Texture *tex, const char *path, AppContext *app) {
    if (tex) return;
    char buf[512];
    if (path) snprintf(buf, sizeof(buf), "Failed to load texture: %s\nIMG Error: %s", path, IMG_GetError());
    else snprintf(buf, sizeof(buf), "Failed to load texture\nIMG Error: %s", IMG_GetError());
    show_error_and_quit("Texture load error", buf, app);
}

static inline void assert_font(TTF_Font *font, const char *path, AppContext *app) {
    if (font) return;
    char buf[512];
    if (path) snprintf(buf, sizeof(buf), "Failed to load font: %s\nTTF Error: %s", path, TTF_GetError());
    else snprintf(buf, sizeof(buf), "Failed to load font\nTTF Error: %s", TTF_GetError());
    show_error_and_quit("Font load error", buf, app);
}

static inline void assert_chunk(Mix_Chunk *chunk, const char *path, AppContext *app) {
    if (chunk) return;
    char buf[512];
    if (path) snprintf(buf, sizeof(buf), "Failed to load sound: %s\nMIX Error: %s", path, Mix_GetError());
    else snprintf(buf, sizeof(buf), "Failed to load sound\nMIX Error: %s", Mix_GetError());
    show_error_and_quit("Sound load error", buf, app);
}

static inline void assert_ptr(void *p, const char *what, AppContext *app) {
    if (p) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "Out of memory or null pointer: %s", what ? what : "unknown");
    show_error_and_quit("Memory error", buf, app);
}

static void safe_destroy_texture(SDL_Texture **tex) {
    if (tex && *tex) {
        SDL_DestroyTexture(*tex);
        *tex = NULL;
    }
}

static void safe_free_chunk(Mix_Chunk **chunk) {
    if (chunk && *chunk) {
        Mix_FreeChunk(*chunk);
        *chunk = NULL;
    }
}

static void safe_free(void **ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

static inline void create_text_texture(TextLabel* text, FontSize fontSize , FontColor color, AppContext* app) {
    safe_destroy_texture(&text->texture);

    TTF_SetFontSize(app->font, fontSizes[fontSize]);    
    SDL_Surface* surf = TTF_RenderText_Blended(app->font, text->text, colors[color]);
    if (!surf) return;
    
    text->texture = SDL_CreateTextureFromSurface(app->renderer, surf);
    SDL_FreeSurface(surf);
    
    if (text->texture) {
        text->needsUpdate = false;
        SDL_QueryTexture(text->texture, NULL,NULL, &text->dst.w,&text->dst.h);
    }
}

static inline void center_texture_rect(SpriteImage *img,float scale, int16_t yOffset) {
    int texW = 0, texH = 0;
    SDL_QueryTexture(img->img, NULL, NULL, &texW, &texH);
    int dstW = texW * scale;
    int dstH = texH * scale;

    img->dst.x = (WINDOW_WIDTH - dstW) >> 1;
    img->dst.y = ((WINDOW_HEIGHT - dstH) >> 1) + yOffset;
    img->dst.w = dstW;
    img->dst.h =  dstH;
}

static inline SpriteID charToSpriteID(char ch) {
  switch (ch) {
    case '.': return SPR_DOT;
    case 'o': return SPR_ORB;
    case ' ': return SPR_EMPTY;
    default: return SPR_NULL;
  }
}

static inline void add_score_to_board(GameLogic *game){
    add_score(&game->board,game->player.playerName.text,game->player.score);
    game->player.playerName.text[0] = '\0';
} 

static inline bool is_valid_name_char(SDL_Keycode key) {
    return (key >= '0' && key <= '9') || (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z');
}

// -----------------  GAME LOGIC -------------------
static inline void reset_positions(GameLogic *game) {
    // Reset pacman
    game->player.pacman.row = PACMAN_START_ROW;
    game->player.pacman.col = PACMAN_START_COL;
    game->player.pacman.dir = DIR_UP;
    game->player.hunterTime = 0;
    game->player.ghostCombo = 1;
    
    // Reset ghosts
    const int8_t ghostStartPos[4][2] = {
        {GHOST_HOME-3, GHOST_HOME},    // Blinky
        {GHOST_HOME-1, GHOST_HOME},    // Pinky
        {GHOST_HOME-1, GHOST_HOME-2},  // Inky
        {GHOST_HOME-1, GHOST_HOME+2}   // Clyde
    };
    
    for (int i = 0; i < 4; i++) {
        game->ghosts[i].row = ghostStartPos[i][0];
        game->ghosts[i].col = ghostStartPos[i][1];
        game->ghosts[i].scared = false;
        game->ghosts[i].dir = DIR_UP;
        game->ghosts[i].kind = TYPE_BLINKY+i;
    }
}

static void init_level(GameLogic *game, bool levelWon) {
    if (levelWon) {
        game->player.dotsEaten = 0;
        if (game->player.lives < 3) game->player.lives++;
        game->player.rewardCount++;
    } else {
        game->player.score = 0;
        game->player.lives = 3;
        game->player.rewardCount = 1;
        game->player.dotsEaten = 0;
    }
    
    reset_positions(game);
    memcpy(game->map, pacman_map, sizeof(pacman_map));
    game->state = STATE_START_LEVEL;
}

static bool check_collisions(AppContext *app) {
    GameLogic*game = &app->game;
    uint8_t i = 0;
    for (; i < 4; i++) {
        if (game->player.pacman.row == game->ghosts[i].row && 
            game->player.pacman.col == game->ghosts[i].col) {
            
            if (game->player.hunterTime > 0 && game->ghosts[i].scared) {
                // Pacman eats ghost
                app->ui.overlay.score.needsUpdate = true;
                game->player.score += game->player.ghostCombo * HUNTER_SCORE_MULTIPLIER;
                game->player.ghostCombo++;
                game->ghosts[i].row = GHOST_HOME;
                game->ghosts[i].col = GHOST_HOME;
                game->ghosts[i].scared = false;
                Mix_PlayChannel(-1, app->sounds.eatGhost, 0);
            }else{
                if(--game->player.lives == 0){
                    add_score_to_board(game);
                    game->state = STATE_GAME_OVER;
                }else{
                    game->state =  STATE_LIFE_LOST;
                }
                return true;
            }
        }
    }
    return false;
}

static bool try_move(GameEntity *entity, Direction dir, bool commitMove, GameLogic *game ) {
    int8_t nextRow = entity->row + directionOffsets[dir][0];
    int8_t nextCol = entity->col + directionOffsets[dir][1];
    
    // Handle tunnel warp
    if (nextCol < 0) {
        nextCol = MAP_COLS-1;
        nextRow = 14;
    } else if (nextCol >= MAP_COLS-1) {
        nextCol = 0;
        nextRow = 14;
    }
    
    if (game->map[nextRow][nextCol] == '#') return false;
    
    if (commitMove) {
        entity->row = nextRow;
        entity->col = nextCol;
        entity->dir = dir;
    }
    return true;
}

static void update_ghosts(AppContext *app) {
    GameLogic *game = &app->game;
    for(int i = 0; i < 4; i++) {
        GameEntity* ghost = &app->game.ghosts[i];

        // Calculate speed based on ghost type and game progress
        ghost->moveTimer += DELTA_TICK_MS;
        
        uint16_t timeRequired = ghost->scared ? GHOST_FRIGHTENED_TICKS :
        ghostBaseTicks[i]-(app->game.player.dotsEaten >> 3);

        if (ghost->moveTimer < timeRequired) continue;
        ghost->moveTimer = 0;

        if(!ghost->scared || game->player.hunterTime == 0) {

            int8_t targetRow = game->player.pacman.row;
            int8_t targetCol = game->player.pacman.col;

            switch (ghost->kind) {
                case TYPE_PINKY: // Pinky - targets 4 tiles ahead of Pacman
                    targetRow += directionOffsets[game->player.pacman.dir][0] << 2;
                    targetCol += directionOffsets[game->player.pacman.dir][1] << 2;

                    // Special case for up direction (original Pacman bug)
                    if (game->player.pacman.dir == DIR_UP) targetCol -= 4;
                    break;

                case TYPE_INKY:{ // Inky - uses Blinky's position to calculate target
                    int8_t pacAheadRow = game->player.pacman.row + (directionOffsets[game->player.pacman.dir][0] << 1);
                    int8_t pacAheadCol = game->player.pacman.col + (directionOffsets[game->player.pacman.dir][1] << 1);

                    targetRow = pacAheadRow + (pacAheadRow - game->ghosts[0].row); // less blinky position.
                    targetCol = pacAheadCol + (pacAheadCol - game->ghosts[0].col);
                    break;
                }
                case TYPE_CLYDE: // Clyde - scatters if close to Pacman
                    if (abs(game->player.pacman.col - ghost->col) + 
                        abs(game->player.pacman.row - ghost->row) <= 8) {
                        targetRow = MAP_ROWS - 1;
                        targetCol = 0;
                    }
                    break;
                default: // Blinky - chases directly
                    break;
            }

            Direction bestDir = DIR_COUNT; // Default to current direction
            uint16_t bestDistance = UINT16_MAX;

            // Try all possible directions (excluding reverse of current direction)
            for (Direction dir = 0; dir < DIR_COUNT; dir++) {
                // Ghosts can't reverse direction (unless in scared mode)
                if (dir == (ghost->dir + 2) % DIR_COUNT) continue;
                
                // Check if movement in this direction is possible
                int8_t nextRow = ghost->row + directionOffsets[dir][0];
                int8_t nextCol = ghost->col + directionOffsets[dir][1];

                // Skip invalid moves
                if (nextRow < 0 || nextRow >= MAP_ROWS || 
                    nextCol < 0 || nextCol >= MAP_COLS || 
                    game->map[nextRow][nextCol] == '#') {
                    continue;
                }

                // Calculate distance to target
                uint16_t distance = (targetCol - nextCol) * (targetCol - nextCol) + 
                                   (targetRow - nextRow) * (targetRow - nextRow);

                // Prefer directions that get us closer to target
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestDir = dir;
                }else if (distance == bestDistance) {
                    // Original Pacman ghost movement priorities
                    bestDir = dir < bestDir ? dir : bestDir; // directions are sorted in a prirority order by defualt.
                }
            }

            if(bestDir == DIR_COUNT) bestDir = (ghost->dir + 2) % DIR_COUNT; // reverse position
            
            try_move(ghost, bestDir, true, game);

        } else {
            Direction dir = rand() % DIR_COUNT;
            uint8_t attempts = 0;    
            while (attempts < DIR_COUNT) {
                if (dir != (ghost->dir+2)%DIR_COUNT && try_move(ghost, dir, true, game)) return;
                dir = (dir + 1) % DIR_COUNT;
                attempts++;
            }
            // If no valid move found, continue in current direction
            try_move(ghost, (ghost->dir+2)%DIR_COUNT, true,game);
        }
    }
}

void update_game(AppContext *app) {
    GameLogic *game = &app->game;
    
    // Update hunter mode timer
    if (game->player.hunterTime > 0) {
        game->player.hunterTime -= DELTA_TICK_MS;
        if (game->player.hunterTime <= 0) {
            game->player.hunterTime = 0;
            for (int i = 0; i < 4; i++) {
                game->ghosts[i].scared = false;
            }
        }
    }
    
    // Fixed timestep game updates
    while (app->timer.accumulator >= DELTA_TICK_MS) {
        app->timer.accumulator -= DELTA_TICK_MS;
        
        update_ghosts(app);
        if (check_collisions(app)) return;
        
        // Update pacman
        game->player.pacman.moveTimer += DELTA_TICK_MS;
        if(game->player.pacman.moveTimer >= BASE_TICKS ){
            game->player.pacman.moveTimer = 0;
            if (!try_move(&game->player.pacman, game->player.pacman.dir, true, game)) {
                continue; // Pacman couldn't move in desired direction
            }
            if (check_collisions(app)) return;

            char tile = game->map[game->player.pacman.row][game->player.pacman.col];
            if (tile == '.' || tile == 'o') {
                if(app->sounds.dotTimer>=300){
                    Mix_PlayChannel(-1, app->sounds.eatDot, 0);
                    app->sounds.dotTimer = 0;
                }
                game->map[game->player.pacman.row][game->player.pacman.col] = ' ';
                game->player.dotsEaten++;
                app->ui.overlay.score.needsUpdate = true;
                if (tile == 'o') {
                    game->player.hunterTime = HUNTER_MODE_DURATION_MS;
                    game->player.score += 50;
                    game->player.ghostCombo = 1;
                    for (int i = 0; i < 4; i++) {
                        game->ghosts[i].scared = true;
                    }
                }else game->player.score += 10;

                // Level completion check
                if (game->player.dotsEaten == TOTAL_DOTS) {
                    if (game->player.rewardCount == 9) {
                        game->state = STATE_GAME_COMPLETE;
                    } else {
                        init_level(game,true);
                    }
                }
            }       
        }
    }
    
}

// --------------  RENDER ---------------
static void render_enter_name_state(AppContext *app){
    SDL_Rect inputBox = {(WINDOW_WIDTH>>1)-150,(WINDOW_HEIGHT>>1),300,50};
    TextLabel *nameLabel = &app->game.player.playerName;
    nameLabel->dst.x = (WINDOW_WIDTH>>1) - (20 + ((strlen(nameLabel->text)>>1) * 20));
    create_text_texture(nameLabel, STANDARD,WHITE,app);

    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
    SDL_RenderDrawRect(app->renderer, &inputBox);
    
    SDL_RenderCopy(app->renderer, app->ui.menu.title.img, NULL, &app->ui.menu.title.dst);
    SDL_RenderCopy(app->renderer, nameLabel->texture, NULL, &nameLabel->dst);
    SDL_RenderCopy(app->renderer, app->ui.scoreboard.hint.texture, NULL, &app->ui.scoreboard.hint.dst);
    SDL_RenderCopy(app->renderer, app->ui.menu.credit.texture, NULL, &app->ui.menu.credit.dst);        
    
    SDL_SetRenderDrawColor(app->renderer, 0,0,0, 255);
    SDL_RenderPresent(app->renderer);
    nameLabel->needsUpdate = false;
}

static void render_playing_state(AppContext *app ,bool present){
    // Render Map
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawColor(app->renderer, 0,0,0, 255);

    for (int8_t i = 0; i < MAP_ROWS; i++) {
        for (int8_t j = MAP_COLS-1; j >=0 ; j--) {
            SDL_Rect tileDst = {
                j * TILE_WIN_SIZE,
                i * TILE_WIN_SIZE + MAP_OFFSET_Y,
                TILE_WIN_SIZE,
                TILE_WIN_SIZE
            };
            
            SpriteID sprite = charToSpriteID(app->game.map[i][j]);
            if (sprite > SPR_ORB) {
                // Special tiles (walls)
                SDL_Rect wallSrc = {j * TILE_SPR_SIZE + 224,i * TILE_SPR_SIZE,TILE_SPR_SIZE,TILE_SPR_SIZE};
                SDL_RenderCopy(app->renderer, app->spritesheet, &wallSrc, &tileDst);
            } else {
                // Regular tiles (dots, orbs)
                tileDst.x += TILE_SPR_SIZE;
                SDL_RenderCopy(app->renderer, app->spritesheet, &spriteClips[sprite], &tileDst);
            }
        }
    }
    
    // Render ghosts
    for (int i = 0; i < 4; i++) {
        GameEntity *ghost = &app->game.ghosts[i];
        if ( !ghost->scared && app->game.player.hunterTime > 0 && ((int)(SDL_GetTicks() * 0.005)) % 2 == 0) {
            continue; // Skip rendering during flash
        }

        SpriteID ghostBase = SPR_GHOST_BLINKY_1;
        if(app->game.player.hunterTime > 0 && app->game.ghosts[i].scared) ghostBase = app->game.player.hunterTime  <= HUNTER_WARNING_TIME_MS ? SPR_GHOST_SCARY_WHITE_1 : SPR_GHOST_SCARY_BLUE_1;
        else if (app->game.ghosts[i].kind == TYPE_PINKY) ghostBase = SPR_GHOST_PINKY_1;
        else if (app->game.ghosts[i].kind == TYPE_INKY) ghostBase = SPR_GHOST_INKY_1;
        else if (app->game.ghosts[i].kind == TYPE_CLYDE) ghostBase = SPR_GHOST_CLYDE_1;

        SDL_Rect ghostDst = {
            ghost->col * TILE_WIN_SIZE,
            ghost->row * TILE_WIN_SIZE + MAP_OFFSET_Y,
            (int)(TILE_WIN_SIZE * 1.25f),
            (int)(TILE_WIN_SIZE * 1.25f)
        };
        SDL_RenderCopy(app->renderer, app->spritesheet, &spriteClips[ghostBase], &ghostDst);
    }
    
    // Render Pacman
    GameEntity *pacman = &app->game.player.pacman;
    SDL_Rect pacmanDst = {
        pacman->col * TILE_WIN_SIZE + 6,
        pacman->row * TILE_WIN_SIZE + MAP_OFFSET_Y,
        TILE_WIN_SIZE * 1.25f,
        TILE_WIN_SIZE * 1.25f
    };
    
    // Alternate between open and closed mouth for animation
    SpriteID pacmanSprite = (pacman->dir << 1)  + (pacman->scared ? 1 : 0);
    pacman->scared = !pacman->scared;
    
    SDL_RenderCopy(app->renderer, app->spritesheet, &spriteClips[pacmanSprite], &pacmanDst);

    // Render Game Layout
    PlayerData *player = &app->game.player;

    if (app->ui.overlay.score.needsUpdate) {
        snprintf(app->ui.overlay.score.text, 14, "Score: %05d", player->score);
        create_text_texture(&app->ui.overlay.score, STANDARD,WHITE,app);
    }

    SDL_RenderCopy(app->renderer, app->ui.overlay.score.texture, NULL, &app->ui.overlay.score.dst);
    
    // Render lives
    SDL_RenderCopy(app->renderer, app->ui.overlay.lives.texture, NULL, &app->ui.overlay.lives.dst);
    SDL_Rect lifeDst = {app->ui.overlay.lives.dst.x + 98,app->ui.overlay.lives.dst.y - 5,TILE_WIN_SIZE << 1,TILE_WIN_SIZE <<1};
    for (int i = 0; i < player->lives; i++) {
        lifeDst.x += 42;
        SDL_RenderCopy(app->renderer, app->spritesheet, &spriteClips[SPR_PACMAN_RIGHT_2], &lifeDst);
    }
    
    // Render rewards
    SDL_Rect rewardDst = {app->ui.overlay.lives.dst.x + 270,app->ui.overlay.lives.dst.y - 10,TILE_WIN_SIZE * 1.5f,TILE_WIN_SIZE * 1.5f};
    for (int i = 0; i < player->rewardCount; i++) {
        if(i == 5) rewardDst.y += 20;
        rewardDst.x += (i % 5) * 24;
        SDL_RenderCopy(app->renderer, app->spritesheet, &spriteClips[SPR_REWARD_1 + i], &rewardDst);
    }

    if(present) {
        SDL_RenderPresent(app->renderer);
        if(app->sounds.moveTimer >= 300){
            Mix_PlayChannel(-1, app->sounds.move, 0);
            app->sounds.moveTimer = 0;
        }
    }
}

static void render_life_lost_state(AppContext *app){
    // Animate pacman death
    Mix_PlayChannel(-1, app->sounds.death, 0);
    SDL_Rect pacmanDst = {
        app->game.player.pacman.col * TILE_WIN_SIZE + 6,
        app->game.player.pacman.row * TILE_WIN_SIZE + MAP_OFFSET_Y,
        TILE_WIN_SIZE * 1.25f,
        TILE_WIN_SIZE * 1.25f
    };
    SDL_Rect deathFrame = {504 - TILE_WIN_SIZE, 0, TILE_WIN_SIZE, TILE_WIN_SIZE};    
    for (int i = 0; i < 11; i++) {
        deathFrame.x += TILE_WIN_SIZE;
        SDL_RenderCopy(app->renderer, app->spritesheet, &deathFrame, &pacmanDst);
        SDL_RenderPresent(app->renderer);
        SDL_Delay(100);
    }
    app->game.state = STATE_START_LEVEL;
    reset_positions(&app->game);
}

static void render_game_over_state(AppContext *app) {
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawColor(app->renderer, 0,0,0, 255);

    SDL_RenderCopy(app->renderer, app->ui.overlay.gameOver.img, NULL, &app->ui.overlay.gameOver.dst);
    SDL_RenderPresent(app->renderer);
    
    SDL_Delay(2000);
    app->game.state = STATE_MENU;
    render(app);
}

static void render_start_level_state(AppContext *app) {
    // Countdown animation
    TextLabel *readyLabel = &app->ui.overlay.ready;
    Mix_PlayChannel(-1,app->sounds.start, 0);
    for (int i = 3; i > 0; i--) {
        render_playing_state(app,false); // render game to have a background.
        snprintf(readyLabel->text, 10, "!Ready %1d", i);
        create_text_texture(readyLabel, STANDARD,WHITE,app);
        
        SDL_RenderCopy(app->renderer, readyLabel->texture, NULL, &readyLabel->dst);
        SDL_RenderPresent(app->renderer);

        SDL_Delay(1000);
    }
    app->timer.accumulator = -3000;
    app->game.state = STATE_PLAYING;
}

static void render_menu_state(AppContext *app) {
    // Draw title and menu options
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);

    SDL_RenderCopy(app->renderer, app->ui.menu.title.img, NULL, &app->ui.menu.title.dst);
    SDL_RenderCopy(app->renderer, app->ui.menu.play.texture, NULL, &app->ui.menu.play.dst);
    SDL_RenderCopy(app->renderer, app->ui.menu.help.texture, NULL, &app->ui.menu.help.dst);
    SDL_RenderCopy(app->renderer, app->ui.menu.exit.texture, NULL, &app->ui.menu.exit.dst);
    SDL_RenderCopy(app->renderer, app->ui.menu.rank.texture, NULL, &app->ui.menu.rank.dst);
    SDL_RenderCopy(app->renderer, app->ui.menu.credit.texture, NULL, &app->ui.menu.credit.dst);
    
    // Draw ghost previews
    SDL_Rect ghostDst = {(WINDOW_WIDTH>>1) - 185, 450, TILE_WIN_SIZE * 4, TILE_WIN_SIZE * 4};
    for (int j = 0; j < 4; j++) {
        SDL_RenderCopy(app->renderer, app->spritesheet, 
            &spriteClips[SPR_GHOST_BLINKY_1 + (j << 1)], &ghostDst);
        ghostDst.x += 100;
    }
    SDL_RenderPresent(app->renderer);
}

static void render_help_state(AppContext *app) {
    // Get help image dimensions and scale it
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);

    SDL_Rect helpDst = {-20, 20, app->ui.help.textW >>1,app->ui.help.textW >> 1};
    
    SDL_RenderCopy(app->renderer, app->ui.help.helpImg, NULL, &helpDst);
    SDL_RenderCopy(app->renderer, app->ui.menu.credit.texture, NULL, &app->ui.menu.credit.dst);
    SDL_RenderPresent(app->renderer);
}

static void render_paused_state(AppContext *app) {
    SDL_RenderCopy(app->renderer, app->ui.overlay.pause.img, NULL, &app->ui.overlay.pause.dst);
    SDL_RenderPresent(app->renderer);
}

static void render_game_complete_state(AppContext *app) {
    Mix_PlayChannel(-1, app->sounds.win, 0);
    SDL_RenderCopy(app->renderer, app->ui.overlay.gameWin.img, NULL, &app->ui.overlay.gameWin.dst);
    SDL_RenderPresent(app->renderer);
    SDL_Delay(2000);
    app->game.state = STATE_MENU;
}

static void render_ranking_state(AppContext *app) {
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);

    SDL_RenderCopy(app->renderer, app->ui.scoreboard.rankingImg.img, NULL, &app->ui.scoreboard.rankingImg.dst);
    
    // Render scoreboard entries
    for (int i = 0; i < app->game.board.count; i++) {
        // Render name
        app->ui.scoreboard.names.dst.y = app->ui.scoreboard.rankingImg.dst.y + (int)(app->ui.scoreboard.rankingImg.dst.h * (0.225f + 0.063f * i));
        strncpy(app->ui.scoreboard.names.text, app->game.board.scores[i].name, MAX_NAME_LEN);
        app->ui.scoreboard.names.text[MAX_NAME_LEN] = '\0';
        create_text_texture(&app->ui.scoreboard.names, SMALL,WHITE,app);
        
        // Render score
        app->ui.scoreboard.scores.dst.y = app->ui.scoreboard.names.dst.y;
        snprintf(app->ui.scoreboard.scores.text, 6, "%5d", app->game.board.scores[i].score);
        create_text_texture(&app->ui.scoreboard.scores, SMALL,WHITE,app);
        
        // Position and render both
        SDL_RenderCopy(app->renderer, app->ui.scoreboard.names.texture, NULL, &app->ui.scoreboard.names.dst);
        SDL_RenderCopy(app->renderer, app->ui.scoreboard.scores.texture, NULL, &app->ui.scoreboard.scores.dst);
    }
    
    SDL_RenderCopy(app->renderer, app->ui.menu.credit.texture, NULL, &app->ui.menu.credit.dst);
    SDL_RenderPresent(app->renderer);
}

void render(AppContext *app) {
    GameState currentState = app->game.state;
    GameState prevState = app->game.prevState;
    
    switch (currentState) {
        case STATE_ENTER_NAME:
            if(app->game.player.playerName.needsUpdate) render_enter_name_state(app);
            break;
            
        case STATE_LIFE_LOST:
            render_life_lost_state(app);
            break;
            
        case STATE_GAME_OVER:
            render_game_over_state(app);
            break;
            
        case STATE_START_LEVEL:
            render_start_level_state(app);
            break;
            
        case STATE_MENU:
            if (prevState != STATE_MENU) {
                render_menu_state(app);
            }
            break;
            
        case STATE_PLAYING:
            render_playing_state(app,true);
            break;
            
        case STATE_HELP:
            if (prevState != STATE_HELP) {
                render_help_state(app);
            }
            break;
            
        case STATE_PAUSED:
            if (prevState != STATE_PAUSED) {
                render_paused_state(app);
            }
            break;
            
        case STATE_GAME_COMPLETE:
            render_game_complete_state(app);
            break;
            
        case STATE_RANKING:
            if (prevState != STATE_RANKING) {
                render_ranking_state(app);
            }
            break;
    }

    app->game.prevState = currentState;
}

// ---------------- INIT AND QUIT ----------------
void init_game_application(AppContext *app) {
    memset(app, 0, sizeof(AppContext));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        TTF_Quit();
        SDL_Quit();
        exit(EXIT_FAILURE);
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    app->window = SDL_CreateWindow("Pacman Game", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    assertGame(app->window != NULL, "Failed to create SDL window", app);

    app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    assertGame(app->renderer != NULL, "Failed to create SDL renderer", app);

    SDL_RenderSetLogicalSize(app->renderer, WINDOW_WIDTH, WINDOW_HEIGHT);
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");

    char *base = SDL_GetBasePath();
    char pathbuf[1024] = {0};
    
    if (!base) {
        base = SDL_strdup("./");
    }else{
        uint16_t len = strlen(base);
        base[len-1] = '\0';
        for(uint16_t i = len-2 ; i>0 && base[i] != PATH_SEP[0] ; i--) base[i] = '\0'; 
    }

    /* FONT */
    join_path(base, "assets/font/PressStart2P-Regular.ttf", pathbuf, sizeof(pathbuf));
    printf("Font base : %s\n",base);
    app->font = TTF_OpenFont(pathbuf, fontSizes[STANDARD]);
    assert_font(app->font, pathbuf, app);

    app->ui.menu.play.text = strdup("PLAY (S)");
    app->ui.menu.play.dst = (SDL_Rect){120, 200, 0, 0};

    app->ui.menu.help.text = strdup("HELP (H)");
    app->ui.menu.help.dst = (SDL_Rect){120, 250, 0, 0};

    app->ui.menu.rank.text = strdup("RANK (R)");
    app->ui.menu.rank.dst = (SDL_Rect){120, 300, 0, 0};

    app->ui.menu.exit.text = strdup("EXIT (ESC)");
    app->ui.menu.exit.dst = (SDL_Rect){100, 350, 0, 0};

    app->ui.menu.credit.text = strdup("@ Made by Facundo Gauna");
    app->ui.menu.credit.dst = (SDL_Rect){10, 575, 0, 0};

    app->ui.overlay.score.text = strdup("Score: 00000");
    app->ui.overlay.score.dst = (SDL_Rect){30, (int)(MAP_OFFSET_Y * 0.3f), 0, 0};

    app->ui.overlay.lives.text = strdup("Lives: ");
    app->ui.overlay.lives.dst = (SDL_Rect){30, MAP_ROWS * TILE_WIN_SIZE + MAP_OFFSET_Y + 12, 0, 0};

    app->ui.overlay.ready.text = strdup("!Ready 3");
    app->ui.overlay.ready.dst = (SDL_Rect){(int)(MAP_COLS * 4.75), 17 * TILE_WIN_SIZE + MAP_OFFSET_Y, 0, 0};

    app->ui.scoreboard.hint.text = strdup("__Enter a name__");
    app->ui.scoreboard.hint.dst = (SDL_Rect){(WINDOW_WIDTH >> 1) - 190, (WINDOW_HEIGHT >> 1) - 30, 0, 0};

    app->ui.scoreboard.names.text = malloc(MAX_NAME_LEN+1);
    assert_ptr(app->ui.scoreboard.names.text, "scoreboard.names.text alloc", app);
    app->ui.scoreboard.names.dst = (SDL_Rect){(WINDOW_WIDTH >> 1) - 75, (WINDOW_HEIGHT >> 1), 0, 0};

    app->ui.scoreboard.scores.text = malloc(6);
    assert_ptr(app->ui.scoreboard.scores.text, "scoreboard.scores.text alloc", app);
    app->ui.scoreboard.scores.dst = (SDL_Rect){(WINDOW_WIDTH >> 1) + 50, (WINDOW_HEIGHT >> 1), 0, 0};

    app->game.player.playerName.text = malloc(MAX_NAME_LEN+1);
    assert_ptr(app->game.player.playerName.text, "playerName.text alloc", app);
    app->game.player.playerName.dst = (SDL_Rect) {0,(WINDOW_HEIGHT>>1)+10,0,0};
    app->game.player.playerName.text[0] = '\0';

    /* -- IMAGE TEXTURES */
    join_path(base, "assets/images/help.png", pathbuf, sizeof(pathbuf));
    app->ui.help.helpImg = IMG_LoadTexture(app->renderer, pathbuf);
    assert_texture(app->ui.help.helpImg, pathbuf, app);

    join_path(base, "assets/images/menu_title.png", pathbuf, sizeof(pathbuf));
    app->ui.menu.title.img = IMG_LoadTexture(app->renderer, pathbuf);
    assert_texture(app->ui.menu.title.img, pathbuf, app);

    join_path(base, "assets/images/game_over.png", pathbuf, sizeof(pathbuf));
    app->ui.overlay.gameOver.img = IMG_LoadTexture(app->renderer, pathbuf);
    assert_texture(app->ui.overlay.gameOver.img, pathbuf, app);

    join_path(base, "assets/images/pause.png", pathbuf, sizeof(pathbuf));
    app->ui.overlay.pause.img = IMG_LoadTexture(app->renderer, pathbuf);
    assert_texture(app->ui.overlay.pause.img, pathbuf, app);

    join_path(base, "assets/images/game_complete.png", pathbuf, sizeof(pathbuf));
    app->ui.overlay.gameWin.img = IMG_LoadTexture(app->renderer, pathbuf);
    assert_texture(app->ui.overlay.gameWin.img, pathbuf, app);

    join_path(base, "assets/images/scoreboard.png", pathbuf, sizeof(pathbuf));
    app->ui.scoreboard.rankingImg.img = IMG_LoadTexture(app->renderer, pathbuf);
    assert_texture(app->ui.scoreboard.rankingImg.img, pathbuf, app);

    join_path(base, "assets/images/sprites.png", pathbuf, sizeof(pathbuf));
    app->spritesheet = IMG_LoadTexture(app->renderer, pathbuf);
    assert_texture(app->spritesheet, pathbuf, app);

    /* -- SOUNDS */
    join_path(base, "assets/sounds/pacman_death.wav", pathbuf, sizeof(pathbuf));
    app->sounds.death = Mix_LoadWAV(pathbuf);
    assert_chunk(app->sounds.death, pathbuf, app);

    join_path(base, "assets/sounds/eat_dot.wav", pathbuf, sizeof(pathbuf));
    app->sounds.eatDot = Mix_LoadWAV(pathbuf);
    assert_chunk(app->sounds.eatDot, pathbuf, app);

    join_path(base, "assets/sounds/eat_ghost.wav", pathbuf, sizeof(pathbuf));
    app->sounds.eatGhost = Mix_LoadWAV(pathbuf);
    assert_chunk(app->sounds.eatGhost, pathbuf, app);

    join_path(base, "assets/sounds/pacman_move.wav", pathbuf, sizeof(pathbuf));
    app->sounds.move = Mix_LoadWAV(pathbuf);
    assert_chunk(app->sounds.move, pathbuf, app);

    join_path(base, "assets/sounds/start_level.wav", pathbuf, sizeof(pathbuf));
    app->sounds.start = Mix_LoadWAV(pathbuf);
    assert_chunk(app->sounds.start, pathbuf, app);

    join_path(base, "assets/sounds/win.wav", pathbuf, sizeof(pathbuf));
    app->sounds.win = Mix_LoadWAV(pathbuf);
    assert_chunk(app->sounds.win, pathbuf, app);

    if (base) SDL_free(base);

    center_texture_rect(&app->ui.menu.title,0.575f, -200);
    center_texture_rect(&app->ui.overlay.gameOver,0.5f,0);
    center_texture_rect(&app->ui.overlay.pause,0.4f,0 );
    center_texture_rect(&app->ui.overlay.gameWin, 0.4f,0);
    center_texture_rect(&app->ui.scoreboard.rankingImg,0.4f,0);
    SDL_QueryTexture(app->ui.help.helpImg, NULL, NULL, &app->ui.help.textW,&app->ui.help.textH);

    create_text_texture(&app->ui.menu.play, STANDARD,WHITE,app);
    create_text_texture(&app->ui.menu.help, STANDARD,WHITE,app);
    create_text_texture(&app->ui.menu.exit, STANDARD,WHITE,app);
    create_text_texture(&app->ui.menu.rank, STANDARD,WHITE,app);
    create_text_texture(&app->ui.menu.credit, SMALL,GREY,app);
    create_text_texture(&app->ui.overlay.score, STANDARD,WHITE,app);
    create_text_texture(&app->ui.overlay.lives, STANDARD,WHITE,app);
    create_text_texture(&app->ui.overlay.ready, STANDARD,WHITE,app);
    create_text_texture(&app->ui.scoreboard.hint, STANDARD,WHITE,app);

    app->game.state = STATE_MENU;
    app->game.prevState = STATE_PLAYING;
    app->isRunning = true;
    app->timer.lastTicks = SDL_GetTicks();

    srand((unsigned int)time(NULL));
    load_scores(&app->game.board);
}

void quit_game_application(AppContext *app) {
    if (!app) return;

    save_scores(&app->game.board);

    /* -------- TEXTURES (TEXT) -------- */
    safe_destroy_texture(&app->ui.menu.play.texture);
    safe_destroy_texture(&app->ui.menu.help.texture);
    safe_destroy_texture(&app->ui.menu.rank.texture);
    safe_destroy_texture(&app->ui.menu.exit.texture);
    safe_destroy_texture(&app->ui.menu.credit.texture);

    safe_destroy_texture(&app->ui.overlay.score.texture);
    safe_destroy_texture(&app->ui.overlay.lives.texture);
    safe_destroy_texture(&app->ui.overlay.ready.texture);

    safe_destroy_texture(&app->ui.scoreboard.hint.texture);
    safe_destroy_texture(&app->ui.scoreboard.names.texture);
    safe_destroy_texture(&app->ui.scoreboard.scores.texture);

    safe_destroy_texture(&app->game.player.playerName.texture);

    /* -------- TEXTURES (IMAGES) -------- */
    safe_destroy_texture(&app->spritesheet);
    safe_destroy_texture(&app->ui.help.helpImg);
    safe_destroy_texture(&app->ui.menu.title.img);
    safe_destroy_texture(&app->ui.overlay.gameOver.img);
    safe_destroy_texture(&app->ui.overlay.pause.img);
    safe_destroy_texture(&app->ui.overlay.gameWin.img);
    safe_destroy_texture(&app->ui.scoreboard.rankingImg.img);

    /* -------- SOUNDS -------- */
    safe_free_chunk(&app->sounds.death);
    safe_free_chunk(&app->sounds.eatDot);
    safe_free_chunk(&app->sounds.eatGhost);
    safe_free_chunk(&app->sounds.move);
    safe_free_chunk(&app->sounds.win);
    safe_free_chunk(&app->sounds.start);

    /* -------- TEXT MEMORY -------- */
    safe_free((void**)&app->ui.menu.play.text);
    safe_free((void**)&app->ui.menu.help.text);
    safe_free((void**)&app->ui.menu.rank.text);
    safe_free((void**)&app->ui.menu.exit.text);
    safe_free((void**)&app->ui.menu.credit.text);

    safe_free((void**)&app->ui.overlay.score.text);
    safe_free((void**)&app->ui.overlay.lives.text);
    safe_free((void**)&app->ui.overlay.ready.text);

    safe_free((void**)&app->ui.scoreboard.hint.text);
    safe_free((void**)&app->ui.scoreboard.names.text);
    safe_free((void**)&app->ui.scoreboard.scores.text);

    safe_free((void**)&app->game.player.playerName.text);

    /* -------- SDL OBJECTS -------- */
    if (app->font) {
        TTF_CloseFont(app->font);
        app->font = NULL;
    }

    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
        app->renderer = NULL;
    }

    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
    }

    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}



// ---------------- EVENTS HANDLERS ---------------
static void handle_enter_name_events(AppContext *app) {
    TextLabel *nameLabel = &app->game.player.playerName;
    SDL_Keycode key = app->event.key.keysym.sym;
    uint8_t len = strlen(nameLabel->text);
    // Handle backspace
    if (key == SDLK_BACKSPACE && len > 0) {
        nameLabel->text[--len] = '\0';
        nameLabel->needsUpdate = true;
    }
    // Handle enter/space to confirm name
    else if ((key == SDLK_RETURN || key == SDLK_SPACE) && len > 0) {
        app->timer.startPauseTicks = SDL_GetTicks();
        init_level(&app->game,false);  // Start new game
        app->game.state = STATE_START_LEVEL;
        SDL_StopTextInput();
    }
    // Handle valid character input
    else if (len < MAX_NAME_LEN && is_valid_name_char(key)) {
        nameLabel->text[len++] = (char)key;
        nameLabel->text[len] = '\0';
        nameLabel->needsUpdate = true;
    }
}

static void handle_playing_events(AppContext *app) {
    GameEntity *pacman = &app->game.player.pacman;
    SDL_Keycode key = app->event.key.keysym.sym;

    // Handle movement keys
    switch (key) {
        case SDLK_UP:    pacman->dir = DIR_UP;    break;
        case SDLK_DOWN:  pacman->dir = DIR_DOWN;  break;
        case SDLK_LEFT:  pacman->dir = DIR_LEFT;  break;
        case SDLK_RIGHT: pacman->dir = DIR_RIGHT; break;
            
        case SDLK_ESCAPE:  // Pause game
            app->game.state = STATE_PAUSED;
            app->timer.startPauseTicks = SDL_GetTicks();
            break;
        default:
            break;
    }
}

static void handle_menu_events(AppContext *app) {
    SDL_Keycode key = app->event.key.keysym.sym;

    switch (key) {
        case SDLK_ESCAPE:  // Quit game
            app->isRunning = false;
            break;
            
        case SDLK_s:  // Start game
            app->game.state = STATE_ENTER_NAME;
            app->game.player.playerName.needsUpdate = true;
            SDL_StartTextInput();
            break;

        case SDLK_h:  // Show help
            app->game.state = STATE_HELP;
            break;
            
        case SDLK_r:  // Show ranking
            app->game.state = STATE_RANKING;
            break;
            
        default:
            break;
    }
}

static void handle_paused_events(AppContext *app) {
    SDL_Keycode key = app->event.key.keysym.sym;

    if (key == SDLK_ESCAPE) {  // Return to menu
        add_score_to_board(&app->game);
        app->game.state = STATE_MENU;
    }
    else if (key == SDLK_s) {  // Resume game
        // Adjust game timer for time spent paused
        uint32_t pauseDuration = SDL_GetTicks() - app->timer.startPauseTicks;
        app->timer.accumulator -= pauseDuration;  // Convert ms to seconds
        app->game.state = STATE_PLAYING;
    }
}

void handle_events(AppContext *app) {
    SDL_Event *event = &app->event;
    
    // Handle universal events (like quitting)
    if (event->type == SDL_QUIT) {
        app->isRunning = false;
        return;
    }
    if(event->type != SDL_KEYDOWN) return;
    switch (app->game.state) {
        case STATE_ENTER_NAME:
            handle_enter_name_events(app);
            break;

        case STATE_PLAYING:
            handle_playing_events(app);
            break;
            
        case STATE_MENU:
            handle_menu_events(app);
            break;
            
        case STATE_PAUSED:
            handle_paused_events(app);
            break;
            
        case STATE_HELP:
        case STATE_RANKING:
            if (event->key.keysym.sym == SDLK_ESCAPE) {
                app->game.state = STATE_MENU;
            }
            break;
            
        default:
            break;
    }
}


int main() {
    AppContext app;
    init_game_application(&app);
    while (app.isRunning) {
        // Calculate frame time
        uint32_t currentTicks = SDL_GetTicks();
        app.timer.accumulator += currentTicks - app.timer.lastTicks;
        app.sounds.dotTimer += currentTicks - app.timer.lastTicks;
        app.sounds.moveTimer += currentTicks - app.timer.lastTicks;
        app.timer.lastTicks = currentTicks;
        
        // Event handling
        while (SDL_PollEvent(&app.event)) {
            handle_events(&app);
        }
        
        // Game state updates
        if(app.game.state == STATE_PLAYING){
            update_game(&app);
        }

        // Rendering
        render(&app);
        
        // Frame rate control
        uint32_t frameTime = SDL_GetTicks() - currentTicks;
        if (frameTime < DELTA_TICK_MS) {
            SDL_Delay(DELTA_TICK_MS - frameTime);
        }
    }
    
    quit_game_application(&app);
    return EXIT_SUCCESS;
}
