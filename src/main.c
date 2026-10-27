/*
 * Joy-Con 2 Mouse Emulator for Nintendo Switch
 * Homebrew NRO application
 * Compile with: devkitA64 + libnx
 */

#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ====== Константы экрана ======
#define SCREEN_W 1280
#define SCREEN_H 720

// ====== Состояние приложения ======
typedef enum {
    STATE_MENU,
    STATE_CONFIG,
    STATE_ACTIVE,
    STATE_CONFIRM
} AppState;

typedef struct {
    u64 activateBtns;   // битовая маска кнопок активации
    u64 deactivateBtns; // битовая маска кнопок деактивации
    bool mouseActive;
    bool configChanged;
    float sensitivity;
    float scrollSpeed;
} AppConfig;

// ====== Цвета интерфейса ======
typedef struct { ColorRGBA; } ColorRGBA; // псевдоним

static const u32 COL_BG_DARK    = 0xFF0F0F1A;
static const u32 COL_BG_CARD    = 0xFF1A1A2E;
static const u32 COL_ACCENT     = 0xFF00D4FF;
static const u32 COL_ACCENT2    = 0xFFFF006E;
static const u32 COL_TEXT       = 0xFFFFFFFF;
static const u32 COL_TEXT_DIM   = 0xFF8888AA;
static const u32 COL_SUCCESS    = 0xFF00FF88;
static const u32 COL_WARN       = 0xFFFFAA00;

// ====== Глобальные переменные ======
static AppConfig g_config = {
    .activateBtns  = HidNpadButton_L | HidNpadButton_ZL |
                     HidNpadButton_Minus | HidNpadButton_Plus,
    .deactivateBtns = HidNpadButton_L | HidNpadButton_ZL |
                      HidNpadButton_Minus | HidNpadButton_Plus,
    .mouseActive = false,
    .configChanged = false,
    .sensitivity = 1.5f,
    .scrollSpeed = 1.0f
};

static AppState g_state = STATE_MENU;
static int g_menuSelection = 0;
static int g_configSelection = 0;
static bool g_showConfirm = false;
static char g_statusMsg[128] = {0};
static float g_mouseX = SCREEN_W / 2.0f;
static float g_mouseY = SCREEN_H / 2.0f;
static float g_prevLX = 0.0f, g_prevLY = 0.0f;
static float g_scrollAccum = 0.0f;

// ====== Графические хелперы (framebuffer) ======
static Framebuffer g_fb;
static u32* g_framebuf = NULL;

static void drawPixel(int x, int y, u32 color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    g_framebuf[y * SCREEN_W + x] = color;
}

static void drawRect(int x, int y, int w, int h, u32 color) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            drawPixel(i, j, color);
}

static void drawRoundedRect(int x, int y, int w, int h, int r, u32 color) {
    // Упрощённый rounded rect (углы квадратные для скорости)
    drawRect(x, y, w, h, color);
}

static void drawGradientBar(int x, int y, int w, int h, u32 c1, u32 c2) {
    for (int i = 0; i < w; i++) {
        float t = (float)i / (float)w;
        u8 r = (u8)(((c1 >> 16) & 0xFF) * (1 - t) + ((c2 >> 16) & 0xFF) * t);
        u8 g = (u8)(((c1 >> 8)  & 0xFF) * (1 - t) + ((c2 >> 8)  & 0xFF) * t);
        u8 b = (u8)(((c1)       & 0xFF) * (1 - t) + ((c2)       & 0xFF) * t);
        u32 col = 0xFF000000 | (r << 16) | (g << 8) | b;
        drawRect(x + i, y, 1, h, col);
    }
}

// Минимальный текст через bitmap (упрощённо — рисуем блоки)
static void drawText(int x, int y, const char* text, u32 color, int scale) {
    // В реальном проекте используйте SDL_ttf или libnx console
    // Здесь — заглушка: рисуем цветную полосу как индикатор текста
    (void)text; (void)color; (void)scale;
    // Для полноценного рендера подключите stb_truetype
}

