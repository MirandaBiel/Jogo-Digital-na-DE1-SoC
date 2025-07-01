#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>

// =================================================================================
// --- CONFIGURAÇÕES DE HARDWARE E TELA ---
// =================================================================================
#define PERIPHERAL_BASE 0xFF200000
#define PERIPHERAL_SIZE 0x00001000 
#define DEVICES_BUTTONS 0x0050

#define FRAME_BASE      0xC8000000
#define LWIDTH          512
#define VISIBLE_WIDTH   320
#define VISIBLE_HEIGHT  240
#define PIXEL_SIZE      2

// =================================================================================
// --- CONFIGURAÇÕES DO JOGO ---
// =================================================================================
#define GRID_SIZE            8
#define GRID_WIDTH           (VISIBLE_WIDTH / GRID_SIZE)  // 40
#define GRID_HEIGHT          (VISIBLE_HEIGHT / GRID_SIZE) // 30
#define MAX_SNAKE_LENGTH     (GRID_WIDTH * GRID_HEIGHT)
#define INITIAL_SNAKE_LENGTH 5
#define INITIAL_SPEED_DELAY  100000 // usleep delay inicial (maior = mais lento)

// =================================================================================
// --- CORES ---
// =================================================================================
#define BLACK            0x0000
#define WHITE            0xFFFF
#define RED              0xF800 // Comida
#define GREEN            0x07E0 // Corpo da Cobra
#define LIME_GREEN       0xAFE5 // Cabeça da Cobra
#define BG_COLOR         0x10A2 // Azul escuro
#define TEXT_BG_COLOR    0x4208 // Fundo para texto de Game Over
#define TEXT_COLOR       WHITE

// =================================================================================
// --- ESTRUTURAS E ESTADOS DE JOGO ---
// =================================================================================
typedef enum {
    STATE_START_SCREEN,
    STATE_GAME_RUNNING,
    STATE_GAME_OVER
} GameState;

typedef enum {
    UP, RIGHT, DOWN, LEFT
} Direction;

typedef struct {
    int x, y;
} Point;

// =================================================================================
// --- VARIÁVEIS GLOBAIS ---
// =================================================================================
// Hardware
int mem_fd = -1;
volatile uint16_t (*tela)[LWIDTH];
volatile void *peripheral_map = NULL;
volatile unsigned int *key_ptr = NULL;
// Jogo
GameState state;
Point snake_body[MAX_SNAKE_LENGTH];
int snake_length;
Direction direction;
Point food;
int score;

// =================================================================================
// --- FUNÇÕES DE HARDWARE E DESENHO ---
// =================================================================================
void cleanup_resources() {
    // A função atexit() garante que isto seja chamado ao sair
    if (tela) munmap((void*)tela, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE);
    if (peripheral_map) munmap((void*)peripheral_map, PERIPHERAL_SIZE);
    if (mem_fd != -1) close(mem_fd);
    printf("\nRecursos liberados. Saindo do jogo.\n");
}

int init_hardware() {
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) { 
        perror("Erro ao abrir /dev/mem"); 
        return -1; 
    }
    
    // Mapear framebuffer da VGA
    void* vga_map = mmap(NULL, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, FRAME_BASE);
    if (vga_map == MAP_FAILED) { 
        perror("Erro ao mapear VGA"); 
        close(mem_fd); 
        return -1; 
    }
    tela = (volatile uint16_t (*)[LWIDTH])vga_map;

    // Mapear periféricos (botões)
    peripheral_map = mmap(NULL, PERIPHERAL_SIZE, PROT_READ, MAP_SHARED, mem_fd, PERIPHERAL_BASE);
    if (peripheral_map == MAP_FAILED) { 
        perror("Erro ao mapear periféricos"); 
        munmap(vga_map, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE); 
        close(mem_fd); 
        return -1; 
    }
    key_ptr = (volatile unsigned int *)(peripheral_map + DEVICES_BUTTONS);

    atexit(cleanup_resources);
    return 0;
}

