/**
 * @file flappy_commented.c
 * @brief Jogo Flappy Bird para 2 jogadores na placa DE1-SoC.
 * * Implementado em C, rodando no Linux embarcado no HPS (Hard Processor System).
 * Controla os periféricos do FPGA (VGA, botões, switches, displays de 7 segmentos)
 * através de mapeamento de memória (/dev/mem).
 * * Funcionalidades:
 * - Modo 1 ou 2 jogadores (controlado por SW8).
 * - Pausa do jogo (controlado por SW9).
 * - Dificuldade totalmente ajustável via switches (SW0-SW7).
 * - Gráficos sem flicker usando a técnica de double buffering.
 */

/**
 * Habilita definições e funcionalidades modernas do sistema (padrão POSIX, etc.) que,
 * por padrão, podem ser escondidas por flags de compilação estritas como -std=c99.
 * É essencial para garantir que a função `usleep` seja declarada corretamente.
 */
#define _DEFAULT_SOURCE

#include <stdio.h>      // Standard Input/Output: Para funções de entrada e saída como `printf`, `sprintf` e `perror`.
#include <stdlib.h>     // Standard Library: Para alocação dinâmica de memória (`malloc`, `free`), números aleatórios (`rand`, `srand`) e `atexit`.
#include <string.h>     // String Handling: Para manipulação de strings e memória, como `memcpy` (essencial para o double buffering) e `strlen`.
#include <stdint.h>     // Standard Integer Types: Fornece tipos de dados com tamanho exato, como `uint16_t` para representar os pixels de 16 bits (RGB565).
#include <unistd.h>     // UNIX Standard: Fornece acesso a chamadas de sistema POSIX, como `usleep` (para controlar o FPS) e `close` (para fechar o file descriptor).
#include <fcntl.h>      // File Control: Usado para a função `open` e suas flags (ex: `O_RDWR`, `O_SYNC`) para abrir o arquivo especial `/dev/mem`.
#include <sys/mman.h>   // Memory Management: Essencial para as funções `mmap` e `munmap`, que mapeiam os endereços de memória físicos do hardware na memória virtual do programa.
#include <time.h>       // Time: Usado para a função `time()`, que serve como semente para o gerador de números aleatórios (`srand`), garantindo que a posição dos canos seja diferente a cada execução.
#include <math.h>       // Math: Inclui funções matemáticas. É uma boa prática para a fórmula do círculo (`x*x + y*y`) e requer a flag `-lm` durante a compilação.

// =================================================================================
// --- SEÇÃO 1: DEFINIÇÕES DE HARDWARE E TELA ---
// Define os endereços de memória e dimensões físicas da tela.
// =================================================================================
#define PERIPHERAL_BASE 0xFF200000 // Endereço base da ponte HPS-para-FPGA (Lightweight)
#define PERIPHERAL_SIZE 0x00010000  // Tamanho da janela de memória dos periféricos
#define DEVICES_BUTTONS 0x0050     // Offset dos botões KEY
#define SWITCHES_OFFSET 0x0040     // Offset dos switches SW
#define HEX3_0_OFFSET   0x0020     // Offset dos displays HEX3-0
#define HEX5_4_OFFSET   0x0030     // Offset dos displays HEX5-4

#define FRAME_BASE      0xC8000000 // Endereço base do framebuffer da VGA
#define LWIDTH          512        // Largura FÍSICA da linha de memória da VGA. Usado para cálculos de ponteiro.
#define VISIBLE_WIDTH   320        // Largura VISÍVEL do jogo na tela.
#define VISIBLE_HEIGHT  240        // Altura VISÍVEL do jogo na tela.
#define PIXEL_SIZE      2          // Tamanho de cada pixel em bytes (RGB565 = 16 bits = 2 bytes)

// =================================================================================
// --- SEÇÃO 2: PARÂMETROS DE DIFICULDADE DINÂMICA ---
// Define os valores para cada nível de dificuldade que pode ser selecionado pelos switches.
// =================================================================================

// Velocidade de movimento dos canos (controlado por SW1, SW0)
#define SPEED_LEVEL_0    2 
#define SPEED_LEVEL_1    3 
#define SPEED_LEVEL_2    4 
#define SPEED_LEVEL_3    5 

// Abertura vertical entre os canos (controlado por SW3, SW2)
#define GAP_EASIEST 100 // 00: Muito fácil
#define GAP_EASY     90 // 01: Fácil
#define GAP_HARD     80 // 10: Difícil
#define GAP_HARDEST  70 // 11: Muito difícil

// Modo de canos: quantidade e espaçamento horizontal (controlado por SW4)
#define NUM_PIPES_EASY 2
#define NUM_PIPES_HARD 3
#define SPACING_EASY 220
#define SPACING_HARD 130 

// Força da gravidade que puxa o pássaro (controlado por SW5)
#define GRAVITY_EASY 0.35 // Mais "flutuante"
#define GRAVITY_HARD 0.5  // Mais "pesado"

// Impulso para cima a cada pulo (controlado por SW6)
#define JUMP_EASY -7.0 // Pulo mais forte
#define JUMP_HARD -5.5 // Pulo mais fraco

// Raio do pássaro, afetando a área de colisão (controlado por SW7)
#define RADIUS_EASY 10 // Alvo menor
#define RADIUS_HARD 13 // Alvo maior

// Constantes fixas do jogo
#define P1_X_POS         60
#define P2_X_POS         90
#define OBSTACLE_WIDTH   50

