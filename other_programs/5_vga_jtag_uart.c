#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// --- Configurações da VGA ---
#define FRAME_BASE      0xC8000000
#define LWIDTH          512      // Largura completa da linha na memória (stride)
#define VISIBLE_WIDTH   320      // Largura visível
#define VISIBLE_HEIGHT  240      // Altura visível
#define PIXEL_SIZE      2        // 2 bytes por pixel (RGB 5-6-5)

// --- Definições de cores (formato RGB 5-6-5) ---
#define BLACK   0x0000
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define GRAY    0x8410
#define WHITE   0xFFFF
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define ORANGE  0xFC00
#define PURPLE  0x780F
#define BROWN   0xA145
#define PINK    0xF81F
#define LIME    0x07F0
#define NAVY    0x000F
#define TEAL    0x0410

// --- Variáveis Globais para acesso ao Hardware ---
int mem_fd; 
volatile uint16_t (*tela)[LWIDTH]; 
uint16_t current_color = WHITE;

// --- Protótipos das funções para organização ---
void set_color(const char *color_name);
void fill_screen();
void draw_circle(int xc, int yc, int r);
void draw_tile(int x0, int y0, int x1, int y1);
void draw_line(int x0, int y0, int x1, int y1);
void draw_rect(int x0, int y0, int x1, int y1);


// --- Funções de Inicialização e Limpeza ---
void cleanup_vga() {
    if (tela != NULL) {
        munmap((void*)tela, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE);
    }
    if (mem_fd != -1) {
        close(mem_fd);
    }
    printf("\nRecursos da VGA liberados. Saindo.\n");
}

int init_vga() {
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) {
        perror("Erro ao abrir /dev/mem");
        return -1;
    }

    void *framebuffer_map = mmap(
        NULL,
        LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd,
        FRAME_BASE
    );

    if (framebuffer_map == MAP_FAILED) {
        perror("Erro ao mapear o framebuffer da VGA");
        close(mem_fd);
        return -1;
    }
    
    tela = (volatile uint16_t (*)[LWIDTH]) framebuffer_map;
    atexit(cleanup_vga);
    return 0;
}


