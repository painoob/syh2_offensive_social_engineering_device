<div align="center">

# OSEP-32 (SYH2)
### Offensive Social Engineering Platform

![ESP32](https://img.shields.io/badge/ESP32-2432S028R-red?style=for-the-badge&logo=espressif)
![Arduino](https://img.shields.io/badge/Arduino_IDE-1.8.x-blue?style=for-the-badge&logo=arduino)
![Version](https://img.shields.io/badge/Versão-2.0.0-orange?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)
![SYH2](https://img.shields.io/badge/Certificação-SYH2_Solyd-black?style=for-the-badge)

**Plataforma de engenharia social ofensiva baseada em ESP32 com display TFT 320×240, slideshow de campanhas visuais, evil portal com captura de credenciais, controle remoto via Wi-Fi e servidor FTP integrado.**

</div>

---

## 📋 Índice

- [Proposta do Projeto](#-proposta-do-projeto)
- [Funcionalidades](#-funcionalidades)
- [Hardware](#-hardware)
- [Pinagem Interna (ESP32-2432S028R)](#-pinagem-interna-esp32-2432s028r)
- [Instalação do Firmware](#-instalação-do-firmware)
- [Configuração da TFT_eSPI](#-configuração-da-tft_espi)
- [Bibliotecas Necessárias](#-bibliotecas-necessárias)
- [Arquitetura do Firmware](#-arquitetura-do-firmware)
- [API REST](#-api-rest)
- [Guia de Operação](#-guia-de-operação)
- [Evil Portal](#-evil-portal)
- [Script osep_sender.py](#-script-osep_senderpy)
- [Credenciais Padrão](#-credenciais-padrão)
- [Aviso Legal](#-aviso-legal)

---

## 🎯 Proposta do Projeto

O **OSEP-32** é uma ferramenta de engenharia social ofensiva desenvolvida para profissionais de segurança e pentesters que atuam em exercícios de **Red Team** e avaliações de segurança física.

O dispositivo exibe sequências de imagens JPEG (campanhas de phishing visuais) em um display TFT compacto, sendo controlado remotamente via Wi-Fi. Na versão 2.0, incorpora um **evil portal com captive portal e captura de credenciais** que opera em paralelo ao slideshow — o display continua exibindo as imagens normalmente enquanto qualquer dispositivo conectado ao AP é redirecionado para a página de phishing, mantendo a **furtividade total** do dispositivo em campo.

Opera de forma **totalmente autônoma**, sem dependência de rede externa, criando seu próprio ponto de acesso Wi-Fi com DNS spoof integrado.

### Casos de Uso

- 🏧 Simulação de terminais de pagamento / ATMs com telas de phishing
- 🏢 Kiosks falsos em eventos corporativos exibindo QR codes maliciosos
- 📋 Displays informativos em lobbies capturando credenciais via evil portal
- 🎓 Treinamentos de conscientização interna (Blue Team awareness)
- 🔍 Avaliações de segurança física com vetor visual + coleta de credenciais

---

## 📹 Demonstração

<img width="640" height="480" alt="poc_osep32+(2)+(1) (1)" src="https://github.com/user-attachments/assets/86b546fc-dc11-424d-a90b-2fd84e3152f6" />

Link completo de demonstração: [https://youtu.be/jyH-7tuEc1Q](https://youtu.be/jyH-7tuEc1Q)

---

## ⚡ Funcionalidades

| Funcionalidade | Descrição |
|---|---|
| **Slideshow automático** | Exibe imagens JPEG em sequência com delay configurável (1–30 s) |
| **Pausa de slideshow** | Toggle remoto para fixar uma imagem na tela |
| **Exibição forçada** | Exibe imediatamente uma imagem específica pelo painel web |
| **Servidor FTP** | Recebe imagens e arquivos via FTP na porta 21 |
| **Reload automático** | Recarrega a lista de imagens após transferência FTP |
| **Painel web (4 abas)** | Slideshow, Evil Portal, Credenciais e Configurações |
| **Autenticação** | Login por senha com token em todas as requisições de API |
| **Galeria web** | Visualização de thumbnails das imagens armazenadas no SD |
| **Reordenação** | Reordena imagens para definir a sequência do slideshow |
| **Exclusão remota** | Remove imagem do SD e da lista via painel web |
| **Config persistente** | Todas as configurações salvas no SD (`/config.txt`) |
| **Modo AP + STA** | Opera como AP standalone ou conectado a rede existente |
| **Controle de brilho** | PWM no backlight do display (1–255) via painel |
| **Mutex FreeRTOS** | Acesso seguro ao SD entre FTP, display e servidor web |
| **Ordem persistente** | Ordem das imagens salva em `/slides/order.txt` |
| **Evil Portal** | Página de phishing servida a dispositivos conectados ao AP, com suporte a template via SD |
| **DNS Spoof** | Servidor DNS na porta 53 redirecionando qualquer domínio — aciona captive portal em Android, iOS e Windows |
| **Captura de credenciais** | Grava qualquer campo de formulário em `/creds.csv` com timestamp e IP do cliente |
| **Portal customizável** | Substitui a página padrão por `/portal.html` enviado via FTP — qualquer template HTML é aceito |
| **Furtividade total** | Evil portal opera em paralelo ao slideshow — o display não revela a operação |

---

## 🔧 Hardware

O OSEP-32 é construído sobre a **ESP32-2432S028R** ("Cheap Yellow Display" / CYD) — uma placa all-in-one que integra ESP32, display ILI9341 2.8" 320×240, leitor de microSD e touch capacitivo em um único PCB. **Nenhuma fiação externa é necessária.**

### Lista de Componentes

| Componente | Especificação | Qtd. |
|---|---|:---:|
| **ESP32-2432S028R (CYD)** | ESP32 + Display ILI9341 2.8" 320×240 + SD card + touch — all-in-one | 1 |
| **Cartão microSD** | 1 GB (ou superior), formatado em FAT32 | 1 |
| **Cabo USB-C** | Para gravação do firmware e alimentação | 1 |
| **Case de acrílico** | Proteção e acabamento (opcional) | 1 |

> ✅ **Vantagem:** por ser uma placa all-in-one, basta inserir o cartão microSD, conectar o USB-C e gravar o firmware. Sem jumpers, sem protoboard, sem soldas.

---

## 📌 Pinagem Interna (ESP32-2432S028R)

### Display TFT ILI9341

| GPIO (ESP32) | Função | Sinal |
|:---:|---|---|
| GPIO 14 | SPI Clock do display | TFT_SCLK |
| GPIO 12 | SPI MISO do display | TFT_MISO |
| GPIO 13 | SPI MOSI do display | TFT_MOSI |
| GPIO 15 | Chip Select do display | TFT_CS |
| GPIO 2  | Data / Command select | TFT_DC |
| GPIO 21 | Backlight PWM | TFT_BL |
| RST | Reset (ligado ao reset geral) | TFT_RST |

### Leitor microSD (barramento SPI separado)

| GPIO (ESP32) | Função | Sinal |
|:---:|---|---|
| GPIO 18 | SPI Clock do SD | SD_SCK |
| GPIO 19 | SPI MISO do SD | SD_MISO |
| GPIO 23 | SPI MOSI do SD | SD_MOSI |
| GPIO 5  | Chip Select do SD | SD_CS |

> ⚠️ **Importante:** na ESP32-2432S028R o display e o SD utilizam **barramentos SPI independentes**.

---

## 🚀 Instalação do Firmware

1. Formate o cartão microSD em **FAT32** e insira na placa
2. Conecte a placa ao computador via cabo USB-C
3. Instale as bibliotecas listadas na seção [Bibliotecas](#-bibliotecas-necessárias)
4. Configure o `User_Setup.h` da TFT_eSPI conforme a [seção de configuração](#-configuração-da-tft_espi)
5. Abra `syh2_offensive_social_engineering_device.ino` no Arduino IDE
6. Selecione: `Ferramentas > Placa > ESP32 Dev Module`
7. Selecione a porta COM correta em `Ferramentas > Porta`
8. Clique em **Upload**
9. Na primeira inicialização, o firmware cria automaticamente `/config.txt` e `/slides` no SD
10. Conecte ao AP Wi-Fi `OSEP-32(SYH2)` (senha: `solydsyh2`)
11. Acesse `http://192.168.4.1` e faça login com senha `solyd`

---

## ⚙️ Configuração da TFT_eSPI

Edite o arquivo `User_Setup.h` na pasta da biblioteca TFT_eSPI:

```cpp
#define ILI9341_2_DRIVER
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TOUCH_CS 33
#define SPI_FREQUENCY      27000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000
```

---

## 📦 Bibliotecas Necessárias

| Biblioteca | Versão | Observação |
|---|---|---|
| ESP32 Arduino Core | 3.3.8+ | Via Board Manager |
| AsyncTCP | 1.1.4+ | Library Manager |
| ESPAsyncWebServer | 3.x+ | Library Manager |
| TFT_eSPI | 2.5.x+ | Requer configuração do `User_Setup.h` |
| TJpg_Decoder | 1.x+ | Library Manager |
| ESP32FtpServer | — | By **robo8080** — Library Manager |
| DNSServer | — | **Inclusa no ESP32 Arduino Core** — não requer instalação separada |

---

## 🏗️ Arquitetura do Firmware

```
syh2_offensive_social_engineering_device_v2.ino
│
├── Configuração        saveConfig() / loadConfig()
│
├── Imagens             loadImagesFromSD() / saveOrder()
│                       moveImage() / deleteImageByName()
│
├── Display             drawImageByName() / drawStatusOverlay()
│
├── Rede                startWifi() — Modo dual AP+STA
│
├── DNS                 startDns() — Spoof wildcard porta 53
│
├── FTP                 startFtp() — Porta 21
│
├── Evil Portal         defaultPortalPage() / captureSuccessPage()
│                       saveCredential() / buildCredsJson()
│
├── Web                 startWeb() — 16 rotas REST
│
└── Loop principal      Slideshow + evil portal em paralelo
```

<img width="1800" height="2080" alt="OSEP32_arquitetura_firmware" src="https://github.com/user-attachments/assets/b8914b0c-2a5a-4cf6-88f0-8f73329d2a08" />

### Decisões Técnicas

**Furtividade do evil portal**
O evil portal opera sem interromper o slideshow. O flag `evilPortalActive` controla apenas o roteamento HTTP — o loop principal continua renderizando imagens normalmente, sem indicação visual no display.

**DNS Spoof wildcard**
O `DNSServer` responde qualquer consulta DNS com o IP do AP (`192.168.4.1`). Combinado com os endpoints de detecção de captive portal (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt` etc.), o pop-up automático de captive portal é acionado em Android, iOS e Windows sem interação do usuário.

**Captura agnóstica de campos**
O endpoint `/capture` itera sobre todos os parâmetros POST recebidos e os grava no CSV. Não há dependência de nomes específicos de campos — qualquer formulário HTML com `action="/capture" method="POST"` funciona automaticamente.

**Mutex FreeRTOS para o SD**
O `sdMutex` garante acesso exclusivo ao SD em todos os pontos de leitura e escrita entre o loop principal, o servidor HTTP assíncrono e o servidor FTP.

**Arrays globais para evitar Stack Overflow**
`images[]` e `found[]` (200 elementos `String` cada) são globais. A declaração local consumia ~5 KB de stack por chamada, causando boot loop.

---

## 🌐 API REST

Todas as rotas protegidas exigem o parâmetro `token` com a senha do painel.

| Método | Endpoint | Descrição | Auth |
|:---:|---|---|:---:|
| `GET` | `/` | Página de login / portal de phishing (quando evil portal ativo) | ❌ |
| `GET` | `/panel` | Painel de controle (4 abas) | ✅ |
| `GET` | `/api/list` | Lista JSON de imagens | ✅ |
| `GET` | `/img?name=X` | Serve imagem JPEG do SD | ✅ |
| `POST` | `/api/show` | Exibe imagem imediatamente | ✅ |
| `POST` | `/api/pause` | Toggle pausa/retomada do slideshow | ✅ |
| `POST` | `/api/move` | Reordena imagem (params: from, to) | ✅ |
| `POST` | `/api/delete` | Remove imagem do SD | ✅ |
| `POST` | `/api/reload` | Força recarregamento da lista | ✅ |
| `POST` | `/api/config` | Salva configurações | ✅ |
| `POST` | `/api/reboot` | Reinicia o dispositivo | ✅ |
| `POST` | `/api/portal` | Toggle ativação/desativação do evil portal | ✅ |
| `GET` | `/api/creds` | Lista JSON de credenciais capturadas | ✅ |
| `GET` | `/api/creds/download` | Download do arquivo creds.csv | ✅ |
| `POST` | `/api/creds/clear` | Apaga o arquivo creds.csv do SD | ✅ |
| `POST` | `/capture` | Recebe campos do formulário de phishing e salva no SD | ❌ |

---

## 📖 Guia de Operação

### Primeiro Acesso

1. Ligue o dispositivo via USB-C ou fonte 5V
2. Aguarde a tela exibir o endereço IP (~3 segundos)
3. Conecte ao Wi-Fi `OSEP-32(SYH2)` — senha `solydsyh2`
4. Acesse `http://192.168.4.1` no browser
5. Faça login com a senha padrão: `solyd`

### Envio de Imagens via FTP

1. Abra o **FileZilla** ou **WinSCP**
2. Conecte: Host `192.168.4.1`, Porta `21`, Usuário `osep`, Senha `osep1234`
3. Navegue até `/slides` e arraste as imagens JPEG
4. No painel web, clique em **Atualizar galeria**

> 💡 Use o script `osep_sender.py` para converter e enviar automaticamente qualquer imagem para o formato correto.

### Controle do Slideshow

| Ação | Efeito |
|---|---|
| **Mostrar agora** | Exibe a imagem imediatamente |
| **⏸ Pausar slideshow** | Fixa a imagem atual na tela |
| **▶ Retomar slideshow** | Volta ao ciclo automático |
| **↑ / ↓** | Reordena a imagem na sequência |
| **Excluir** | Remove permanentemente a imagem do SD |

---

## 🎣 Evil Portal

### Ativação

1. No painel web, acesse a aba **Evil Portal**
2. Clique em **Ativar Evil Portal** — o status muda para `ATIVO`
3. O slideshow **continua normalmente** no display (furtividade total)
4. Qualquer dispositivo conectado ao AP é redirecionado automaticamente para a página de phishing

### Portal Customizado

Envie um arquivo `portal.html` via FTP para a **raiz do SD** (não para `/slides`). Único requisito do formulário:

```html
<form action="/capture" method="POST">
  <!-- qualquer campo — todos são capturados automaticamente -->
  <input type="email" name="email">
  <input type="password" name="senha">
  <button type="submit">Entrar</button>
</form>
```

### Visualização das Credenciais

Na aba **Credenciais** do painel web:
- Tabela com todas as capturas (timestamp, IP, campos)
- Botão **Download CSV** — exporta `/creds.csv`
- Botão **Limpar** — apaga todas as entradas remotamente

### Compatibilidade de Captive Portal

O DNS spoof wildcard + endpoints de detecção disparam o pop-up automático em:

| Sistema | Endpoint detectado |
|---|---|
| Android | `/generate_204`, `/gen_204` |
| iOS / macOS | `/hotspot-detect.html`, `/library/test/success.html` |
| Windows | `/ncsi.txt`, `/connecttest.txt` |

---

## 🐍 Script osep_sender.py

### Instalação

```bash
pip install pillow watchdog
```

### Uso

```bash
# Monitoramento contínuo
python osep_sender.py

# Com argumentos
python osep_sender.py --host 192.168.1.50 --watch ./imagens --done ./enviados

# Processar e sair
python osep_sender.py --no-watch
```

### Funcionalidades

- Monitoramento contínuo de pasta via `watchdog`
- Conversão automática para JPEG 320×240 baseline, qualidade 90
- Detecção e correção de rotação EXIF
- Rotação automática de retrato para paisagem
- Letterbox preto para proporções diferentes de 4:3
- Envio automático via FTP após conversão
- Suporte a: PNG, BMP, GIF, WebP, TIFF, JPEG

---

## 🔑 Credenciais Padrão

| Serviço | Usuário | Senha |
|---|---|---|
| Wi-Fi AP | `OSEP-32(SYH2)` | `solydsyh2` |
| Painel Web | *(token)* | `solyd` |
| FTP | `osep` | `osep1234` |

> 🔴 **SEGURANÇA:** altere todas as credenciais padrão antes de utilizar em campo.

---

## 📁 Estrutura do Repositório

```
osep-32/
├── syh2_offensive_social_engineering_device_v2.ino  # Firmware v2 (slideshow + evil portal)
├── User_Setup.h                                      # Configuração TFT_eSPI para CYD
├── osep_sender.py                                    # Script de conversão e envio FTP
└── README.md                                         # Este arquivo
```

---

## ⚖️ Aviso Legal

Esta ferramenta foi desenvolvida **exclusivamente para fins educacionais e profissionais de segurança ofensiva**. O uso do OSEP-32 é permitido somente em ambientes autorizados, como laboratórios de segurança, exercícios de Red Team com escopo definido e treinamentos controlados.

**O uso não autorizado desta ferramenta contra sistemas ou pessoas sem consentimento explícito é ilegal e antiético.** Os autores não se responsabilizam por qualquer uso indevido.

---

<div align="center">

Desenvolvido para a certificação **SYH2 — Solyd Offensive Security**

</div>
