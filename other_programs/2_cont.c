#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// Endereço base e tamanho da janela de memória dos periféricos
#define HW_REGS_BASE 0xFF200000
#define HW_REGS_SPAN 0x00010000

// Offsets para os displays de 7 segmentos
#define HEX3_0_OFFSET 0x0020 // Controla os displays HEX0, 1, 2, 3
#define HEX5_4_OFFSET 0x0030 // Controla os displays HEX4, 5

// Tabela de conversão de Dígito -> Código de 7 segmentos
const unsigned char seven_seg_digits[10] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F  // 9
};

// Ponteiros globais para os periféricos
volatile void *virtual_base = NULL;
volatile unsigned int *hex3_0_ptr = NULL;
volatile unsigned int *hex5_4_ptr = NULL;
int fd = -1;

/**
 * @brief Inicializa o mapeamento da memória para acessar os periféricos.
 * @return 0 em caso de sucesso, -1 em caso de falha.
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

    // Calcula os endereços virtuais para os registradores dos displays
    hex3_0_ptr = (volatile unsigned int *)(virtual_base + HEX3_0_OFFSET);
    hex5_4_ptr = (volatile unsigned int *)(virtual_base + HEX5_4_OFFSET);

    return 0;
}

/**
 * @brief Libera os recursos de memória mapeada e fecha o arquivo.
 */
void cleanup_peripherals() {
    // Apaga todos os displays ao sair
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
    
    // Registra a função de limpeza para ser chamada ao sair (ex: com CTRL+C)
    atexit(cleanup_peripherals);

    printf("Iniciando contador de 0 a 99 nos displays de 7 segmentos.\n");
    printf("A dezena será exibida no HEX1 e a unidade no HEX0.\n");
    printf("Pressione CTRL+C para sair.\n");
    
    // Apaga os displays HEX5 e HEX4, que não serão usados.
    *hex5_4_ptr = 0;

    // Loop principal para a contagem repetir indefinidamente
    while (1) {
        // Loop que conta de 0 a 99
        for (int count = 0; count <= 99; count++) {
            // 1. Decompor o número atual nos dígitos da dezena e da unidade
            int dezena = count / 10;
            int unidade = count % 10;

            // 2. Buscar os códigos de 7 segmentos para cada dígito
            unsigned char hex_code_dezena = seven_seg_digits[dezena];
            unsigned char hex_code_unidade = seven_seg_digits[unidade];

            // 3. Combinar os códigos. O código da dezena é deslocado 8 bits
            // para a esquerda para ocupar a posição do HEX1 (bits 8-15).
            unsigned int display_value = (hex_code_dezena << 8) | hex_code_unidade;

            // 4. Escrever o valor combinado no registrador que controla HEX0-3.
            // Isso acenderá HEX1 e HEX0 e apagará HEX3 e HEX2 (pois seus bits são 0).
            *hex3_0_ptr = display_value;
            
            // Exibe o número atual no console também
            printf("Exibindo: %02d\r", count);
            fflush(stdout); // Garante que a saída seja impressa imediatamente

            // 5. Pausa para controlar a velocidade da contagem.
            // 500.000 microssegundos = 0.5 segundos.
            usleep(500000);
        }
    }

    return 0;
}