void draw_grid_rect(int grid_x, int grid_y, uint16_t color) {
    int start_x = grid_x * GRID_SIZE;
    int start_y = grid_y * GRID_SIZE;
    for (int y = 0; y < GRID_SIZE - 1; y++) { // Deixa 1 pixel de espaço para efeito de grade
        for (int x = 0; x < GRID_SIZE - 1; x++) {
            if ( (start_y + y < VISIBLE_HEIGHT) && (start_x + x < VISIBLE_WIDTH) && (start_y + y >=0) && (start_x +x >= 0) ) {
                tela[start_y + y][start_x + x] = color;
            }
        }
    }
}

void fill_screen(uint16_t color) {
    for (int y = 0; y < VISIBLE_HEIGHT; y++) {
        for (int x = 0; x < VISIBLE_WIDTH; x++) {
            tela[y][x] = color;
        }
    }
}

// =================================================================================
// --- LÓGICA DO JOGO ---
// =================================================================================
void place_food() {
    int on_snake;
    do {
        on_snake = 0;
        food.x = rand() % GRID_WIDTH;
        food.y = rand() % GRID_HEIGHT;
        // Garante que a comida não apareça na cobra
        for (int i = 0; i < snake_length; i++) {
            if (food.x == snake_body[i].x && food.y == snake_body[i].y) {
                on_snake = 1;
                break;
            }
        }
    } while (on_snake);
}

void init_game() {
    state = STATE_GAME_RUNNING;
    snake_length = INITIAL_SNAKE_LENGTH;
    direction = RIGHT;
    score = 0;
    
    // Cria a cobra inicial no centro da tela
    int start_x = GRID_WIDTH / 2;
    int start_y = GRID_HEIGHT / 2;
    for (int i = 0; i < snake_length; i++) {
        snake_body[i].x = start_x - i;
        snake_body[i].y = start_y;
    }
    
    place_food();
    fill_screen(BG_COLOR); // Limpa a tela para um novo jogo
    printf("Jogo iniciado! Pontuacao: 0\n");
}

void update_game_state() {
    // --- Move a cobra ---
    // Cada segmento do corpo se move para a posição do segmento da frente
    for (int i = snake_length - 1; i > 0; i--) {
        snake_body[i] = snake_body[i - 1];
    }

    // Move a cabeça de acordo com a direção atual
    Point* head = &snake_body[0];
    if (direction == UP) head->y--;
    if (direction == DOWN) head->y++;
    if (direction == LEFT) head->x--;
    if (direction == RIGHT) head->x++;

    // --- Verifica colisões ---
    // 1. Colisão com as paredes
    if (head->x < 0 || head->x >= GRID_WIDTH || head->y < 0 || head->y >= GRID_HEIGHT) {
        state = STATE_GAME_OVER;
        return;
    }

    // 2. Colisão com o próprio corpo
    for (int i = 1; i < snake_length; i++) {
        if (head->x == snake_body[i].x && head->y == snake_body[i].y) {
            state = STATE_GAME_OVER;
            return;
        }
    }

    // 3. Colisão com a comida
    if (head->x == food.x && head->y == food.y) {
        if (snake_length < MAX_SNAKE_LENGTH) {
            // Para crescer, simplesmente aumentamos o comprimento.
            // O algoritmo de movimento fará o resto.
            snake_body[snake_length] = snake_body[snake_length - 1];
            snake_length++;
        }
        score += 10;
        printf("Comeu! Pontuacao: %d\n", score);
        place_food();
    }
}

void draw_game_elements() {
    // Desenha a tela inteira (pode ser otimizado para apenas desenhar o que mudou)
    fill_screen(BG_COLOR);
    // Desenha a comida
    draw_grid_rect(food.x, food.y, RED);
    // Desenha a cobra
    for (int i = 0; i < snake_length; i++) {
        uint16_t color = (i == 0) ? LIME_GREEN : GREEN;
        draw_grid_rect(snake_body[i].x, snake_body[i].y, color);
    }
}

