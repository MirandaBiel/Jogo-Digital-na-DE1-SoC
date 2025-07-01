#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// Endereço base para os periféricos de uso geral na DE1-SoC
#define HW_REGS_BASE 0xFF200000
// Tamanho da janela de memória a ser mapeada
#define HW_REGS_SPAN 0x00010000

// Offsets dos periféricos a partir do endereço base
#define LEDR_OFFSET  0x0000 // Offset para os LEDs vermelhos
#define SW_OFFSET    0x0040 // Offset para as chaves (switches)

// Ponteiros globais para os periféricos. 'volatile' é crucial para
// garantir que o compilador não otimize o acesso à memória,
// forçando uma leitura/escrita real no hardware a cada vez.
volatile void *virtual_base = NULL;
volatile unsigned int *led_ptr = NULL;
volatile unsigned int *switch_ptr = NULL;
int fd = -1; // Descritor de arquivo para /dev/mem

/**
 * @brief Inicializa o mapeamento da memória para acessar os periféricos.
 * @return 0 em caso de sucesso, -1 em caso de falha.
 */
int init_peripherals() {
    // Abrir o dispositivo de memória do kernel
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("Erro ao abrir /dev/mem");
        return -1;
    }

    // Mapear a região física dos periféricos para a memória virtual do programa
    virtual_base = mmap(
        NULL,                 // Deixar o kernel escolher o endereço virtual
        HW_REGS_SPAN,         // Tamanho da região a ser mapeada
        PROT_READ | PROT_WRITE, // Queremos ler e escrever na memória
        MAP_SHARED,           // Compartilhar as alterações com outros processos
        fd,                   // Descritor do arquivo /dev/mem
        HW_REGS_BASE          // Endereço físico base a ser mapeado
    );

    if (virtual_base == MAP_FAILED) {
        perror("Erro no mmap");
        close(fd);
        return -1;
    }

    // Calcula os endereços virtuais específicos para os LEDs e as chaves
    // a partir do ponteiro base mapeado.
    led_ptr = (volatile unsigned int *)(virtual_base + LEDR_OFFSET);
    switch_ptr = (volatile unsigned int *)(virtual_base + SW_OFFSET);

    return 0;
}

/**
 * @brief Libera os recursos de memória mapeada e fecha o arquivo.
 */
void cleanup_peripherals() {
    if (virtual_base != NULL) {
        munmap((void *)virtual_base, HW_REGS_SPAN);
    }
    if (fd != -1) {
        close(fd);
    }
}

int main() {
    // Inicializa o acesso ao hardware
    if (init_peripherals() != 0) {
        fprintf(stderr, "Falha ao inicializar periféricos.\n");
        return 1;
    }

    printf("Programa iniciado.\n");
    printf("Movimente as chaves (SW0-SW9) e veja os LEDs (LEDR0-LEDR9) corresponderem.\n");
    printf("Pressione CTRL+C para sair.\n");

    // Loop principal
    while (1) {
        // 1. Ler o valor atual do registrador das chaves.
        // O registrador de 32 bits contém o estado das 10 chaves nos 10 bits menos significativos.
        unsigned int switch_value = *switch_ptr;

        // 2. Escrever o valor lido diretamente no registrador dos LEDs.
        // O hardware da placa se encarrega de acender os LEDs correspondentes.
        *led_ptr = switch_value;

        // 3. Pequena pausa para ser um bom "cidadão" no sistema operacional,
        // evitando o uso de 100% da CPU. 100ms é suficiente.
        usleep(100000); 
    }

    // A cleanup_peripherals() não é chamada aqui porque o loop é infinito.
    // Ela seria útil se o programa tivesse uma condição de saída normal.
    // Pressionar CTRL+C termina o programa abruptamente.
    // Para um código mais robusto, poderíamos capturar o sinal (ex: SIGINT),
    // mas para este exercício simples, não é necessário.

    return 0;
}