// =================================================================================
// --- SEÇÃO 3: CORES, FONTES E TABELA DE 7 SEGMENTOS ---
// Define as cores no formato RGB565 e os dados para desenhar fontes e placares.
// =================================================================================
#define WHITE    0xFFFF
#define GREEN    0x07E0 
#define P1_COLOR 0xFFE0
#define P2_COLOR 0xF800
#define BEAK_COLOR 0xFC00
#define DEAD_COLOR 0x8410
#define SKY_BLUE 0x841F
#define BLACK    0x0000

#define FONT_WIDTH 3         // Define a largura em pixels da matriz base de cada caractere da fonte (3x5).
#define FONT_HEIGHT 5        // Define a altura em pixels da matriz base de cada caractere da fonte (3x5).
#define FONT_CHAR_SPACING 2  // Define o espaçamento em pixels a ser adicionado entre cada caractere desenhado na tela.
#define FONT_SCALE 2         // Define o fator de multiplicação para o tamanho da fonte. Cada pixel da matriz 3x5 será desenhado como um bloco de 2x2 pixels.     

// Matriz usada como "bitmap" para desenhar os algarismos de 0 a 9 na tela.
const int font_3x5[10][FONT_HEIGHT][FONT_WIDTH] = {
    {{1,1,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}}, {{0,1,0},{1,1,0},{0,1,0},{0,1,0},{1,1,1}}, 
    {{1,1,1},{0,0,1},{1,1,1},{1,0,0},{1,1,1}}, {{1,1,1},{0,0,1},{0,1,1},{0,0,1},{1,1,1}}, 
    {{1,0,1},{1,0,1},{1,1,1},{0,0,1},{0,0,1}}, {{1,1,1},{1,0,0},{1,1,1},{0,0,1},{1,1,1}},
    {{1,1,1},{1,0,0},{1,1,1},{1,0,1},{1,1,1}}, {{1,1,1},{0,0,1},{0,1,0},{0,1,0},{0,1,0}},
    {{1,1,1},{1,0,1},{1,1,1},{1,0,1},{1,1,1}}, {{1,1,1},{1,0,1},{1,1,1},{0,0,1},{1,1,1}}
};
// Tabela de consulta para converter um número (0-9) no código para acender os displays de 7 segmentos.
const unsigned char seven_seg_digits[10] = {
    0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F
};

// =================================================================================
// --- SEÇÃO 4: ESTRUTURAS DE DADOS E ESTADOS DE JOGO ---
// =================================================================================

// Define os possíveis estados da máquina de estados principal do jogo.
typedef enum { GAME_RUNNING, GAME_OVER } GameState;
// Estrutura para armazenar todas as informações de um pássaro.
typedef struct { double y, velocity_y; int alive; } Bird;
// Estrutura para armazenar todas as informações de um par de canos.
typedef struct { int x, gap_y, scored; } Obstacle;

// =================================================================================
// --- SEÇÃO 5: VARIÁVEIS GLOBAIS DE HARDWARE ---
// Ponteiros voláteis para acessar diretamente os endereços de memória dos periféricos.
// "volatile" impede que o compilador otimize o acesso, forçando a leitura do hardware a cada vez.
// =================================================================================
/**
 * @brief File descriptor para o arquivo de memória do sistema (/dev/mem).
 * É usado pela função `open` para obter acesso à memória física do sistema.
 * Inicializado como -1 (um valor inválido) para indicar que ainda não foi aberto.
 */
int mem_fd = -1;

/**
 * @brief Ponteiro para o framebuffer da VGA.
 * Aponta para o início da memória de vídeo, permitindo o desenho direto na tela.
 * A sintaxe `(*tela)[LWIDTH]` o trata como uma matriz 2D para facilitar o acesso por coordenadas (tela[y][x]).
 */
volatile uint16_t (*tela)[LWIDTH] = NULL;

/**
 * @brief Ponteiro genérico para o início da região de memória dos periféricos.
 * Após o mapeamento com `mmap`, ele serve como base para calcular os endereços dos outros periféricos (botões, switches, etc.).
 */
volatile void *peripheral_map = NULL;

/**
 * @brief Ponteiro para os botões (KEYs).
 * Após o mapeamento, a leitura de `*key_ptr` retorna o estado atual dos botões.
 */
volatile unsigned int *key_ptr = NULL;

/**
 * @brief Ponteiro para os switches (SW).
 * A leitura de `*sw_ptr` retorna o estado dos 10 switches como um único inteiro.
 */
volatile unsigned int *sw_ptr = NULL;

/**
 * @brief Ponteiro para os displays de 7 segmentos HEX3, HEX2, HEX1 e HEX0.
 * Escrever um valor em `*hex3_0_ptr` atualiza o que é exibido nestes displays.
 */
volatile unsigned int *hex3_0_ptr = NULL;

/**
 * @brief Ponteiro para os displays de 7 segmentos HEX5 e HEX4.
 * Escrever um valor em `*hex5_4_ptr` atualiza o que é exibido nestes displays.
 */
volatile unsigned int *hex5_4_ptr = NULL;

// =================================================================================
// --- SEÇÃO 6: FUNÇÕES DE HARDWARE E DESENHO ---
// =================================================================================

/**
 * @brief Libera todos os recursos do sistema que foram alocados durante a execução.
 * * Esta função é essencial para uma finalização "limpa" do programa. Ela garante que
 * os mapeamentos de memória sejam desfeitos e que os arquivos abertos sejam fechados,
 * devolvendo os recursos ao sistema operacional. É tipicamente registrada com `atexit()`
 * para ser executada automaticamente quando o programa termina.
 */