// =================================================================================
// --- FUNÇÃO PRINCIPAL ---
// =================================================================================
int main() {
    if (init_hardware() != 0) { return 1; }
    srand(time(NULL));

    state = STATE_START_SCREEN;
    unsigned int prev_key_state = 0x0;

    while (1) {
        unsigned int current_key_state = *key_ptr;
        if (current_key_state & 0b0001) { break; } // Sair com KEY0

        switch (state) {
            case STATE_START_SCREEN: {
                fill_screen(BG_COLOR);
                // Simula "SNAKE"
                draw_grid_rect(GRID_WIDTH/2 - 2, GRID_HEIGHT/2 - 2, LIME_GREEN);
                draw_grid_rect(GRID_WIDTH/2 - 1, GRID_HEIGHT/2 - 2, GREEN);
                draw_grid_rect(GRID_WIDTH/2, GRID_HEIGHT/2 - 2, GREEN);
                draw_grid_rect(GRID_WIDTH/2 + 1, GRID_HEIGHT/2 - 2, GREEN);
                draw_grid_rect(GRID_WIDTH/2 + 2, GRID_HEIGHT/2 - 2, GREEN);
                // Simula "Press KEY1/KEY2 to Start"
                draw_grid_rect(GRID_WIDTH/2, GRID_HEIGHT/2, WHITE);
                
                if ((current_key_state & 0b0110) && !(prev_key_state & 0b0110)) { // KEY1 ou KEY2
                    init_game();
                }
                break;
            }
            case STATE_GAME_RUNNING: {
                // Lógica de controle (virar esquerda/direita com detecção de borda)
                int key1_pressed = (current_key_state & 0b0010) && !(prev_key_state & 0b0010);
                int key2_pressed = (current_key_state & 0b0100) && !(prev_key_state & 0b0100);

                if (key1_pressed) { // Virar à esquerda (não pode inverter direção)
                    if (direction != DOWN && direction != UP) direction = (direction - 1 + 4) % 4; // Evita erro lógico de direção
                    else if (direction != RIGHT && direction != LEFT) direction = (direction - 1 + 4) % 4;
                }
                if (key2_pressed) { // Virar à direita (não pode inverter direção)
                     if (direction != DOWN && direction != UP) direction = (direction + 1) % 4;
                     else if (direction != RIGHT && direction != LEFT) direction = (direction + 1) % 4;
                }

                update_game_state();
                // Apenas desenha se o jogo não acabou nesta iteração
                if (state == STATE_GAME_RUNNING) {
                    draw_game_elements();
                }
                break;
            }
            case STATE_GAME_OVER: {
                 // Fundo da mensagem
                for(int i=0; i<5; i++) for(int j=0; j<12; j++) draw_grid_rect(GRID_WIDTH/2 - 6+j, GRID_HEIGHT/2-2+i, TEXT_BG_COLOR);
                // "GAME OVER"
                draw_grid_rect(GRID_WIDTH/2 - 4, GRID_HEIGHT/2-1, RED);
                draw_grid_rect(GRID_WIDTH/2 - 2, GRID_HEIGHT/2-1, RED);
                draw_grid_rect(GRID_WIDTH/2, GRID_HEIGHT/2-1, RED);
                draw_grid_rect(GRID_WIDTH/2 + 2, GRID_HEIGHT/2-1, RED);
                draw_grid_rect(GRID_WIDTH/2 + 4, GRID_HEIGHT/2-1, RED);

                printf("FIM DE JOGO! Pontuacao final: %d. Pressione KEY1 ou KEY2 para jogar novamente.\n", score);
                
                // Espera um pressionar de tecla para reiniciar
                if ((current_key_state & 0b0110) && !(prev_key_state & 0b0110)) {
                    state = STATE_START_SCREEN;
                }
                break;
            }
        }

        prev_key_state = current_key_state;
        // A velocidade aumenta conforme o score (diminuindo o delay)
        int current_delay = INITIAL_SPEED_DELAY - (score * 200);
        if (current_delay < 40000) current_delay = 40000; // Limite máximo de velocidade
        usleep(current_delay);
    }
    
    return 0; // atexit() cuidará da limpeza
}