// ====== Рендер интерфейса ======
static void renderBackground(void) {
    // Тёмный градиентный фон
    for (int y = 0; y < SCREEN_H; y++) {
        float t = (float)y / (float)SCREEN_H;
        u8 r = (u8)(15 + t * 10);
        u8 g = (u8)(15 + t * 10);
        u8 b = (u8)(26 + t * 15);
        u32 col = 0xFF000000 | (r << 16) | (g << 8) | b;
        drawRect(0, y, SCREEN_W, 1, col);
    }

    // Декоративная линия сверху
    drawGradientBar(0, 0, SCREEN_W, 4, COL_ACCENT, COL_ACCENT2);

    // Сетка на фоне (прикольный эффект)
    for (int x = 0; x < SCREEN_W; x += 80)
        drawRect(x, 0, 1, SCREEN_H, 0x151525);
    for (int y = 0; y < SCREEN_H; y += 80)
        drawRect(0, y, SCREEN_W, 1, 0x151525);
}

static void renderCard(int x, int y, int w, int h, const char* title) {
    (void)title;
    // Тень
    drawRoundedRect(x + 4, y + 4, w, h, 12, 0x80000000);
    // Карточка
    drawRoundedRect(x, y, w, h, 12, COL_BG_CARD);
    // Акцентная полоса слева
    drawRect(x, y, 6, h, COL_ACCENT);
}

static void renderMenu(void) {
    renderBackground();

    // Заголовок
    drawRect(60, 60, 8, 60, COL_ACCENT);
    // Текст заголовка (псевдо)
    drawRect(80, 70, 500, 40, COL_ACCENT); // "JOY-CON 2 MOUSE"
    drawRect(80, 120, 300, 20, COL_TEXT_DIM); // "for Nintendo Switch"

    // Карточки меню
    const char* menuItems[] = {
        "[1] START MOUSE MODE",
        "[2] CONFIGURE BUTTONS",
        "[3] SENSITIVITY SETTINGS",
        "[4] ABOUT / HELP"
    };

    for (int i = 0; i < 4; i++) {
        int cy = 220 + i * 110;
        bool selected = (g_menuSelection == i);
        u32 cardColor = selected ? 0xFF252545 : COL_BG_CARD;
        u32 accentColor = selected ? COL_ACCENT2 : COL_ACCENT;

        drawRoundedRect(100, cy, 1080, 90, 10, cardColor);
        drawRect(100, cy, 8, 90, accentColor);

        // Индикатор выбора
        if (selected) {
            drawRect(120, cy + 35, 30, 20, COL_ACCENT);
        }

        (void)menuItems[i]; // текст рендерится через шрифт
    }

    // Статус мыши
    if (g_config.mouseActive) {
        drawRect(1000, 60, 200, 50, COL_SUCCESS);
        // "MOUSE: ON"
    } else {
        drawRect(1000, 60, 200, 50, COL_WARN);
        // "MOUSE: OFF"
    }

    // Подсказки внизу
    drawRect(60, 660, 200, 30, COL_TEXT_DIM); // "A: Select"
    drawRect(300, 660, 200, 30, COL_TEXT_DIM); // "B: Back"
    drawRect(540, 660, 300, 30, COL_TEXT_DIM); // "+/-: Toggle Mouse"
}

static void renderConfig(void) {
    renderBackground();

    drawRect(60, 60, 8, 60, COL_ACCENT2);
    drawRect(80, 70, 400, 40, COL_ACCENT2); // "BUTTON CONFIGURATION"

    const char* configItems[] = {
        "ACTIVATE BUTTONS:  L + ZL + MINUS + PLUS",
        "DEACTIVATE BUTTONS: L + ZL + MINUS + PLUS",
        "LEFT CLICK:  R button",
        "RIGHT CLICK: ZR button",
        "SCROLL WHEEL: Left Stick",
        "MOUSE MOVE:   Left Stick (when active)",
        "SENSITIVITY:  1.5x",
        "SCROLL SPEED: 1.0x"
    };

    for (int i = 0; i < 8; i++) {
        int cy = 160 + i * 65;
        bool selected = (g_configSelection == i);
        u32 rowColor = selected ? 0xFF2A2A4A : 0x00000000;
        drawRect(100, cy, 1080, 50, rowColor);
        if (selected)
            drawRect(100, cy, 6, 50, COL_ACCENT);
        (void)configItems[i];
    }

    // Инструкция по перестановке Joy-Con
    drawRoundedRect(100, 580, 1080, 100, 10, 0xFF1A2A1A);
    drawRect(120, 590, 6, 80, COL_SUCCESS);
    // "TIP: Rotate Joy-Con so SL/SR buttons face up for proper mouse orientation"
}