void cleanup_resources() {
    // Verifica se o ponteiro para os displays HEX3-0 é válido antes de usá-lo.
    if (hex3_0_ptr) *hex3_0_ptr = 0;

    // Verifica se o ponteiro para os displays HEX5-4 é válido.
    if (hex5_4_ptr) *hex5_4_ptr = 0;

    // Verifica se o ponteiro para a tela (framebuffer) foi mapeado.
    if (tela) munmap((void*)tela, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE); // Libera o mapeamento de memória da VGA.

    // Verifica se o ponteiro para a base dos periféricos foi mapeado.
    if (peripheral_map) munmap((void*)peripheral_map, PERIPHERAL_SIZE); // Libera o mapeamento de memória dos periféricos.

    // Verifica se o arquivo /dev/mem foi aberto com sucesso.
    if (mem_fd != -1) close(mem_fd); // Fecha o file descriptor, liberando o acesso ao arquivo de memória.

    // Informa ao usuário que a limpeza foi concluída com sucesso.
    printf("\nRecursos liberados. Saindo do jogo.\n");
}

/**
 * @brief Inicializa o acesso a todos os periféricos de hardware.
 * Abre o arquivo de memória /dev/mem e usa a chamada de sistema mmap para mapear 
 * os endereços físicos do hardware (VGA, botões, etc.) em ponteiros virtuais que
 * o programa em C pode usar. Esta função é o coração da interação entre o
 * software e o hardware.
 * @return 0 em caso de sucesso, -1 em caso de falha.
 */
int init_hardware() {
    // Abre o arquivo especial /dev/mem, que representa a memória física do sistema.
    // O_RDWR permite ler e escrever. O_SYNC garante que as escritas sejam imediatas
    // e não fiquem em cache, o que é crucial para o controle de hardware.
    // Requer privilégios de superusuário (sudo) para ser executado.
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) {
        // perror exibe uma mensagem de erro do sistema (ex: "Permission denied").
        perror("Erro ao abrir /dev/mem");
        return -1; // Retorna um código de erro.
    }
    
    // Mapeia a memória do framebuffer da VGA na memória virtual do nosso programa.
    // A partir de agora, escrever nos endereços de 'vga_map' é o mesmo que desenhar na tela.
    void* vga_map = mmap(NULL, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, FRAME_BASE);
    if (vga_map == MAP_FAILED) {
        perror("Erro ao mapear VGA");
        close(mem_fd); // Se falhar, fecha o arquivo de memória antes de sair.
        return -1;
    }
    // Faz um "cast" do ponteiro genérico (void*) para um tipo específico que nos permite
    // usar a sintaxe de matriz 2D (tela[y][x]), facilitando o desenho.
    tela = (volatile uint16_t (*)[LWIDTH])vga_map;

    // Mapeia a região de memória onde todos os outros periféricos (botões, switches, etc.) estão localizados.
    peripheral_map = mmap(NULL, PERIPHERAL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, PERIPHERAL_BASE);
    if (peripheral_map == MAP_FAILED) {
        perror("Erro ao mapear periféricos");
        // Se este mapeamento falhar, precisamos liberar o mapeamento anterior (VGA) antes de sair.
        munmap(vga_map, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE);
        close(mem_fd);
        return -1;
    }
    
    // Calcula os endereços virtuais exatos para cada periférico usando o ponteiro base
    // do mapa de periféricos e o offset (deslocamento) específico de cada um.
    key_ptr = (volatile unsigned int *)(peripheral_map + DEVICES_BUTTONS);
    sw_ptr = (volatile unsigned int *)(peripheral_map + SWITCHES_OFFSET);
    hex3_0_ptr = (volatile unsigned int *)(peripheral_map + HEX3_0_OFFSET);
    hex5_4_ptr = (volatile unsigned int *)(peripheral_map + HEX5_4_OFFSET);

    // Registra a função cleanup_resources para ser chamada automaticamente quando o
    // programa terminar. Isso garante que os recursos sejam sempre liberados corretamente.
    atexit(cleanup_resources);
    
    // Retorna 0 para indicar que a inicialização foi bem-sucedida.
    return 0;
}

/**
 * @brief Desenha um único pixel na tela, com checagem de limites.
 */
void set_pix(int x, int y, uint16_t color) {
    if (y >= 0 && y < VISIBLE_HEIGHT && x >= 0 && x < VISIBLE_WIDTH) {
        tela[y][x] = color;
    }
}

/**
 * @brief Desenha um retângulo preenchido.
 */
void draw_filled_rect(int x0, int y0, int x1, int y1, uint16_t color) {
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            set_pix(x, y, color);
        }
    }
}

/**
 * @brief Desenha um círculo preenchido.
 */
void draw_circle(int xc, int yc, int r, uint16_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                set_pix(xc + x, yc + y, color);
            }
        }
    }
}

/**
 * @brief Desenha um único algarismo (0-9) na tela em uma posição específica.
 * A função usa uma matriz de fonte pré-definida (font_3x5) como um "bitmap"
 * e desenha um retângulo escalonado para cada "pixel" que deve ser aceso.
 *
 * @param digit O número (0 a 9) a ser desenhado.
 * @param x A coordenada X do canto onde o dígito será desenhado.
 * @param y A coordenada Y do canto onde o dígito será desenhado.
 * @param color A cor (formato RGB565) a ser usada para desenhar o dígito.
 */
