#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <math.h>

#define PERIPHERAL_BASE 0xFF200000
#define PERIPHERAL_SIZE 0x00010000 
#define DEVICES_BUTTONS 0x0050
#define SWITCHES_OFFSET 0x0040
#define HEX3_0_OFFSET   0x0020
#define HEX5_4_OFFSET   0x0030

#define FRAME_BASE      0xC8000000
#define LWIDTH          512
#define VISIBLE_WIDTH   320
#define VISIBLE_HEIGHT  240
#define PIXEL_SIZE      2

#define SPEED_LEVEL_0    2 
#define SPEED_LEVEL_1    3 
#define SPEED_LEVEL_2    4 
#define SPEED_LEVEL_3    5 

#define GAP_EASIEST 100
#define GAP_EASY     90
#define GAP_HARD     80
#define GAP_HARDEST  70

#define NUM_PIPES_EASY 2
#define NUM_PIPES_HARD 3
#define SPACING_EASY 220
#define SPACING_HARD 130 

#define GRAVITY_EASY 0.5
#define GRAVITY_HARD 0.35

#define JUMP_EASY -5.5
#define JUMP_HARD -7.0

#define RADIUS_EASY 10
#define RADIUS_HARD 13

#define P1_X_POS         60
#define P2_X_POS         90
#define OBSTACLE_WIDTH   50

#define WHITE    0xFFFF
#define GREEN    0x07E0 
#define P1_COLOR 0xFFE0
#define P2_COLOR 0xF800
#define BEAK_COLOR 0xFC00
#define DEAD_COLOR 0x8410
#define SKY_BLUE 0x841F
#define BLACK    0x0000

#define FONT_WIDTH 3
#define FONT_HEIGHT 5
#define FONT_CHAR_SPACING 2 
#define FONT_SCALE 2        

const int font_3x5[10][FONT_HEIGHT][FONT_WIDTH] = {
    {{1,1,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}}, {{0,1,0},{1,1,0},{0,1,0},{0,1,0},{1,1,1}}, 
    {{1,1,1},{0,0,1},{1,1,1},{1,0,0},{1,1,1}}, {{1,1,1},{0,0,1},{0,1,1},{0,0,1},{1,1,1}}, 
    {{1,0,1},{1,0,1},{1,1,1},{0,0,1},{0,0,1}}, {{1,1,1},{1,0,0},{1,1,1},{0,0,1},{1,1,1}},
    {{1,1,1},{1,0,0},{1,1,1},{1,0,1},{1,1,1}}, {{1,1,1},{0,0,1},{0,1,0},{0,1,0},{0,1,0}},
    {{1,1,1},{1,0,1},{1,1,1},{1,0,1},{1,1,1}}, {{1,1,1},{1,0,1},{1,1,1},{0,0,1},{1,1,1}}
};
const unsigned char seven_seg_digits[10] = {
    0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F
};

typedef enum { GAME_RUNNING, GAME_OVER } GameState;
typedef struct { double y, velocity_y; int alive; } Bird;
typedef struct { int x, gap_y, scored; } Obstacle;

int mem_fd = -1;
volatile uint16_t (*tela)[LWIDTH] = NULL;
volatile void *peripheral_map = NULL;
volatile unsigned int *key_ptr = NULL;
volatile unsigned int *sw_ptr = NULL;
volatile unsigned int *hex3_0_ptr = NULL;
volatile unsigned int *hex5_4_ptr = NULL;

void cleanup_resources() {
    if (hex3_0_ptr) *hex3_0_ptr = 0;
    if (hex5_4_ptr) *hex5_4_ptr = 0;
    if (tela) munmap((void*)tela, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE);
    if (peripheral_map) munmap((void*)peripheral_map, PERIPHERAL_SIZE);
    if (mem_fd != -1) close(mem_fd);
    printf("\nRecursos liberados. Saindo do jogo.\n");
}

