#define _DEFAULT_SOURCE // Necessário para usleep em alguns sistemas
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// Endereços e Offsets dos Periféricos
#define HW_REGS_BASE 0xFF200000
#define HW_REGS_SPAN 0x00010000
#define SW_OFFSET    0x0040 // Chaves (Switches)
#define KEY_OFFSET   0x0050 // Botões (Push-buttons)
#define HEX3_0_OFFSET 0x0020 // Displays HEX0, 1, 2, 3
#define HEX5_4_OFFSET 0x0030 // Displays HEX4, 5

// Tabela de conversão para 7 segmentos (0-9, A-F)
const unsigned char hex_seg_digits[16] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F, // 9
    0x77, // A
    0x7C, // b (minúsculo para diferenciar de 8)
    0x39, // C
    0x5E, // d (minúsculo para diferenciar de 0)
    0x79, // E
    0x71  // F
};

// Ponteiros globais para os periféricos
volatile void *virtual_base = NULL;
volatile unsigned int *switch_ptr = NULL;
volatile unsigned int *key_ptr = NULL;
volatile unsigned int *hex3_0_ptr = NULL;
volatile unsigned int *hex5_4_ptr = NULL;
int fd = -1;

/**
 * @brief Inicializa o acesso aos periféricos via mapeamento de memória.
 * @return 0 em sucesso, -1 em falha.
 */
int init_peripherals() {
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("Erro ao abrir /dev/mem");
        return -1;
    }

    virtual_base = mmap(NULL, HW_REGS_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, HW_REGS_BASE);
    if (virtual_base == MAP_FAILED) {
        perror("Erro no mmap");
        close(fd);
        return -1;
    }

    // Calcula os endereços virtuais para cada periférico
    switch_ptr = (volatile unsigned int *)(virtual_base + SW_OFFSET);
    key_ptr = (volatile unsigned int *)(virtual_base + KEY_OFFSET);
    hex3_0_ptr = (volatile unsigned int *)(virtual_base + HEX3_0_OFFSET);
    hex5_4_ptr = (volatile unsigned int *)(virtual_base + HEX5_4_OFFSET);

    return 0;
}

/**
 * @brief Libera os recursos ao finalizar o programa.
 */
void cleanup_peripherals() {
    if (hex3_0_ptr) *hex3_0_ptr = 0;
    if (hex5_4_ptr) *hex5_4_ptr = 0;
    
    if (virtual_base != NULL) {
        munmap((void *)virtual_base, HW_REGS_SPAN);
    }
    if (fd != -1) {
        close(fd);
    }
}

int main() {
    if (init_peripherals() != 0) {
        fprintf(stderr, "Falha ao inicializar periféricos.\n");
        return 1;
    }
    
    atexit(cleanup_peripherals);

    // Variáveis de estado da lógica
    int position = 0;   // Posição atual (0-5, para HEX0 a HEX5)
    int direction = 1;  // 1 = Direita (HEX0->5), -1 = Esquerda (HEX5->0)
    unsigned int prev_key_state = 0; // Para detectar o clique do botão

    printf("Iniciado. Use SW0-SW3 para escolher o dígito.\n");
    printf("Pressione KEY0 para inverter o sentido.\n");
    printf("Pressione CTRL+C para sair.\n");

    while (1) {
        // --- LEITURA DAS ENTRADAS ---
        
        // Lê as chaves e mascara para obter apenas os 4 bits menos significativos (0 a F)
        unsigned int digit_to_display = *switch_ptr & 0x0F;
        
        // Converte o dígito lido para o código de 7 segmentos
        unsigned char hex_code = hex_seg_digits[digit_to_display];

        // Lê o estado atual dos botões
        unsigned int current_key_state = *key_ptr;
        
        // --- LÓGICA DE CONTROLE ---

        // Detecção de borda para o KEY0: Ação ocorre apenas no momento do clique
        int key0_pressed_now = (current_key_state & 0b0001);
        int key0_pressed_before = (prev_key_state & 0b0001);

        if (key0_pressed_now && !key0_pressed_before) {
            direction *= -1; // Inverte a direção
            printf("Direção invertida! Sentido: %s\n", direction == 1 ? "Direita" : "Esquerda");
        }
        prev_key_state = current_key_state; // Atualiza o estado anterior

        // --- ATUALIZAÇÃO DA SAÍDA (DISPLAYS) ---
        
        // 1. Apaga todos os displays para garantir que apenas um esteja aceso
        *hex3_0_ptr = 0;
        *hex5_4_ptr = 0;

        // 2. Acende o display na posição correta
        switch (position) {
            case 0: *hex3_0_ptr = hex_code; break;
            case 1: *hex3_0_ptr = hex_code << 8; break;
            case 2: *hex3_0_ptr = hex_code << 16; break;
            case 3: *hex3_0_ptr = hex_code << 24; break;
            case 4: *hex5_4_ptr = hex_code; break;
            case 5: *hex5_4_ptr = hex_code << 8; break;
        }

        // --- ATUALIZAÇÃO DO ESTADO PARA O PRÓXIMO FRAME ---

        // 3. Move para a próxima posição
        position += direction;
        
        // 4. Lógica de wrap-around (volta para o início ao chegar no fim)
        if (position > 5) {
            position = 0;
        }
        if (position < 0) {
            position = 5;
        }
        
        // Imprime o estado atual no console
        printf("Dígito: %X | Posição: HEX%d \r", digit_to_display, position);
        fflush(stdout);

        // Pausa para controlar a velocidade do deslocamento
        usleep(400000); // 0.4 segundos
    }

    return 0;
}