void draw_digit(int digit, int x, int y, uint16_t color) {
    // Cláusula de guarda: se o dígito não estiver no intervalo 0-9, a função retorna
    // imediatamente para evitar acesso a uma posição inválida da matriz font_3x5.
    if (digit < 0 || digit > 9) return;

    // Loop externo: itera sobre cada linha da matriz da fonte (de 0 a FONT_HEIGHT-1).
    for (int row = 0; row < FONT_HEIGHT; row++) {
        // Loop interno: itera sobre cada coluna da matriz da fonte (de 0 a FONT_WIDTH-1).
        for (int col = 0; col < FONT_WIDTH; col++) {
            // Verifica na nossa "matriz-desenho" se o pixel na posição [linha][coluna]
            // para o dígito atual deve ser aceso (valor 1).
            if (font_3x5[digit][row][col] == 1) {
                // Se o pixel deve ser aceso, desenha um retângulo preenchido na tela.
                // As coordenadas e o tamanho do retângulo são multiplicados pela FONT_SCALE
                // para que o dígito apareça maior do que seu tamanho base de 3x5.
                draw_filled_rect(x + (col * FONT_SCALE), y + (row * FONT_SCALE),
                                 x + (col * FONT_SCALE) + FONT_SCALE, y + (row * FONT_SCALE) + FONT_SCALE,
                                 color);
            }
        }
    }
}

/**
 * @brief Desenha um número inteiro (como um placar) na tela, alinhado à direita.
 * A função recebe uma coordenada 'x' que funciona como a borda DIREITA do texto.
 * Ela então desenha cada dígito da direita para a esquerda.
 *
 * @param score O número inteiro a ser desenhado.
 * @param x     A coordenada X da borda DIREITA onde o placar deve terminar.
 * @param y     A coordenada Y do topo do placar.
 * @param color A cor (formato RGB565) a ser usada para o placar.
 */
void draw_score(int score, int x, int y, uint16_t color) {
    // Cria um buffer de caracteres (string) para armazenar a versão em texto do placar.
    char score_text[10];
    
    // Converte o número inteiro 'score' para uma string de caracteres. Ex: 123 -> "123".
    sprintf(score_text, "%d", score);
    
    // Calcula o comprimento da string. Ex: para "123", len será 3.
    int len = strlen(score_text);
    
    // Inicializa uma variável 'cursor' com a posição X final (borda direita).
    int current_x = x;

    // Loop que itera pela string de trás para frente (do último ao primeiro caractere).
    // Isso é o que garante o alinhamento à direita.
    for (int i = len - 1; i >= 0; i--) {
        // Converte o caractere do dígito (ex: '5') para seu valor inteiro (ex: 5).
        // Isso é feito subtraindo o valor ASCII do caractere '0'.
        int digit = score_text[i] - '0';
        
        // Calcula a largura total que um caractere ocupará na tela, incluindo a escala.
        int char_width = (FONT_WIDTH * FONT_SCALE);
        
        // ANTES de desenhar, move o cursor para a esquerda pelo tamanho do caractere.
        current_x -= char_width;
        
        // Chama a função draw_digit para desenhar o algarismo na nova posição do cursor.
        draw_digit(digit, current_x, y, color);
        
        // Move o cursor mais um pouco para a esquerda para criar o espaçamento
        // para o próximo dígito a ser desenhado.
        current_x -= FONT_CHAR_SPACING;
    }
}

/**
 * @brief Atualiza os placares nos displays de 7 segmentos da placa.
 * A função separa cada placar (0-99) em dezenas e unidades, converte cada
 * dígito para o código de 7 segmentos correspondente e os envia para os
 * registradores de hardware corretos.
 *
 * @param score1 Placar do Jogador 1.
 * @param score2 Placar do Jogador 2.
 */
void update_hex_displays(int score1, int score2) {
    // Garante que o placar não ultrapasse 99, o máximo que pode ser exibido em 2 displays.
    if (score1 > 99) score1 = 99;
    if (score2 > 99) score2 = 99;

    // --- Lógica para o Placar do Jogador 1 (HEX1 e HEX0) ---

    // Isola o dígito da dezena (ex: 57 / 10 = 5).
    int p1_dezena = score1 / 10;
    // Isola o dígito da unidade (ex: 57 % 10 = 7).
    int p1_unidade = score1 % 10;

    // Busca na tabela de consulta o código de 7 segmentos para a dezena.
    unsigned char p1_code_d = seven_seg_digits[p1_dezena];
    // Busca na tabela de consulta o código de 7 segmentos para a unidade.
    unsigned char p1_code_u = seven_seg_digits[p1_unidade];
    
    // Combina os dois códigos em um único inteiro e escreve no registrador de hardware.
    // O registrador HEX3-0 controla 4 displays. Os 8 bits mais baixos controlam o HEX0,
    // os 8 bits seguintes controlam o HEX1, e assim por diante.
    // (p1_code_d << 8) -> Desloca o código da dezena 8 bits para a esquerda para alinhá-lo com o HEX1.
    // | p1_code_u -> Combina (com OU bit-a-bit) o resultado com o código da unidade, que fica no HEX0.
    *hex3_0_ptr = (p1_code_d << 8) | p1_code_u;

    // --- Lógica para o Placar do Jogador 2 (HEX5 e HEX4) ---

    // Repete o mesmo processo para o segundo jogador.
    int p2_dezena = score2 / 10;
    int p2_unidade = score2 % 10;
    unsigned char p2_code_d = seven_seg_digits[p2_dezena];
    unsigned char p2_code_u = seven_seg_digits[p2_unidade];
    
    // Escreve nos registradores dos displays HEX5 e HEX4.
    // O código da dezena (P2) vai para o HEX5 e o da unidade (P2) para o HEX4.
    *hex5_4_ptr = (p2_code_d << 8) | p2_code_u;
}

/**
 * @brief Preenche toda a área visível da tela com uma única cor.
 */
void fill_screen(uint16_t color) {
    for (int y = 0; y < VISIBLE_HEIGHT; y++) {
        for (int x = 0; x < VISIBLE_WIDTH; x++) {
            tela[y][x] = color;
        }
    }
}