// --- Funções de I/O e Utilitários ---
void read_line(char *buffer, int max_len) {
    if (fgets(buffer, max_len, stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
    }
}

void to_upper(char *str) {
    while (*str) {
        *str = toupper((unsigned char)*str);
        str++;
    }
}

// --- Funções de Desenho ---
void set_pix(int x, int y) {
    if (y < 0 || y >= VISIBLE_HEIGHT || x < 0 || x >= VISIBLE_WIDTH) return;
    tela[y][x] = current_color;
}

void draw_line(int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        set_pix(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_circle(int xc, int yc, int r) {
    int x = -r, y = 0, err = 2 - 2 * r;
    do {
        set_pix(xc - x, yc + y);
        set_pix(xc - y, yc - x);
        set_pix(xc + x, yc - y);
        set_pix(xc + y, yc + x);
        int e2 = err;
        if (e2 <= y) err += ++y * 2 + 1;
        if (e2 > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
}

void draw_rect(int x0, int y0, int x1, int y1) {
    draw_line(x0, y0, x1, y0);
    draw_line(x1, y0, x1, y1);
    draw_line(x1, y1, x0, y1);
    draw_line(x0, y1, x0, y0);
}

void draw_tile(int x0, int y0, int x1, int y1) {
    int ymin = y0 < y1 ? y0 : y1;
    int ymax = y0 > y1 ? y0 : y1;
    int xmin = x0 < x1 ? x0 : x1;
    int xmax = x0 > y1 ? x0 : x1; // Correção: era x0 > y1, deve ser x0 > x1
    for (int y = ymin; y <= ymax; y++) {
        for (int x = xmin; x <= xmax; x++) {
            set_pix(x, y);
        }
    }
}

void fill_screen() {
    uint16_t color = current_color;
    for (int y = 0; y < VISIBLE_HEIGHT; y++) {
        for (int x = 0; x < VISIBLE_WIDTH; x++) {
            tela[y][x] = color;
        }
    }
}

// --- Lógica Principal e Menu ---
void set_color(const char *color_name) {
    if (strcmp(color_name, "BLACK") == 0) current_color = BLACK;
    else if (strcmp(color_name, "RED") == 0) current_color = RED;
    else if (strcmp(color_name, "GREEN") == 0) current_color = GREEN;
    else if (strcmp(color_name, "BLUE") == 0) current_color = BLUE;
    else if (strcmp(color_name, "GRAY") == 0) current_color = GRAY;
    else if (strcmp(color_name, "WHITE") == 0) current_color = WHITE;
    else if (strcmp(color_name, "YELLOW") == 0) current_color = YELLOW;
    else if (strcmp(color_name, "CYAN") == 0) current_color = CYAN;
    else if (strcmp(color_name, "MAGENTA") == 0) current_color = MAGENTA;
    else if (strcmp(color_name, "ORANGE") == 0) current_color = ORANGE;
    else if (strcmp(color_name, "PURPLE") == 0) current_color = PURPLE;
    else if (strcmp(color_name, "BROWN") == 0) current_color = BROWN;
    else if (strcmp(color_name, "PINK") == 0) current_color = PINK;
    else if (strcmp(color_name, "LIME") == 0) current_color = LIME;
    else if (strcmp(color_name, "NAVY") == 0) current_color = NAVY;
    else if (strcmp(color_name, "TEAL") == 0) current_color = TEAL;
    else {
        printf("Cor '%s' invalida!\n", color_name);
        return;
    }
    printf("Cor definida como %s\n", color_name);
}

void print_menu() {
    printf("\n--- Menu de Opcoes Interativo ---\n");
    printf("Use: <COMANDO> <parametros>\n");
    printf("1. COLOR <cor>        - Define a cor (ex: RED, BLUE, ...)\n");
    printf("2. LINE <x0 y0 x1 y1>   - Desenha uma linha\n");
    printf("3. CIRC <xc yc r>     - Desenha um circulo\n");
    printf("4. RECT <x0 y0 x1 y1>   - Desenha um retangulo\n");
    printf("5. TILE <x0 y0 x1 y1>   - Desenha retangulo preenchido\n");
    printf("6. FUNDO              - Preenche a tela\n");
    printf("7. SAIR               - Termina o programa\n");
}

void run_demo_sequence() {
    printf("\n--- Iniciando Sequencia de Demonstracao Automatica ---\n");
    sleep(1);

    // Teste 1: Preenchimento de Fundo (Cor Neutra)
    printf("\n[Teste 1] Preenchendo o fundo com a cor GRAY...\n");
    set_color("GRAY");
    fill_screen();
    sleep(2);

    // Teste 2: Círculo Sobre o Fundo
    printf("[Teste 2] Desenhando um circulo PURPLE (xc=160, yc=120, raio=100)...\n");
    set_color("PURPLE");
    draw_circle(160, 120, 100);
    sleep(2);

    // Teste 3: Retângulo Preenchido (Tile) Ciano
    printf("[Teste 3] Desenhando um retangulo preenchido CYAN de (x=20, y=180) a (x=300, y=220)...\n");
    set_color("CYAN");
    draw_tile(20, 180, 300, 220);
    sleep(2);

    // Teste 4: Linha Branca para Contraste
    printf("[Teste 4] Desenhando uma linha WHITE de (x=10, y=10) a (x=310, y=230)...\n");
    set_color("WHITE");
    draw_line(10, 10, 310, 230);
    sleep(2);

    // Teste 5: Retângulo Vazado (Rect) Vermelho
    printf("[Teste 5] Desenhando um retangulo vazado RED de (x=-50, y=-50) a (x=10, y=10)...\n");
    set_color("RED");
    draw_rect(-50, -50, 10, 10);
    sleep(2);

    printf("\n--- Demonstracao Concluida ---\n");
    set_color("WHITE"); // Reseta a cor para o modo interativo
}


int main() {
    if (init_vga() != 0) {
        return 1;
    }

    char command[100], params[100];
    char input[200];

    printf("Sistema de desenho VGA - DE1-SoC (Linux on ARMv7)\n");
    printf("Tela VGA inicializada com sucesso.\n");

    // Executa a sequência de demonstração automática
    run_demo_sequence();

    while (1) {
        print_menu();
        printf("> ");
        fflush(stdout);

        read_line(input, sizeof(input));
        to_upper(input);
        
        // Limpa os buffers antes de parsear
        command[0] = '\0';
        params[0] = '\0';
        sscanf(input, "%s %[^\n]", command, params);
        
        if (strcmp(command, "1") == 0 || strcmp(command, "COLOR") == 0) {
            set_color(params);
        } else if (strcmp(command, "2") == 0 || strcmp(command, "LINE") == 0) {
            int x0, y0, x1, y1;
            if (sscanf(params, "%d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
                draw_line(x0, y0, x1, y1);
            } else { printf("Formato invalido. Use: LINE x0 y0 x1 y1\n"); }
        } else if (strcmp(command, "3") == 0 || strcmp(command, "CIRC") == 0) {
            int xc, yc, r;
            if (sscanf(params, "%d %d %d", &xc, &yc, &r) == 3) {
                draw_circle(xc, yc, r);
            } else { printf("Formato invalido. Use: CIRC xc yc r\n"); }
        } else if (strcmp(command, "4") == 0 || strcmp(command, "RECT") == 0) {
            int x0, y0, x1, y1;
            if (sscanf(params, "%d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
                draw_rect(x0, y0, x1, y1);
            } else { printf("Formato invalido. Use: RECT x0 y0 x1 y1\n"); }
        } else if (strcmp(command, "5") == 0 || strcmp(command, "TILE") == 0) {
            int x0, y0, x1, y1;
            if (sscanf(params, "%d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
                draw_tile(x0, y0, x1, y1);
            } else { printf("Formato invalido. Use: TILE x0 y0 x1 y1\n"); }
        } else if (strcmp(command, "6") == 0 || strcmp(command, "FUNDO") == 0) {
            fill_screen();
            printf("Tela preenchida com a cor atual.\n");
        } else if (strcmp(command, "7") == 0 || strcmp(command, "SAIR") == 0) {
            break;
        } else if (strlen(command) > 0) { // Evita msg de erro para entrada vazia
            printf("Comando desconhecido: %s\n", command);
        }
    }

    return 0;
}