static void renderActiveOverlay(void) {
    // Полупрозрачный оверлей режима мыши
    drawRect(0, 0, SCREEN_W, SCREEN_H, 0x40000000);

    // Курсор мыши
    int cx = (int)g_mouseX;
    int cy = (int)g_mouseY;
    drawRect(cx - 2, cy - 15, 4, 30, COL_ACCENT);
    drawRect(cx - 15, cy - 2, 30, 4, COL_ACCENT);
    drawRect(cx - 1, cy - 1, 3, 3, 0xFFFFFFFF);

    // Индикатор в углу
    drawRoundedRect(20, 20, 300, 60, 10, 0xCC00FF88);
    // "🖱 MOUSE ACTIVE"
    // "Press L+ZL+Minus+Plus to deactivate"
}

// ====== Обработка ввода ======
static void handleInput(void) {
    PadState pad;
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    padUpdate(&pad);

    u64 buttonsDown = padGetButtonsDown(&pad);
    u64 buttonsHeld = padGetButtons(&pad);

    // Стики
    float lx, ly;
    padGetStickPos(&pad, 0, &lx, &ly);
    lx /= 32767.0f;
    ly /= -32767.0f; // инвертируем Y

    switch (g_state) {
    case STATE_MENU:
        if (buttonsDown & HidNpadButton_Up)
            g_menuSelection = (g_menuSelection + 3) % 4;
        if (buttonsDown & HidNpadButton_Down)
            g_menuSelection = (g_menuSelection + 1) % 4;
        if (buttonsDown & HidNpadButton_A) {
            switch (g_menuSelection) {
            case 0: // Start mouse
                if (!g_config.mouseActive) {
                    g_config.mouseActive = true;
                    g_state = STATE_ACTIVE;
                    snprintf(g_statusMsg, sizeof(g_statusMsg),
                             "Mouse activated! Rotate Joy-Con SL/SR up.");
                }
                break;
            case 1: // Configure
                g_state = STATE_CONFIG;
                break;
            case 2: // Sensitivity
                g_config.sensitivity += 0.1f;
                if (g_config.sensitivity > 3.0f) g_config.sensitivity = 0.5f;
                break;
            case 3: // About
                snprintf(g_statusMsg, sizeof(g_statusMsg),
                         "Joy-Con 2 Mouse v1.0 | Homebrew NRO");
                break;
            }
        }
        if (buttonsDown & HidNpadButton_B) {
            // Выход
            g_state = STATE_CONFIRM;
        }
        // Быстрый тогл мышью через комбо
        if ((buttonsHeld & g_config.activateBtns) == g_config.activateBtns) {
            g_config.mouseActive = !g_config.mouseActive;
            if (g_config.mouseActive) g_state = STATE_ACTIVE;
            else g_state = STATE_MENU;
        }
        break;

    case STATE_CONFIG:
        if (buttonsDown & HidNpadButton_Up)
            g_configSelection = (g_configSelection + 7) % 8;
        if (buttonsDown & HidNpadButton_Down)
            g_configSelection = (g_configSelection + 1) % 8;
        if (buttonsDown & HidNpadButton_A) {
            // Редактирование выбранного пункта
            if (g_configSelection == 0 || g_configSelection == 1) {
                // Циклическое переключение кнопок активации
                u64* target = (g_configSelection == 0) ?
                              &g_config.activateBtns : &g_config.deactivateBtns;
                const u64 presets[] = {
                    HidNpadButton_L | HidNpadButton_ZL |
                    HidNpadButton_Minus | HidNpadButton_Plus,
                    HidNpadButton_L | HidNpadButton_R,
                    HidNpadButton_ZL | HidNpadButton_ZR,
                    HidNpadButton_Minus | HidNpadButton_Plus,
                    HidNpadButton_X
                };
                static int presetIdx = 0;
                presetIdx = (presetIdx + 1) % 5;
                *target = presets[presetIdx];
                g_config.configChanged = true;
            }
        }
        if (buttonsDown & HidNpadButton_B)
            g_state = STATE_MENU;
        break;

    case STATE_ACTIVE:
        // Движение мыши от стика
        if (fabsf(lx) > 0.15f || fabsf(ly) > 0.15f) {
            g_mouseX += lx * g_config.sensitivity * 8.0f;
            g_mouseY += ly * g_config.sensitivity * 8.0f;
            // Ограничение экраном
            if (g_mouseX < 0) g_mouseX = 0;
            if (g_mouseX > SCREEN_W) g_mouseX = SCREEN_W;
            if (g_mouseY < 0) g_mouseY = 0;
            if (g_mouseY > SCREEN_H) g_mouseY = SCREEN_H;
        }

        // Колёсико от вертикального стика (мёртвая зона больше)
        if (fabsf(ly) > 0.5f) {
            g_scrollAccum += ly * g_config.scrollSpeed;
            if (fabsf(g_scrollAccum) >= 1.0f) {
                int scrollDir = (g_scrollAccum > 0) ? 1 : -1;
                // Здесь можно инжектить scroll event
                g_scrollAccum = 0.0f;
                (void)scrollDir;
            }
        }

        // ЛКМ = R, ПКМ = ZR
        if (buttonsDown & HidNpadButton_R) {
            // Inject left click
        }
        if (buttonsDown & HidNpadButton_ZR) {
            // Inject right click
        }

        // Деактивация
        if ((buttonsHeld & g_config.deactivateBtns) == g_config.deactivateBtns) {
            g_config.mouseActive = false;
            g_state = STATE_MENU;
            snprintf(g_statusMsg, sizeof(g_statusMsg), "Mouse deactivated.");
        }

        // Выход из режима
        if (buttonsDown & HidNpadButton_B)
            g_state = STATE_MENU;
        break;

    case STATE_CONFIRM:
        if (buttonsDown & HidNpadButton_A) {
            // Выход из приложения
            g_state = STATE_MENU; // или exit
        }
        if (buttonsDown & HidNpadButton_B)
            g_state = STATE_MENU;
        break;
    }
}