/**
 * @brief Desenha a forma completa do pássaro na tela, combinando várias formas geométricas.
 * As partes do pássaro (olho, bico) são desenhadas em posições relativas ao seu
 * centro e raio, permitindo que o desenho se ajuste a diferentes tamanhos.
 *
 * @param x           A coordenada X do centro do corpo do pássaro.
 * @param y           A coordenada Y do centro do corpo do pássaro.
 * @param body_color  A cor principal do corpo do pássaro (amarelo para P1, vermelho para P2).
 * @param bird_radius O raio atual do pássaro, vindo dos switches de dificuldade.
 */
void draw_flappy_bird(int x, int y, uint16_t body_color, int bird_radius) {
    // 1. Desenha o corpo principal do pássaro como um círculo preenchido.
    draw_circle(x, y, bird_radius, body_color);
    
    // 2. Desenha a parte branca do olho, posicionada à frente e acima do centro.
    // O tamanho e a posição do olho são proporcionais ao raio do pássaro.
    draw_circle(x + bird_radius / 2, y - bird_radius / 3, bird_radius / 4, WHITE);
    
    // 3. Desenha a pupila como um único pixel preto no centro da parte branca do olho.
    set_pix(x + bird_radius / 2, y - bird_radius / 3, BLACK);
    
    // 4. Desenha o bico como um pequeno retângulo na frente do pássaro.
    draw_filled_rect(x + bird_radius, y - 2, x + bird_radius + 5, y + 2, BEAK_COLOR);
    
    // 5. Desenha uma pequena asa branca na parte de trás do corpo do pássaro.
    draw_filled_rect(x - bird_radius / 2, y, x, y + 5, WHITE);
}

// =================================================================================
// --- SEÇÃO 7: LÓGICA DO JOGO ---
// =================================================================================

/**
 * @brief Verifica se um pássaro colidiu com os limites da tela ou com um obstáculo.
 * A função realiza uma checagem de colisão geométrica baseada em retângulos de contorno (Bounding Box).
 * * @param bird Ponteiro para a estrutura do pássaro.
 * @param bird_x_pos Posição X fixa do pássaro.
 * @param obs Ponteiro para a estrutura do obstáculo a ser verificado.
 * @param bird_radius O raio atual do pássaro (dificuldade).
 * @param gap_height A altura atual do buraco (dificuldade).
 * @return 1 se houver colisão, 0 caso contrário.
 */
int check_collision(const Bird* bird, int bird_x_pos, const Obstacle* obs, int bird_radius, int gap_height) {
    // 1. VERIFICAÇÃO DE COLISÃO COM OS LIMITES DA TELA (TETO E CHÃO)
    // Se a borda de cima do pássaro (y - raio) for menor que 0 (acima do teto) OU
    // se a borda de baixo (y + raio) for maior que a altura da tela, é uma colisão.
    if ((bird->y - bird_radius) < 0 || (bird->y + bird_radius) > VISIBLE_HEIGHT) {
        return 1; // Retorna 1 (verdadeiro) para indicar colisão.
    }

    // 2. VERIFICAÇÃO DE COLISÃO COM OS CANOS
    // Esta checagem é feita em duas partes para otimização (Axis-Aligned Bounding Box).

    // 2.1. Primeiro, verifica se há sobreposição no eixo X.
    // Se a borda direita do pássaro está à frente do início do cano E
    // se a borda esquerda do pássaro está antes do fim do cano, então eles se sobrepõem horizontalmente.
    if (bird_x_pos + bird_radius > obs->x && bird_x_pos - bird_radius < obs->x + OBSTACLE_WIDTH) {
        
        // 2.2. Se (e somente se) houver sobreposição em X, verifica a colisão no eixo Y.
        // O pássaro colide se NÃO estiver passando pelo buraco.
        // Se a borda de cima do pássaro está acima do início do buraco (colidiu com o cano de cima) OU
        // se a borda de baixo do pássaro está abaixo do fim do buraco (colidiu com o cano de baixo).
        if (bird->y - bird_radius < obs->gap_y || bird->y + bird_radius > obs->gap_y + gap_height) {
            return 1; // Retorna 1 (verdadeiro) para indicar colisão.
        }
    }

    // 3. SE NENHUMA CONDIÇÃO DE COLISÃO FOI ATENDIDA
    // Se o código chegou até aqui, significa que o pássaro está seguro em relação a este obstáculo.
    return 0; // Retorna 0 (falso) para indicar que não houve colisão.
}

/**
 * @brief Reinicia o estado do jogo para os valores iniciais de uma nova partida.
 * Configura a posição dos jogadores, zera os placares e posiciona os obstáculos
 * iniciais fora da tela, com base nos parâmetros de dificuldade atuais.
 *
 * @param p1 Ponteiro para a estrutura do Jogador 1.
 * @param p2 Ponteiro para a estrutura do Jogador 2.
 * @param obstacles Array de estruturas de obstáculos.
 * @param num_obstacles Quantos obstáculos devem ser inicializados (2 ou 3), conforme o SW4.
 * @param score1 Ponteiro para a variável de placar do Jogador 1.
 * @param score2 Ponteiro para a variável de placar do Jogador 2.
 * @param is_two_player_mode Flag que indica se o P2 deve ser ativado (do SW8).
 * @param spacing Espaçamento horizontal entre os canos (do SW4).
 * @param gap_height Abertura vertical dos canos (dos SW2 e SW3).
 */
