// ====== ESP32-2432S028 (Cheap Yellow Display) - User_Setup.h ======

#define USER_SETUP_INFO "CYD_ESP32-2432S028"

// Driver correto para muita CYD
#define ILI9341_2_DRIVER
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON

// Tamanho do painel (portrait)
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Backlight (você confirmou que 21 liga)
#define TFT_BL 21
#define TFT_BACKLIGHT_ON HIGH

// Pinos TFT (CYD)
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1

// Touch (se você for usar depois)
#define TOUCH_CS 33

// Frequência SPI (segura)
#define SPI_FREQUENCY      27000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000

// Fontes (pode manter padrão)
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
