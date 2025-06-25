# Tarefa: Roteiro de FreeRTOS #2 - EmbarcaTech 2025
Autor: **Andre Melo e Pedro Sampaio**
Curso: Residência Tecnológica em Sistemas Embarcados
Instituição: EmbarcaTech - HBr
Brasilia, junho de 2025
---

# Self-Randomizing Keypad com FreeRTOS

Sistema de fechadura eletrônica com randomização automática de dígitos implementado com FreeRTOS para prevenir a visualização da senha por terceiros. Desenvolvido com a BitDogLab (Raspberry Pi Pico W) como segundo projeto de FreeRTOS da residencia do EmbarcaTech.

![Demonstração do Self-Randomizing Keypad](./demo.gif)

## Funcionalidades

- **Multitarefa com FreeRTOS**: Sistema organizado em tasks independentes para melhor gerenciamento de recursos
- Posições dos dígitos se randomizam automaticamente para prevenir bisbilhotagem
- Display com 4 linhas e 4 dígitos por linha
- Navegação intuitiva utilizando joystick
- Feedback visual através de display OLED e LEDs indicadores
- Feedback sonoro com melodias diferentes para sucesso/falha
- Interface I2C para display OLED
- Entrada segura de senha através da seleção de linhas ofuscadas ao invés de dígitos diretos
- **Sincronização entre tasks** usando semáforos e filas do FreeRTOS

## Arquitetura FreeRTOS

O sistema utiliza as seguintes tasks do FreeRTOS:

- **Display Task**: Gerencia a atualização do display OLED e a matriz de dígitos
- **Input Task**: Processa entradas do joystick e botão
- **LED Task**: Controla os LEDs indicadores
- **Audio Task**: Gerencia o feedback sonoro através do buzzer
- **Main Task**: Coordena a lógica principal do sistema de senha

### Mecanismos de Sincronização

- **Semáforos**: Para controle de acesso aos recursos compartilhados
- **Filas**: Para comunicação entre tasks
- **Timers**: Para controle de timeouts e delays não-bloqueantes

## Requisitos de Hardware

- Raspberry Pi Pico W
- Display OLED SSD1306 (128x64 pixels, I2C)
- Joystick Analógico
- 2x LEDs (Verde e Vermelho) (ou um LED RGB)
- Botão
- Buzzer Piezoelétrico
- Placa de Desenvolvimento BitDogLab (Opcional, contém todos acima)

## Configuração de Pinos

| Função | Pino | Tipo | Descrição |
|--------|------|------|-----------|
| Display SDA | GPIO 14 | I2C | Linha de dados do display OLED |
| Display SCL | GPIO 15 | I2C | Linha de clock do display OLED |
| Joystick X | GPIO 26 | ADC | Entrada analógica eixo X |
| Joystick Y | GPIO 27 | ADC | Entrada analógica eixo Y (não utilizado) |
| Botão | GPIO 6 | Entrada | Botão de seleção com interrupção |
| LED Verde | GPIO 11 | PWM | Indicador de sucesso |
| LED Vermelho | GPIO 13 | PWM | Indicador de falha |
| Buzzer | GPIO 21 | PWM | Feedback sonoro |

## Compilação e Instalação

*Se você quiser apenas rodar o projeto na BitDogLab (ou na Pico W com as conexões indicadas) basta transferir o arquivo `.uf2` na pasta build para a placa no modo bootloader*

### Pré-requisitos
- SDK do Raspberry Pi Pico
- FreeRTOS configurado para RP2040
- CMake
- Toolchain ARM GCC

### Passos para Compilação

1. Clone o repositório:
```bash
git clone https://github.com/EmbarcaTech-2025/tarefa-freertos-2-pedro_e_andre.git
cd tarefa-freertos-2-pedro_e_andre
```

2. Configure o ambiente:
```bash
mkdir build
cd build
cmake ..
```

3. Compile o projeto:
```bash
make -j4
```

4. Conecte seu Raspberry Pi Pico W em modo bootloader e copie o arquivo `.uf2` gerado para ele.

## Como Usar

1. O sistema exibe 4 linhas com 4 dígitos de 0 a F aleatórios em cada
2. Use o joystick para mover para cima/baixo entre as linhas
3. Pressione o botão B para selecionar a linha que contém o dígito desejado da sua senha
4. Um asterisco (*) aparece para cada dígito inserido
5. Após inserir todos os 6 dígitos:
   - Senha correta: LED Verde + melodia de sucesso
   - Senha incorreta: LED Vermelho + melodia de falha
6. O sistema reinicia automaticamente e randomiza os dígitos para a próxima tentativa

## Estrutura do Projeto

```
tarefa-freertos-2-pedro_e_andre/
├── src/
│   ├── main.c
│   ├── tasks/
│   │   ├── display_task.c
│   │   ├── input_task.c
│   │   ├── led_task.c
│   │   └── audio_task.c
│   └── drivers/
│       └── ssd1306/
├── include/
│   ├── tasks/
│   └── drivers/
├── FreeRTOS/
├── validation/
│   ├── matrix_generator.c
│   └── matrix_validator.c
├── CMakeLists.txt
└── README.md
```

## Recursos de Segurança

- Dígitos são randomizados em todos os usos e sempre que um digito é selecionado
- Múltiplos dígitos por linha previnem observação direta
- Seleção por linha ao invés de dígito adiciona uma camada extra de ofuscação
- Dois números duplicados por matriz aumentam a dificuldade de adivinhação
- Nenhum dígito se repete na mesma linha
- **Timeouts implementados via FreeRTOS** para resetar o sistema automaticamente

## Configuração do FreeRTOS

O projeto utiliza as seguintes configurações principais do FreeRTOS:

- **Scheduler**: Preemptivo
- **Tick Rate**: 1000 Hz
- **Heap Size**: 32KB
- **Stack Size por Task**: 256-512 words (dependendo da task)
- **Priority Levels**: 0-3 (0 = idle, 3 = máxima prioridade)

## Links

- [Repositório do Projeto](https://github.com/EmbarcaTech-2025/tarefa-freertos-2-pedro_e_andre)
- [Vídeo de Demonstração](https://youtu.be/1ibz22kYk1E)
- [Documentação FreeRTOS](https://www.freertos.org/Documentation/RTOS_book.html)

---
## 📜 Licença
GNU GPL-3.0.