int init_hardware() {
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) { perror("Erro ao abrir /dev/mem"); return -1; }
    
    void* vga_map = mmap(NULL, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, FRAME_BASE);
    if (vga_map == MAP_FAILED) { perror("Erro ao mapear VGA"); close(mem_fd); return -1; }
    tela = (volatile uint16_t (*)[LWIDTH])vga_map;

    peripheral_map = mmap(NULL, PERIPHERAL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, PERIPHERAL_BASE);
    if (peripheral_map == MAP_FAILED) { perror("Erro ao mapear periféricos"); munmap(vga_map, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE); close(mem_fd); return -1; }
    
    key_ptr = (volatile unsigned int *)(peripheral_map + DEVICES_BUTTONS);
    sw_ptr = (volatile unsigned int *)(peripheral_map + SWITCHES_OFFSET);
    hex3_0_ptr = (volatile unsigned int *)(peripheral_map + HEX3_0_OFFSET);
    hex5_4_ptr = (volatile unsigned int *)(peripheral_map + HEX5_4_OFFSET);

    atexit(cleanup_resources);
    return 0;
}

void set_pix(int x, int y, uint16_t color) {
    if (y >= 0 && y < VISIBLE_HEIGHT && x >= 0 && x < VISIBLE_WIDTH) {
        tela[y][x] = color;
    }
}

void draw_filled_rect(int x0, int y0, int x1, int y1, uint16_t color) {
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            set_pix(x, y, color);
        }
    }
}

void draw_circle(int xc, int yc, int r, uint16_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                set_pix(xc + x, yc + y, color);
            }
        }
    }
}

void draw_digit(int digit, int x, int y, uint16_t color) {
    if (digit < 0 || digit > 9) return;
    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (font_3x5[digit][row][col] == 1) {
                draw_filled_rect(x + (col * FONT_SCALE), y + (row * FONT_SCALE),
                                 x + (col * FONT_SCALE) + FONT_SCALE, y + (row * FONT_SCALE) + FONT_SCALE,
                                 color);
            }
        }
    }
}

void draw_score(int score, int x, int y, uint16_t color) {
    char score_text[10];
    sprintf(score_text, "%d", score);
    int len = strlen(score_text);
    int current_x = x;

    for (int i = len - 1; i >= 0; i--) {
        int digit = score_text[i] - '0';
        int char_width = (FONT_WIDTH * FONT_SCALE);
        current_x -= char_width;
        draw_digit(digit, current_x, y, color);
        current_x -= FONT_CHAR_SPACING;
    }
}

void update_hex_displays(int score1, int score2) {
    if (score1 > 99) score1 = 99;
    if (score2 > 99) score2 = 99;

    int p1_dezena = score1 / 10;
    int p1_unidade = score1 % 10;
    unsigned char p1_code_d = seven_seg_digits[p1_dezena];
    unsigned char p1_code_u = seven_seg_digits[p1_unidade];
    *hex3_0_ptr = (p1_code_d << 8) | p1_code_u;

    int p2_dezena = score2 / 10;
    int p2_unidade = score2 % 10;
    unsigned char p2_code_d = seven_seg_digits[p2_dezena];
    unsigned char p2_code_u = seven_seg_digits[p2_unidade];
    *hex5_4_ptr = (p2_code_d << 8) | p2_code_u;
}

void fill_screen(uint16_t color) {
    for (int y = 0; y < VISIBLE_HEIGHT; y++) {
        for (int x = 0; x < VISIBLE_WIDTH; x++) {
            tela[y][x] = color;
        }
    }
}

void draw_flappy_bird(int x, int y, uint16_t body_color, int bird_radius) {
    draw_circle(x, y, bird_radius, body_color);
    draw_circle(x + bird_radius / 2, y - bird_radius / 3, bird_radius / 4, WHITE);
    set_pix(x + bird_radius / 2, y - bird_radius / 3, BLACK);
    draw_filled_rect(x + bird_radius, y - 2, x + bird_radius + 5, y + 2, BEAK_COLOR);
    draw_filled_rect(x - bird_radius / 2, y, x, y + 5, WHITE);
}

int check_collision(const Bird* bird, int bird_x_pos, const Obstacle* obs, int bird_radius, int gap_height) {
    if ((bird->y - bird_radius) < 0 || (bird->y + bird_radius) > VISIBLE_HEIGHT) {
        return 1;
    }
    if (bird_x_pos + bird_radius > obs->x && bird_x_pos - bird_radius < obs->x + OBSTACLE_WIDTH) {
        if (bird->y - bird_radius < obs->gap_y || bird->y + bird_radius > obs->gap_y + gap_height) {
            return 1;
        }
    }
    return 0;
}