void reset_game(Bird* p1, Bird* p2, Obstacle obstacles[], int num_obstacles, int* score1, int* score2, int is_two_player_mode, int spacing, int gap_height) {
    // ---- Configuração do Jogador 1 ----
    // Posiciona o pássaro no meio da tela verticalmente.
    p1->y = VISIBLE_HEIGHT / 2.0;
    // Zera a velocidade vertical para que ele comece parado.
    p1->velocity_y = 0;
    // Define o estado do pássaro como vivo.
    p1->alive = 1;

    // ---- Reset dos Placares ----
    // Usa ponteiros para modificar as variáveis de placar na função 'main'.
    *score1 = 0;
    *score2 = 0;

    // ---- Configuração do Jogador 2 (condicional) ----
    // Verifica se o modo de 2 jogadores está ativo (SW8 para cima).
    if (is_two_player_mode) {
        // Se sim, configura o Jogador 2 da mesma forma que o Jogador 1.
        p2->y = VISIBLE_HEIGHT / 2.0;
        p2->velocity_y = 0;
        p2->alive = 1;
    } else {
        // Se não, o Jogador 2 é desativado ao ser marcado como "não vivo".
        // Isso impede que ele seja desenhado, movido ou verificado para colisões.
        p2->alive = 0; 
    }

    // ---- Posicionamento Inicial dos Obstáculos ----
    // Este loop posiciona os obstáculos iniciais fora da tela, à direita.
    for (int i = 0; i < num_obstacles; i++) {
        // Calcula a posição X de cada cano para que fiquem enfileirados e espaçados corretamente.
        obstacles[i].x = VISIBLE_WIDTH + 150 + i * spacing;
        // Define uma altura aleatória para a abertura do cano, garantindo que não fique
        // muito perto do teto ou do chão (margem de 30 pixels em cima e 30 em baixo).
        obstacles[i].gap_y = rand() % (VISIBLE_HEIGHT - gap_height - 60) + 30;
        // Garante que o cano não comece com a pontuação já marcada.
        obstacles[i].scored = 0;
    }
    
    // Medida de segurança: se estamos no modo fácil (2 canos), garante que o terceiro
    // obstáculo do array seja movido para fora da tela para não aparecer por engano
    // devido a dados antigos na memória.
    if (num_obstacles < 3) {
        obstacles[2].x = -OBSTACLE_WIDTH - 10;
    }
    
    // ---- Mensagens no Console ----
    // Imprime as instruções iniciais para o(s) jogador(es).
    printf("Iniciando Jogo! P1 (Amarelo) usa KEY1. ");
    if(is_two_player_mode) printf("P2 (Vermelho) usa KEY2. ");
    printf("KEY0 para Sair.\n");
    // Força a escrita imediata do buffer do printf para o console.
    fflush(stdout);
}

// =================================================================================
// --- SEÇÃO 8: FUNÇÃO PRINCIPAL ---
// O coração do programa, onde o loop do jogo acontece.
// =================================================================================
/**
 * @brief Função principal e ponto de entrada do programa.
 * Orquestra a inicialização do hardware, o loop principal do jogo, a máquina de estados
 * (GAME_RUNNING, GAME_OVER), a leitura de inputs, a atualização da lógica de jogo,
 * o desenho na tela e a liberação de recursos ao final.
 */
