
# Flappy Bird para 2 Jogadores na Placa DE1-SoC

## 📌 Visão Geral do Projeto

Este projeto é uma implementação completa e customizável do jogo **Flappy Bird**, desenvolvida em linguagem **C** para rodar no **Linux embarcado** da placa **Terasic DE1-SoC**. O jogo interage diretamente com os periféricos mapeados em memória, incluindo saída VGA, botões (KEYs), switches e displays de 7 segmentos (HEX).

A proposta é guiar um ou dois pássaros por uma série infinita de canos, acumulando pontos. O jogo permite a **configuração dinâmica da dificuldade e do modo de jogo** por meio dos switches da placa, tornando a experiência altamente interativa.

---

## 🎮 Funcionalidades Implementadas

- **Modo de 1 ou 2 Jogadores**: alternável pelo switch **SW8**.
- **Renderização suave (Double Buffering)**: sem cintilação ou tearing.
- **Sistema de Pausa**: ativado/desativado via **SW9**.
- **Dificuldade Configurável em Tempo Real**:
  - Velocidade do jogo (**SW0, SW1**)
  - Abertura dos canos (**SW2, SW3**)
  - Número de obstáculos (**SW4**)
  - Gravidade (**SW5**)
  - Força do pulo (**SW6**)
  - Tamanho do pássaro (**SW7**)
- **Pontuação**:
  - Placar da rodada atual na tela (VGA)
  - Recordes persistentes nos displays HEX

---

## 🧰 Requisitos

- **Hardware**: Placa **Terasic DE1-SoC**
- **Software**:
  - Linux embarcado (DE1-SoC Computer)
  - Compilador `gcc` com suporte à biblioteca matemática (`-lm`)

---

## ⚙️ Como Compilar e Executar

1. Salve o código-fonte como `flappy_game.c`.
2. Transfira para a placa (via SSH, cartão SD etc).
3. Compile no terminal da DE1-SoC:

```bash
gcc -std=c99 flappy_game.c -o flappy_game -lm
```

4. Execute com privilégios de superusuário:

```bash
sudo ./flappy_game
```

---

## 🕹️ Jogabilidade e Controles

### 🎯 Objetivo

Evite colisões com canos ou com as bordas da tela. A partida termina para um jogador ao colidir.

### 🔘 Botões (KEYs)

- **KEY1**: Pulo do Jogador 1 (amarelo)
- **KEY2**: Pulo do Jogador 2 (vermelho)
- **KEY0**: Encerra o jogo imediatamente

### 🎚️ Switches (SW0–SW9)

A convenção é: **baixo = fácil / cima = difícil**

| Switch | Função | Descrição |
|--------|--------|-----------|
| SW0/SW1 | Velocidade | 00 = Lento, 01 = Normal, 10 = Rápido, 11 = Muito rápido |
| SW2/SW3 | Abertura dos canos | 00 = Máxima, 01 = Grande, 10 = Pequena, 11 = Mínima |
| SW4 | Obstáculos | 0 = 2 canos, 1 = 3 canos com menor espaçamento |
| SW5 | Gravidade | 0 = Fraca, 1 = Forte |
| SW6 | Força do pulo | 0 = Forte, 1 = Fraco |
| SW7 | Tamanho do pássaro | 0 = Pequeno, 1 = Grande |
| SW8 | Modo de jogo | 0 = 1 Jogador, 1 = 2 Jogadores |
| SW9 | Pausar jogo | 0 = Executando, 1 = Pausado |

---

## 🧮 Sistema de Pontuação

- **Tela VGA**: Mostra o placar da rodada atual.
- **Displays HEX**:
  - **HEX 1-0**: Recorde Jogador 1
  - **HEX 5-4**: Recorde Jogador 2
  - Atualização automática ao final de cada partida.

---

## 🔄 Fim de Jogo e Reinício

Quando todos os jogadores colidem, a partida termina. Pressione **KEY1** ou **KEY2** para reiniciar com as configurações atuais.

---

## 🖥️ Lógica de Renderização VGA: Double Buffering

Durante o desenvolvimento, foi identificado um **problema de cintilação** causado pelo redesenho direto no framebuffer. Para corrigir, adotamos **Double Buffering**:

1. **Renderização** ocorre em um back buffer oculto.
2. Após completar o quadro, o conteúdo é **copiado com `memcpy`** para o framebuffer.
3. Isso garante uma animação **fluida e sem flickering**.

---

## 👤 Autor

- **Nome**: Gabriel da Conceição Miranda 
- **Curso**: Engenharia Mecatrônica 
- **Universidade**: Universidade de Brasília  
- **Data**: Julho de 2025