void reset_game(Bird* p1, Bird* p2, Obstacle obstacles[], int num_obstacles, int* score1, int* score2, int is_two_player_mode, int spacing, int gap_height) {
    p1->y = VISIBLE_HEIGHT / 2.0;
    p1->velocity_y = 0;
    p1->alive = 1;

    *score1 = 0;
    *score2 = 0;

    if (is_two_player_mode) {
        p2->y = VISIBLE_HEIGHT / 2.0;
        p2->velocity_y = 0;
        p2->alive = 1;
    } else {
        p2->alive = 0; 
    }

    for (int i = 0; i < num_obstacles; i++) {
        obstacles[i].x = VISIBLE_WIDTH + 150 + i * spacing;
        obstacles[i].gap_y = rand() % (VISIBLE_HEIGHT - gap_height - 60) + 30;
        obstacles[i].scored = 0;
    }
    
    if (num_obstacles < 3) {
        obstacles[2].x = -OBSTACLE_WIDTH -10;
    }
    
    printf("Iniciando Jogo! P1 (Amarelo) usa KEY1. ");
    if(is_two_player_mode) printf("P2 (Vermelho) usa KEY2. ");
    printf("KEY0 para Sair.\n");
    fflush(stdout);
}

int main() {
    if (init_hardware() != 0) { return 1; }

    uint16_t* back_buffer = malloc(LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE);
    if (!back_buffer) { perror("Erro ao alocar o back buffer"); return 1; }

    srand(time(NULL));

    Obstacle obstacles[3]; 

    Bird player1, player2;
    GameState state = GAME_RUNNING;
    unsigned int prev_key_state = 0x0;
    
    int score_p1, score_p2;
    int high_score_p1 = 0, high_score_p2 = 0;
    
    int is_two_player_mode = (*(volatile unsigned int *)(peripheral_map + SWITCHES_OFFSET)) & 0x100;
    reset_game(&player1, &player2, obstacles, NUM_PIPES_EASY, &score_p1, &score_p2, is_two_player_mode, SPACING_EASY, GAP_EASY);
    update_hex_displays(high_score_p1, high_score_p2);

    while (1) {
        unsigned int current_key_state = *key_ptr;
        unsigned int switch_state = *sw_ptr; 
        
        is_two_player_mode = switch_state & 0x100;
        int is_paused = switch_state & 0x200;

        int num_obstacles;
        int current_spacing;
        if (switch_state & (1 << 4)) {
            num_obstacles = NUM_PIPES_HARD;
            current_spacing = SPACING_HARD;
        } else {
            num_obstacles = NUM_PIPES_EASY;
            current_spacing = SPACING_EASY;
        }
        
        int current_speed;
        switch (switch_state & 0b11) {
            case 0b00: current_speed = SPEED_LEVEL_0; break;
            case 0b01: current_speed = SPEED_LEVEL_1; break;
            case 0b10: current_speed = SPEED_LEVEL_2; break;
            case 0b11: current_speed = SPEED_LEVEL_3; break;
        }

        int current_gap_height;
        switch ((switch_state >> 2) & 0b11) {
            case 0b00: current_gap_height = GAP_EASIEST; break;
            case 0b01: current_gap_height = GAP_EASY;    break;
            case 0b10: current_gap_height = GAP_HARD;    break;
            case 0b11: current_gap_height = GAP_HARDEST; break;
        }

        double current_gravity = (switch_state & (1 << 5)) ? GRAVITY_HARD : GRAVITY_EASY;
        double current_jump_velocity = (switch_state & (1 << 6)) ? JUMP_HARD : JUMP_EASY;
        int current_bird_radius = (switch_state & (1 << 7)) ? RADIUS_HARD : RADIUS_EASY;

        if (current_key_state & 0b0001) { break; } 

        switch(state) {
            case GAME_RUNNING: {
                if (!is_paused) {
                    if (player1.alive) {
                        if ((current_key_state & 0b0010) && !(prev_key_state & 0b0010)) {
                            player1.velocity_y = current_jump_velocity;
                        }
                    }
                    if (player2.alive) {
                        if ((current_key_state & 0b0100) && !(prev_key_state & 0b0100)) {
                            player2.velocity_y = current_jump_velocity;
                        }
                    }
                    if(player1.alive) {
                        player1.velocity_y += current_gravity;
                        player1.y += player1.velocity_y;
                    }
                    if(player2.alive) {
                        player2.velocity_y += current_gravity;
                        player2.y += player2.velocity_y;
                    }
                    for (int i = 0; i < num_obstacles; i++) {
                        obstacles[i].x -= current_speed;
                        if (!obstacles[i].scored && obstacles[i].x + OBSTACLE_WIDTH < P1_X_POS) {
                            obstacles[i].scored = 1;
                            if (player1.alive) score_p1++;
                            if (player2.alive) score_p2++;
                        }
                        if (obstacles[i].x + OBSTACLE_WIDTH < 0) {
                            int max_x = 0;
                            for (int j = 0; j < num_obstacles; j++) {
                                if (obstacles[j].x > max_x) {
                                    max_x = obstacles[j].x;
                                }
                            }
                            obstacles[i].x = max_x + current_spacing;
                            obstacles[i].gap_y = rand() % (VISIBLE_HEIGHT - current_gap_height - 60) + 30;
                            obstacles[i].scored = 0;
                        }
                    }
                    for (int i = 0; i < num_obstacles; i++) {
                        if (player1.alive && check_collision(&player1, P1_X_POS, &obstacles[i], current_bird_radius, current_gap_height)) {
                            player1.alive = 0;
                        }
                        if (player2.alive && check_collision(&player2, P2_X_POS, &obstacles[i], current_bird_radius, current_gap_height)) {
                            player2.alive = 0;
                        }
                    }
                    int game_is_over = 0;
                    if(is_two_player_mode) { if(!player1.alive && !player2.alive) game_is_over = 1; } 
                    else { if(!player1.alive) game_is_over = 1; }

                    if(game_is_over) {
                        state = GAME_OVER;
                        if (score_p1 > high_score_p1) high_score_p1 = score_p1;
                        if (score_p2 > high_score_p2) high_score_p2 = score_p2;
                    }
                }

                volatile uint16_t (*original_tela_ptr)[LWIDTH] = tela;
                tela = (volatile uint16_t (*)[LWIDTH])back_buffer;

                fill_screen(SKY_BLUE);
                for (int i = 0; i < num_obstacles; i++) {
                    draw_filled_rect(obstacles[i].x, 0, obstacles[i].x + OBSTACLE_WIDTH, obstacles[i].gap_y, GREEN);
                    draw_filled_rect(obstacles[i].x, obstacles[i].gap_y + current_gap_height, obstacles[i].x + OBSTACLE_WIDTH, VISIBLE_HEIGHT, GREEN);
                }
                
                if (player1.alive) draw_flappy_bird(P1_X_POS, (int)player1.y, P1_COLOR, current_bird_radius);
                if (player2.alive) draw_flappy_bird(P2_X_POS, (int)player2.y, P2_COLOR, current_bird_radius);
                
                if(is_paused) {
                    draw_filled_rect(145, 100, 155, 140, WHITE);
                    draw_filled_rect(165, 100, 175, 140, WHITE);
                }
                
                draw_score(score_p1 + score_p2, VISIBLE_WIDTH - 10, 10, WHITE);
                
                tela = original_tela_ptr;
                memcpy((void*)tela, back_buffer, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE);
                update_hex_displays(high_score_p1, high_score_p2);
                break;
            } 
            case GAME_OVER: {
                int restart_key_pressed = (current_key_state & 0b0110) && !(prev_key_state & 0b0110);
                if (restart_key_pressed) {
                    reset_game(&player1, &player2, obstacles, num_obstacles, &score_p1, &score_p2, is_two_player_mode, current_spacing, current_gap_height);
                    state = GAME_RUNNING;
                }
                break;
            }
        } 
        prev_key_state = current_key_state;
        usleep(16666);
    }
    
    free(back_buffer);
    return 0;
}