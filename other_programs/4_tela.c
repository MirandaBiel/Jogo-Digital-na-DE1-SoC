#define _DEFAULT_SOURCE // Garante a disponibilidade de todas as funções
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// --- Configurações da VGA (do seu código base) ---
#define FRAME_BASE      0xC8000000
#define LWIDTH          512      // Largura completa da linha na memória (stride)
#define VISIBLE_WIDTH   320      // Largura visível
#define VISIBLE_HEIGHT  240      // Altura visível
#define PIXEL_SIZE      2        // 2 bytes por pixel (RGB 5-6-5)

// --- Definições de cores (do seu código base) ---
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

// --- Variáveis Globais ---
int mem_fd; 
volatile uint16_t (*tela)[LWIDTH]; 
uint16_t current_color = BLACK; // Cor inicial é preta

// --- Protótipos de Funções ---
void fill_screen();
int set_color(const char *color_name);

// =================================================================================
// --- FUNÇÕES DE INICIALIZAÇÃO E LIMPEZA (Intactas do seu código) ---
// =================================================================================
void cleanup_vga() {
    fill_screen(BLACK); // Limpa a tela ao sair
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
        NULL, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE,
        PROT_READ | PROT_WRITE, MAP_SHARED,
        mem_fd, FRAME_BASE
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

// =================================================================================
// --- FUNÇÕES DE I/O E UTILITÁRIOS (Intactas do seu código) ---
// =================================================================================
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

// =================================================================================
// --- FUNÇÕES PRINCIPAIS DO EXERCÍCIO ---
// =================================================================================
/**
 * @brief Preenche a tela com a cor definida na variável global 'current_color'.
 */
void fill_screen() {
    // Usa a variável global, que é alterada por set_color
    uint16_t color = current_color;
    for (int y = 0; y < VISIBLE_HEIGHT; y++) {
        for (int x = 0; x < VISIBLE_WIDTH; x++) {
            tela[y][x] = color;
        }
    }
}

/**
 * @brief Define a cor atual a partir do nome de uma cor.
 * @param color_name String com o nome da cor.
 * @return 1 se a cor for válida, 0 caso contrário.
 */
int set_color(const char *color_name) {
    uint16_t previous_color = current_color;

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
        printf("ERRO: Cor '%s' invalida!\n", color_name);
        current_color = previous_color; // Restaura a cor anterior
        return 0; // Falha
    }
    printf("Cor definida como %s.\n", color_name);
    return 1; // Sucesso
}

int main() {
    if (init_vga() != 0) {
        return 1;
    }
    
    char input_buffer[100];
    fill_screen(); // Preenche com a cor inicial (preto)

    printf("Programa para preencher a tela VGA.\n");

    while (1) {
        printf("\nDigite um nome de cor ou 'SAIR' para finalizar.\n");
        printf("Cores: RED, GREEN, BLUE, WHITE, BLACK, YELLOW, PURPLE, ORANGE, etc.\n");
        printf("> ");
        fflush(stdout);

        read_line(input_buffer, sizeof(input_buffer));
        to_upper(input_buffer);

        if (strcmp(input_buffer, "SAIR") == 0) {
            break; // Sai do loop while
        }

        // Tenta definir a cor e, se for bem-sucedido, preenche a tela
        if (set_color(input_buffer)) {
            fill_screen();
            printf("Tela preenchida com a nova cor.\n");
        }
    }

    return 0;
}