int main() {
    // ========================================================================
    // --- 1. Inicialização do Hardware e Buffers ---
    // ========================================================================

    // Chama a função para abrir /dev/mem e mapear todos os periféricos de hardware.
    // Se falhar, encerra o programa com um código de erro.
    if (init_hardware() != 0) { return 1; }

    // Aloca memória para o "back buffer", nosso buffer de desenho secundário.
    // Isso é essencial para a técnica de double buffering, que evita o flicker na tela.
    uint16_t* back_buffer = malloc(LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE);
    if (!back_buffer) {
        perror("Erro ao alocar o back buffer");
        return 1;
    }

    // Inicializa o gerador de números aleatórios usando o tempo atual como semente.
    // Isso garante que a posição dos canos seja diferente a cada vez que o jogo é executado.
    srand(time(NULL));

    // ========================================================================
    // --- 2. Declaração das variáveis de estado do jogo ---
    // ========================================================================

    // Array para os obstáculos.
    Obstacle obstacles[3]; 
    // Estruturas para os dois pássaros.
    Bird player1, player2;
    // Variável que controla o estado atual da máquina de estados principal.
    GameState state = GAME_RUNNING;
    // Guarda o estado anterior dos botões para detectar a borda de subida (pressionar).
    unsigned int prev_key_state = 0x0;
    
    // Variáveis para os placares da partida atual e os recordes.
    int score_p1, score_p2;
    int high_score_p1 = 0, high_score_p2 = 0;
    
    // ========================================================================
    // --- 3. Reset inicial do jogo ---
    // ========================================================================

// ------------------------------------------------------------------------
    // Lê o estado inicial do switch de modo de jogo (SW8) para a primeira partida.
    // Esta é uma leitura única, feita antes do loop principal, para garantir que o jogo
    // comece no modo correto (1P ou 2P) desde a primeira tela.
    // O ponteiro é lido e a operação bitwise `& 0x100` isola o estado do bit 8.
    // ------------------------------------------------------------------------
    int is_two_player_mode = (*(volatile unsigned int *)(peripheral_map + SWITCHES_OFFSET)) & 0x100;
    
    // ------------------------------------------------------------------------
    // Chama a função reset_game para configurar o estado inicial do jogo.
    // Note que são usados parâmetros de dificuldade padrão (NUM_PIPES_EASY, SPACING_EASY, etc.).
    // Isso ocorre porque a lógica completa de leitura de todos os switches de dificuldade
    // está dentro do loop principal. O jogo sempre começa com uma dificuldade base
    // e se ajusta no primeiro quadro executado.
    // ------------------------------------------------------------------------
    reset_game(&player1, &player2, obstacles, NUM_PIPES_EASY, &score_p1, &score_p2, is_two_player_mode, SPACING_EASY, GAP_EASY);
    
    // ------------------------------------------------------------------------
    // Atualiza os displays de 7 segmentos com os recordes iniciais.
    // Como as variáveis `high_score_p1` e `high_score_p2` foram inicializadas com 0,
    // esta chamada irá mostrar "00" para cada placar de recorde antes do jogo começar.
    // ------------------------------------------------------------------------
    update_hex_displays(high_score_p1, high_score_p2);

    // ========================================================================
    // --- 4. Loop Principal do Jogo (executa a cada quadro) ---
    // ========================================================================
    while (1) {
        // 4.1. Leitura do estado atual dos periféricos
        unsigned int current_key_state = *key_ptr; // Lê os 4 botões KEY.
        unsigned int switch_state = *sw_ptr;      // Lê os 10 switches SW.
        
        // 4.2. Lógica de Dificuldade dos Switches: traduz o estado dos switches em parâmetros de jogo
        is_two_player_mode = switch_state & 0x100; // Usa bitwise AND para checar o bit 8 (SW8).
        int is_paused = switch_state & 0x200;      // Checa o bit 9 (SW9).

        // Lógica para o número de canos e espaçamento (SW4)
        int num_obstacles;
        int current_spacing;
        if (switch_state & (1 << 4)) { // Checa se o bit 4 está ligado.
            num_obstacles = NUM_PIPES_HARD;
            current_spacing = SPACING_HARD;
        } else {
            num_obstacles = NUM_PIPES_EASY;
            current_spacing = SPACING_EASY;
        }
        
        // Lógica para a velocidade (SW1 e SW0)
        int current_speed;
        switch (switch_state & 0b11) { // Isola os 2 bits mais baixos (SW1 e SW0).
            case 0b00: current_speed = SPEED_LEVEL_0; break;
            case 0b01: current_speed = SPEED_LEVEL_1; break;
            case 0b10: current_speed = SPEED_LEVEL_2; break;
            case 0b11: current_speed = SPEED_LEVEL_3; break;
        }

        // Lógica para a abertura dos canos (SW3 e SW2)
        int current_gap_height;
        switch ((switch_state >> 2) & 0b11) { // Desloca 2 bits para a direita e isola os 2 bits resultantes.
            case 0b00: current_gap_height = GAP_EASIEST; break;
            case 0b01: current_gap_height = GAP_EASY;    break;
            case 0b10: current_gap_height = GAP_HARD;    break;
            case 0b11: current_gap_height = GAP_HARDEST; break;
        }

        // Lógica para os parâmetros restantes (operador ternário para uma checagem simples)
        double current_gravity = (switch_state & (1 << 5)) ? GRAVITY_HARD : GRAVITY_EASY;       // SW5
        double current_jump_velocity = (switch_state & (1 << 6)) ? JUMP_HARD : JUMP_EASY; // SW6
        int current_bird_radius = (switch_state & (1 << 7)) ? RADIUS_HARD : RADIUS_EASY;   // SW7

        // Condição de saída do jogo (pressionar KEY0)
        if (current_key_state & 0b0001) { break; } 

        // 4.3. Máquina de Estados Principal
        switch(state) {
            case GAME_RUNNING: {
                // 4.3.1. ATUALIZAÇÃO DA LÓGICA DO JOGO (SE NÃO ESTIVER PAUSADO)
                if (!is_paused) {
                    // Processa input dos jogadores (detecta a transição de não pressionado para pressionado)
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

                    // Aplica física (gravidade e movimento) a cada pássaro vivo
                    if(player1.alive) {
                        player1.velocity_y += current_gravity;
                        player1.y += player1.velocity_y;
                    }
                    if(player2.alive) {
                        player2.velocity_y += current_gravity;
                        player2.y += player2.velocity_y;
                    }

                    // Move e recicla os obstáculos
                    for (int i = 0; i < num_obstacles; i++) {
                        // --- 1. Movimento do Obstáculo ---
                        // Subtrai a velocidade atual da posição X do obstáculo.
                        obstacles[i].x -= current_speed;
                        
                        // --- 2. Lógica de Pontuação ---
                        // Esta condição verifica se um ponto deve ser marcado. Ela precisa de duas
                        // condições para ser verdadeira, evitando pontuação múltipla.
                        // `!obstacles[i].scored`: Garante que ainda não pontuamos neste cano.
                        // `obstacles[i].x + OBSTACLE_WIDTH < P1_X_POS`: Verifica se a borda DIREITA
                        // do obstáculo já passou pela posição X do jogador.
                        if (!obstacles[i].scored && obstacles[i].x + OBSTACLE_WIDTH < P1_X_POS) {
                            // Se as condições forem atendidas, marca o obstáculo como "pontuado"
                            // para que este bloco não seja executado novamente para o mesmo cano.
                            obstacles[i].scored = 1;
                            
                            // Incrementa o placar para cada jogador que ainda estiver vivo.
                            if (player1.alive) score_p1++;
                            if (player2.alive) score_p2++;
                        }
                        
                        // --- 3. Lógica de Reciclagem de Obstáculos ---
                        // Esta condição verifica se um obstáculo já saiu completamente da tela.
                        // `obstacles[i].x + OBSTACLE_WIDTH < 0`: Verifica se a borda DIREITA do
                        // obstáculo está à esquerda da borda da tela (posição 0).
                        if (obstacles[i].x + OBSTACLE_WIDTH < 0) {
                            // Para garantir um fluxo contínuo de canos, precisamos encontrar
                            // qual dos obstáculos atuais está mais à direita na tela.
                            int max_x = 0;
                            for (int j = 0; j < num_obstacles; j++) {
                                if (obstacles[j].x > max_x) {
                                    max_x = obstacles[j].x;
                                }
                            }
                            
                            // Reposiciona o obstáculo que saiu da tela (`obstacles[i]`) à direita
                            // do obstáculo mais avançado (`max_x`), adicionando o espaçamento correto.
                            // Isso cria uma esteira infinita de canos e funciona perfeitamente ao
                            // alternar entre 2 e 3 obstáculos.
                            obstacles[i].x = max_x + current_spacing;
                            
                            // Gera uma nova altura aleatória para a abertura do cano,
                            // tornando o próximo desafio imprevisível.
                            obstacles[i].gap_y = rand() % (VISIBLE_HEIGHT - current_gap_height - 60) + 30;
                            
                            // Reseta a flag de pontuação para que seja possível pontuar neste
                            // obstáculo novamente na sua próxima aparição.
                            obstacles[i].scored = 0;
                        }
                    }

                    // Verifica colisões para cada pássaro com cada obstáculo
                    for (int i = 0; i < num_obstacles; i++) {
                        if (player1.alive && check_collision(&player1, P1_X_POS, &obstacles[i], current_bird_radius, current_gap_height)) {
                            player1.alive = 0;
                        }
                        if (player2.alive && check_collision(&player2, P2_X_POS, &obstacles[i], current_bird_radius, current_gap_height)) {
                            player2.alive = 0;
                        }
                    }

                    // Verifica condição de fim de jogo
                    int game_is_over = 0;
                    if(is_two_player_mode) { if(!player1.alive && !player2.alive) game_is_over = 1; } 
                    else { if(!player1.alive) game_is_over = 1; }

                    if(game_is_over) {
                        state = GAME_OVER;
                        if (score_p1 > high_score_p1) high_score_p1 = score_p1;
                        if (score_p2 > high_score_p2) high_score_p2 = score_p2;
                    }
                }

                // --- 4.3.2. ROTINA DE DESENHO (SEMPRE EXECUTA, MESMO EM PAUSA) ---
                // Esta seção é responsável por desenhar tudo na tela. Ela implementa a técnica
                // de "Double Buffering" para criar uma animação suave.

                // --- ETAPA 1: Redirecionar o Desenho para o Buffer Escondido ---
                // Salva o ponteiro original que aponta para a memória de vídeo real.
                volatile uint16_t (*original_tela_ptr)[LWIDTH] = tela;
                // Redireciona o ponteiro global 'tela' para que ele aponte para o
                // buffer secundário ('back_buffer'). A partir daqui, todas as funções de
                // desenho (draw_rect, draw_circle, etc.) irão, sem saber, desenhar
                // em uma "tela falsa" na memória RAM, e não na tela visível.
                tela = (volatile uint16_t (*)[LWIDTH])back_buffer;

                // --- ETAPA 2: Desenhar a Cena Completa no Back Buffer ---
                // Limpa o back buffer, preenchendo-o com a cor de fundo. Isso garante
                // que não haja "fantasmas" do quadro anterior.
                fill_screen(SKY_BLUE);
                
                // Desenha cada um dos obstáculos ativos no back buffer.
                for (int i = 0; i < num_obstacles; i++) {
                    draw_filled_rect(obstacles[i].x, 0, obstacles[i].x + OBSTACLE_WIDTH, obstacles[i].gap_y, GREEN);
                    draw_filled_rect(obstacles[i].x, obstacles[i].gap_y + current_gap_height, obstacles[i].x + OBSTACLE_WIDTH, VISIBLE_HEIGHT, GREEN);
                }
                
                // Desenha os pássaros (se estiverem vivos) no back buffer.
                if (player1.alive) draw_flappy_bird(P1_X_POS, (int)player1.y, P1_COLOR, current_bird_radius);
                if (player2.alive) draw_flappy_bird(P2_X_POS, (int)player2.y, P2_COLOR, current_bird_radius);
                
                // Se o jogo estiver pausado, desenha o ícone de pausa sobre a cena.
                if(is_paused) {
                    draw_filled_rect(145, 100, 155, 140, WHITE);
                    draw_filled_rect(165, 100, 175, 140, WHITE);
                }
                
                // Desenha o placar no canto superior direito do back buffer.
                draw_score(score_p1 + score_p2, VISIBLE_WIDTH - 10, 10, WHITE);
                
                // --- ETAPA 3: Copiar o Quadro Pronto para a Tela Visível ---
                // Restaura o ponteiro 'tela' para seu valor original, apontando novamente para a memória de vídeo real.
                tela = original_tela_ptr;
                
                // Esta é a etapa final e mais importante do double buffering.
                // Copia o conteúdo inteiro do nosso back_buffer (onde a cena foi montada
                // perfeitamente) para a memória de vídeo real de uma só vez.
                // A função memcpy é extremamente rápida, fazendo com que a atualização da tela
                // pareça instantânea para o olho humano, eliminando o flicker.
                memcpy((void*)tela, back_buffer, LWIDTH * VISIBLE_HEIGHT * PIXEL_SIZE);
                
                // Atualiza os displays de 7 segmentos com o placar de recorde.
                update_hex_displays(high_score_p1, high_score_p2);
                
                // Sai do estado GAME_RUNNING neste ciclo do switch-case.
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
        usleep(16666); // Limita o loop a ~60 quadros por segundo
    }
    
    // 5. Liberação de recursos antes de sair
    free(back_buffer); // Libera a memória alocada para o back buffer.
    return 0;
}