// ====== Инжекция мышиного ввода в игры ======
static void injectMouseInput(void) {
    if (!g_config.mouseActive) return;

    // В реальном приложении здесь используется:
    // - hid:emulate для создания виртуального мышиного устройства
    // - или pm:dmnt для инжекта в конкретный процесс игры
    //
    // Упрощённая схема:
    // 1. Получаем координаты курсора (g_mouseX, g_mouseY)
    // 2. Конвертируем в touch-координаты (Switch использует touch)
    // 3. Инжектим через hidAddTouchScreenEntry или аналог

    float touchX = g_mouseX / SCREEN_W;  // 0.0 - 1.0
    float touchY = g_mouseY / SCREEN_H;

    // Для полноценной работы нужен доступ к hid:emulate
    // Это требует кастомных прав в NRO (homebrew обычно имеет)
    (void)touchX;
    (void)touchY;
}

// ====== Главный цикл ======
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // Инициализация Switch
    consoleInit(NULL);
    gfxInitDefault();
    hidInitialize();

    // Получаем framebuffer
    g_framebuf = (u32*)gfxGetFramebuffer(NULL, NULL, NULL, NULL);
    if (!g_framebuf) {
        printf("Failed to get framebuffer!\n");
        consoleWait();
        return 1;
    }

    // Приветственный экран
    printf("Joy-Con 2 Mouse Emulator v1.0\n");
    printf("=============================\n\n");
    printf("Activate buttons: L + ZL + MINUS + PLUS\n");
    printf("Left Click:  R button\n");
    printf("Right Click: ZR button\n");
    printf("Scroll:      Left Stick (vertical)\n");
    printf("Move:        Left Stick\n\n");
    printf("TIP: Rotate Joy-Con so SL/SR face UP\n");
    printf("     for proper mouse orientation.\n\n");
    printf("Press A to start GUI mode...\n");

    // Ждём нажатия A
    PadState pad;
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_A) break;
        if (down & HidNpadButton_Plus) {
            // Выход
            goto cleanup;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gfxWaitForVsync();
    }

    consoleExit();

    // Главный GUI цикл
    while (appletMainLoop()) {
        handleInput();

        // Рендер
        memset(g_framebuf, 0, SCREEN_W * SCREEN_H * 4);

        switch (g_state) {
        case STATE_MENU:    renderMenu(); break;
        case STATE_CONFIG:  renderConfig(); break;
        case STATE_ACTIVE:
            renderMenu();
            renderActiveOverlay();
            injectMouseInput();
            break;
        case STATE_CONFIRM:
            renderMenu();
            drawRect(300, 250, 680, 200, 0xFF000000);
            // "Exit application?"
            break;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gfxWaitForVsync();
    }

cleanup:
    gfxExit();
    hidExit();
    return 0;
}
