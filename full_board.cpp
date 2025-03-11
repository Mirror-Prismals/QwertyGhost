// main.cpp
// Compile on Windows with (example):
//   cl main.cpp /I"path_to_glm" /I"path_to_stb" /EHsc /link glfw3.lib opengl32.lib user32.lib gdi32.lib

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <thread>
#include "stb_easy_font.h"

// -------------------------
// Constants & Global Settings (20% increased)
// -------------------------
constexpr float KEY_WIDTH = 60.0f;         // originally 50.0f
constexpr float KEY_HEIGHT = 60.0f;        // originally 50.0f
constexpr float KEY_SPACING_X = 6.0f;      // originally 5.0f
constexpr float KEY_SPACING_Y = 6.0f;      // originally 5.0f
constexpr float KEY_DEPTH = 18.0f;         // originally 15.0f
constexpr double PRESS_FEEDBACK_DURATION = 0.15; // seconds for press animation

// Global position for main keyboard block (will be centered)
float g_mainStartX = 50.0f;
float g_mainStartY = 50.0f;

// -------------------------
// Embedded ChucK Code (Simple Click)
// -------------------------
const char* embeddedChucKCode = R"(
// Ultra-Crisp Mechanical Keyboard Click in ChucK
//
// This version focuses on a very sharp, high-frequency burst to simulate an
// extremely loud mechanical key click. The parameters are tuned to avoid any low-end muddiness.

Noise clickNoise => HPF noiseHPF => ADSR noiseEnv => dac;
SinOsc clickSine => ADSR sineEnv => dac;

// Noise component: ultra-short burst for the raw click edge
1.0 => clickNoise.gain;
5000 => noiseHPF.freq;      // High-pass filter to cut out lower frequencies
noiseEnv.set(0, 1, 0.0003, 0.02); // Blisteringly fast attack and decay

// Sine component: a piercing transient to accentuate the click
10000 => clickSine.freq;    // Extremely high frequency for extra snap
1.0 => clickSine.gain;
sineEnv.set(0, 1, 0.0001, 0.015);  // Even shorter envelope for a razor-thin burst

// Fire both components simultaneously for maximum impact
noiseEnv.keyOn();
sineEnv.keyOn();
1::ms => now;   // A brief moment for the click to be audible
noiseEnv.keyOff();
sineEnv.keyOff();
10::ms => now;  // Allow the tails to decay naturally


)";
const char* TEMP_CHUCK_FILENAME = "temp_chuck.ck";

// -------------------------
// Key Types & Colors
// -------------------------
enum class KeyType {
    ALPHANUM,       // Letters, digits, punctuation
    FUNCTION,       // F-keys and similar
    MODIFIER,       // Shift, Ctrl, Alt, etc.
    NAVIGATION,     // Navigation keys (e.g., Ins, Home, PgUp, Del, End, PgDn)
    ARROW,          // Arrow keys (Up, Down, Left, Right)
    NUMPAD,         // Numeric keypad (not used now)
    BACKGROUND_ONLY // For the 'housing'
};

static void getBaseColor(KeyType type, float& r, float& g, float& b)
{
    switch (type) {
    case KeyType::FUNCTION:
        r = 0.8f; g = 0.8f; b = 0.8f;
        break;
    case KeyType::MODIFIER:
        r = 0.85f; g = 0.85f; b = 0.80f;
        break;
    case KeyType::NUMPAD:
        r = 0.90f; g = 0.90f; b = 0.85f;
        break;
    case KeyType::ALPHANUM:
    default:
        r = 0.93f; g = 0.93f; b = 0.88f;
        break;
    }
}

// -------------------------
// Key Structure
// -------------------------
struct Key {
    std::string label;
    glm::vec2 pos;    // Top-left corner position
    glm::vec2 size;
    float pressAnim = 0.0f; // 0.0 (up) to 0.5 (fully pressed)
    bool isPressed = false;
    bool keycapRemoved = false; // if true, show advanced mechanical switch design
    KeyType type;     // For coloring
};

std::vector<Key> keyboardKeys;
std::map<int, int> glfwKeyToIndex;
double g_lastFrameTime = 0.0;

// Global flag for left mouse button state
bool g_leftMouseDown = false;

// -------------------------
// Advanced Drawing Functions
// -------------------------
void drawBeveledBox3D(float x, float y, float w, float h,
    float depth,
    float baseR, float baseG, float baseB)
{
    float bevel = depth * 0.5f;
    // FRONT face
    glColor3f(baseR, baseG, baseB);
    glBegin(GL_QUADS);
    glVertex3f(x, y, 0);
    glVertex3f(x + w, y, 0);
    glVertex3f(x + w, y + h, 0);
    glVertex3f(x, y + h, 0);
    glEnd();

    // TOP face
    glColor3f(baseR + 0.07f, baseG + 0.07f, baseB + 0.07f);
    glBegin(GL_QUADS);
    glVertex3f(x, y, 0);
    glVertex3f(x + w, y, 0);
    glVertex3f(x + w - bevel, y - bevel, -depth);
    glVertex3f(x - bevel, y - bevel, -depth);
    glEnd();

    // RIGHT face
    glColor3f(baseR - 0.05f, baseG - 0.05f, baseB - 0.05f);
    glBegin(GL_QUADS);
    glVertex3f(x + w, y, 0);
    glVertex3f(x + w, y + h, 0);
    glVertex3f(x + w - bevel, y + h - bevel, -depth);
    glVertex3f(x + w - bevel, y - bevel, -depth);
    glEnd();

    // BOTTOM face
    glColor3f(baseR - 0.02f, baseG - 0.02f, baseB - 0.02f);
    glBegin(GL_QUADS);
    glVertex3f(x, y + h, 0);
    glVertex3f(x + w, y + h, 0);
    glVertex3f(x + w - bevel, y + h - bevel, -depth);
    glVertex3f(x - bevel, y + h - bevel, -depth);
    glEnd();

    // LEFT face
    glColor3f(baseR - 0.03f, baseG - 0.03f, baseB - 0.03f);
    glBegin(GL_QUADS);
    glVertex3f(x, y, 0);
    glVertex3f(x, y + h, 0);
    glVertex3f(x - bevel, y + h - bevel, -depth);
    glVertex3f(x - bevel, y - bevel, -depth);
    glEnd();
}

void drawThreeFacedCube(float x, float y, float w, float h,
    float depth,
    float baseR, float baseG, float baseB)
{
    // FRONT face
    glColor3f(baseR, baseG, baseB);
    glBegin(GL_QUADS);
    glVertex3f(x, y, 0);
    glVertex3f(x + w, y, 0);
    glVertex3f(x + w, y + h, 0);
    glVertex3f(x, y + h, 0);
    glEnd();

    float bevel = depth * 0.5f;
    // TOP face
    glColor3f(baseR + 0.07f, baseG + 0.07f, baseB + 0.07f);
    glBegin(GL_QUADS);
    glVertex3f(x, y, 0);
    glVertex3f(x + w, y, 0);
    glVertex3f(x + w - bevel, y - bevel, -depth);
    glVertex3f(x - bevel, y - bevel, -depth);
    glEnd();

    // LEFT face
    glColor3f(baseR - 0.03f, baseG - 0.03f, baseB - 0.03f);
    glBegin(GL_QUADS);
    glVertex3f(x, y, 0);
    glVertex3f(x, y + h, 0);
    glVertex3f(x - bevel, y + h - bevel, -depth);
    glVertex3f(x - bevel, y - bevel, -depth);
    glEnd();
}

void drawKeycap3D(const Key& key) {
    float pressAnim = key.pressAnim;
    float bx = key.pos.x, by = key.pos.y;
    float bw = key.size.x, bh = key.size.y;
    float shiftLeft = 10.0f * pressAnim;
    float shiftUp = 10.0f * pressAnim;
    float pressOffsetZ = KEY_DEPTH * pressAnim;
    float newDepth = KEY_DEPTH * (1.0f - 0.5f * pressAnim);
    float x = bx - shiftLeft;
    float y = by - shiftUp;

    float baseR = 0.9f, baseG = 0.9f, baseB = 0.85f;

    // FRONT face
    glColor3f(baseR, baseG, baseB);
    glBegin(GL_QUADS);
    glVertex3f(x, y, -pressOffsetZ);
    glVertex3f(x + bw, y, -pressOffsetZ);
    glVertex3f(x + bw, y + bh, -pressOffsetZ);
    glVertex3f(x, y + bh, -pressOffsetZ);
    glEnd();

    // TOP face
    glColor3f(baseR + 0.07f, baseG + 0.07f, baseB + 0.07f);
    glBegin(GL_QUADS);
    glVertex3f(x, y, -pressOffsetZ);
    glVertex3f(x + bw, y, -pressOffsetZ);
    glVertex3f(x + bw - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
    glVertex3f(x - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
    glEnd();

    // RIGHT face
    glColor3f(baseR - 0.05f, baseG - 0.05f, baseB - 0.05f);
    glBegin(GL_QUADS);
    glVertex3f(x + bw, y, -pressOffsetZ);
    glVertex3f(x + bw, y + bh, -pressOffsetZ);
    glVertex3f(x + bw - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
    glVertex3f(x + bw - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
    glEnd();

    // BOTTOM face
    glColor3f(baseR - 0.02f, baseG - 0.02f, baseB - 0.02f);
    glBegin(GL_QUADS);
    glVertex3f(x, y + bh, -pressOffsetZ);
    glVertex3f(x + bw, y + bh, -pressOffsetZ);
    glVertex3f(x + bw - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
    glVertex3f(x - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
    glEnd();

    // LEFT face
    glColor3f(baseR - 0.03f, baseG - 0.03f, baseB - 0.03f);
    glBegin(GL_QUADS);
    glVertex3f(x, y, -pressOffsetZ);
    glVertex3f(x, y + bh, -pressOffsetZ);
    glVertex3f(x - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
    glVertex3f(x - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
    glEnd();
}

//
// Show mechanical switch internals when keycapRemoved == true
//
void drawMechanicalSwitch3D(const Key& key) {
    float pressAnim = key.pressAnim;
    float shiftLeft = 10.0f * pressAnim;
    float shiftUp = 10.0f * pressAnim;
    float pressOffsetZ = KEY_DEPTH * pressAnim;

    // Outer "switch housing"
    float bx = key.pos.x + key.size.x * 0.3f;
    float by = key.pos.y + key.size.y * 0.3f;
    float bw = key.size.x * 0.4f;
    float bh = key.size.y * 0.4f;
    float outerDepth = 16.0f * (KEY_DEPTH / 15.0f); // scaled accordingly
    drawBeveledBox3D(bx, by, bw, bh, outerDepth, 0.5f, 0.5f, 0.5f);

    // Inner "stem"
    float animDepth = outerDepth - 6.0f;
    {
        float cloneScale = 0.7f * 0.8f; // 0.56
        float cloneW = bw * cloneScale;
        float cloneH = bh * cloneScale;
        float greenCubeDepth = animDepth * cloneScale * 0.7143f; // ~4.0
        float normalizedPress = pressAnim / 0.5f;
        float restingZ = -(greenCubeDepth / 2.0f);
        float pressedZ = -(greenCubeDepth - 1.0f);
        float zTranslation = restingZ + normalizedPress * (pressedZ - restingZ);

        float cloneX = bx + (bw - cloneW) / 2.0f + 2.0f;
        float cloneY = by + (bh - cloneH) / 2.0f + 2.0f;

        glPushMatrix();
        glTranslatef(-0.5f * shiftLeft, -0.5f * shiftUp, -pressOffsetZ);
        glTranslatef(0, 0, zTranslation);
        glPushAttrib(GL_DEPTH_BUFFER_BIT);
        glDepthFunc(GL_ALWAYS);
        drawThreeFacedCube(cloneX, cloneY, cloneW, cloneH, greenCubeDepth, 0.1f, 0.4f, 0.1f);
        glPopAttrib();
        glPopMatrix();
    }
}

void drawKeyMerged(const Key& key) {
    if (key.keycapRemoved)
        drawMechanicalSwitch3D(key);
    else
        drawKeycap3D(key);
}

void renderText(float x, float y, const char* text) {
    char buffer[99999];
    int num_quads = stb_easy_font_print(
        x,
        y,
        const_cast<char*>(text),
        nullptr,
        buffer,
        sizeof(buffer)
    );

    glDisable(GL_DEPTH_TEST);
    glColor3f(0.0f, 0.0f, 0.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 16, buffer);
    glDrawArrays(GL_QUADS, 0, num_quads * 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glEnable(GL_DEPTH_TEST);
}

void updateKeyAnimation(Key& key, float deltaTime) {
    float animSpeed = 0.5f / static_cast<float>(PRESS_FEEDBACK_DURATION);
    float target = key.isPressed ? 0.5f : 0.0f;
    if (key.pressAnim < target) {
        key.pressAnim += animSpeed * deltaTime;
        if (key.pressAnim > target) key.pressAnim = target;
    }
    else if (key.pressAnim > target) {
        key.pressAnim -= animSpeed * deltaTime;
        if (key.pressAnim < target) key.pressAnim = target;
    }
}

// Helper to add a key & optionally map a GLFW keycode
void addKey(const std::string& label, float x, float y, float w, float h, KeyType kt, int glfwKey)
{
    Key k;
    k.label = label;
    k.pos = glm::vec2(x, y);
    k.size = glm::vec2(w, h);
    k.type = kt;
    keyboardKeys.push_back(k);

    if (glfwKey != -1) {
        int idx = static_cast<int>(keyboardKeys.size()) - 1;
        glfwKeyToIndex[glfwKey] = idx;
    }
}

// -------------------------
// Keyboard Layout
// -------------------------
void initKeyboardLayout() {
    keyboardKeys.clear();
    glfwKeyToIndex.clear();

    // Use the global g_mainStartX, g_mainStartY for the main block's position
    float startX = g_mainStartX;
    float startY = g_mainStartY;
    float currentX = startX;
    float currentY = startY;

    // Row 1: Esc + F-keys
    addKey("Esc", currentX, currentY, KEY_WIDTH, KEY_HEIGHT, KeyType::FUNCTION, GLFW_KEY_ESCAPE);
    currentX += KEY_WIDTH + KEY_SPACING_X;
    for (int i = 0; i < 12; i++) {
        std::string label = "F" + std::to_string(i + 1);
        addKey(label, currentX, currentY, KEY_WIDTH, KEY_HEIGHT, KeyType::FUNCTION, GLFW_KEY_F1 + i);
        currentX += KEY_WIDTH + KEY_SPACING_X;
    }

    // Row 2: Number row and Backspace
    currentX = startX;
    currentY += KEY_HEIGHT + KEY_SPACING_Y;
    std::vector<std::pair<std::string, int>> row2 = {
        {"`", GLFW_KEY_GRAVE_ACCENT},
        {"1", GLFW_KEY_1}, {"2", GLFW_KEY_2}, {"3", GLFW_KEY_3},
        {"4", GLFW_KEY_4}, {"5", GLFW_KEY_5}, {"6", GLFW_KEY_6},
        {"7", GLFW_KEY_7}, {"8", GLFW_KEY_8}, {"9", GLFW_KEY_9},
        {"0", GLFW_KEY_0}, {"-", GLFW_KEY_MINUS}, {"=", GLFW_KEY_EQUAL}
    };
    for (auto& keyPair : row2) {
        addKey(keyPair.first, currentX, currentY, KEY_WIDTH, KEY_HEIGHT, KeyType::ALPHANUM, keyPair.second);
        currentX += KEY_WIDTH + KEY_SPACING_X;
    }
    addKey("Backspace", currentX, currentY, KEY_WIDTH * 1.5f, KEY_HEIGHT, KeyType::MODIFIER, GLFW_KEY_BACKSPACE);

    // Row 3: QWERTY row
    currentX = startX;
    currentY += KEY_HEIGHT + KEY_SPACING_Y;
    std::vector<std::pair<std::string, int>> row3 = {
        {"Q", GLFW_KEY_Q}, {"W", GLFW_KEY_W}, {"E", GLFW_KEY_E},
        {"R", GLFW_KEY_R}, {"T", GLFW_KEY_T}, {"Y", GLFW_KEY_Y},
        {"U", GLFW_KEY_U}, {"I", GLFW_KEY_I}, {"O", GLFW_KEY_O},
        {"P", GLFW_KEY_P}, {"[", GLFW_KEY_LEFT_BRACKET}, {"]", GLFW_KEY_RIGHT_BRACKET}
    };
    for (auto& keyPair : row3) {
        addKey(keyPair.first, currentX, currentY, KEY_WIDTH, KEY_HEIGHT, KeyType::ALPHANUM, keyPair.second);
        currentX += KEY_WIDTH + KEY_SPACING_X;
    }

    // Row 4: ASDF row
    currentX = startX;
    currentY += KEY_HEIGHT + KEY_SPACING_Y;
    std::vector<std::pair<std::string, int>> row4 = {
        {"A", GLFW_KEY_A}, {"S", GLFW_KEY_S}, {"D", GLFW_KEY_D},
        {"F", GLFW_KEY_F}, {"G", GLFW_KEY_G}, {"H", GLFW_KEY_H},
        {"J", GLFW_KEY_J}, {"K", GLFW_KEY_K}, {"L", GLFW_KEY_L},
        {";", GLFW_KEY_SEMICOLON}, {"'", GLFW_KEY_APOSTROPHE}
    };
    for (auto& keyPair : row4) {
        addKey(keyPair.first, currentX, currentY, KEY_WIDTH, KEY_HEIGHT, KeyType::ALPHANUM, keyPair.second);
        currentX += KEY_WIDTH + KEY_SPACING_X;
    }

    // Row 5: Shift row (Left Shift, then Z, X, C, V, B, N, M, then Right Shift)
    currentX = startX;
    currentY += KEY_HEIGHT + KEY_SPACING_Y;
    // Left Shift
    addKey("Shift", currentX, currentY, KEY_WIDTH * 1.5f, KEY_HEIGHT, KeyType::MODIFIER, GLFW_KEY_LEFT_SHIFT);
    currentX += KEY_WIDTH * 1.5f + KEY_SPACING_X;
    // Letters Z, X, C, V, B, N, M
    std::vector<std::pair<std::string, int>> row5 = {
        {"Z", GLFW_KEY_Z}, {"X", GLFW_KEY_X}, {"C", GLFW_KEY_C},
        {"V", GLFW_KEY_V}, {"B", GLFW_KEY_B}, {"N", GLFW_KEY_N},
        {"M", GLFW_KEY_M}
    };
    for (auto& keyPair : row5) {
        addKey(keyPair.first, currentX, currentY, KEY_WIDTH, KEY_HEIGHT, KeyType::ALPHANUM, keyPair.second);
        currentX += KEY_WIDTH + KEY_SPACING_X;
    }
    // Right Shift
    addKey("Shift", currentX, currentY, KEY_WIDTH * 1.5f, KEY_HEIGHT, KeyType::MODIFIER, GLFW_KEY_RIGHT_SHIFT);

    // Row 6: Bottom row: Ctrl, Alt, Space, Alt, Ctrl
    float bottomRowY = currentY + KEY_HEIGHT + KEY_SPACING_Y;
    currentX = startX;
    addKey("Ctrl", currentX, bottomRowY, KEY_WIDTH * 1.2f, KEY_HEIGHT, KeyType::MODIFIER, GLFW_KEY_LEFT_CONTROL);
    currentX += KEY_WIDTH * 1.2f + KEY_SPACING_X;
    addKey("Alt", currentX, bottomRowY, KEY_WIDTH, KEY_HEIGHT, KeyType::MODIFIER, GLFW_KEY_LEFT_ALT);
    currentX += KEY_WIDTH + KEY_SPACING_X;
    addKey("Space", currentX, bottomRowY, KEY_WIDTH * 6.0f, KEY_HEIGHT, KeyType::ALPHANUM, GLFW_KEY_SPACE);
    currentX += KEY_WIDTH * 6.0f + KEY_SPACING_X;
    addKey("Alt", currentX, bottomRowY, KEY_WIDTH, KEY_HEIGHT, KeyType::MODIFIER, GLFW_KEY_RIGHT_ALT);
    currentX += KEY_WIDTH + KEY_SPACING_X;
    addKey("Ctrl", currentX, bottomRowY, KEY_WIDTH * 1.2f, KEY_HEIGHT, KeyType::MODIFIER, GLFW_KEY_RIGHT_CONTROL);

    // -----------------------------
    // Arrow Keys
    // -----------------------------
    // Place arrow keys in an inverted T shape to the right of the main block.
    float arrowBlockX = g_mainStartX + 948.0f + KEY_SPACING_X * 10; // gap after main block (Row 2 width is 948)
    float arrowRowY = bottomRowY; // align with bottom row
    float arrowUpY = arrowRowY - (KEY_HEIGHT + KEY_SPACING_Y);
    // Arrow keys: Left, Down, Right and Up above Down
    addKey("Left", arrowBlockX, arrowRowY, KEY_WIDTH, KEY_HEIGHT, KeyType::ARROW, GLFW_KEY_LEFT);
    addKey("Down", arrowBlockX + (KEY_WIDTH + KEY_SPACING_X), arrowRowY, KEY_WIDTH, KEY_HEIGHT, KeyType::ARROW, GLFW_KEY_DOWN);
    addKey("Right", arrowBlockX + 2 * (KEY_WIDTH + KEY_SPACING_X), arrowRowY, KEY_WIDTH, KEY_HEIGHT, KeyType::ARROW, GLFW_KEY_RIGHT);
    addKey("Up", arrowBlockX + (KEY_WIDTH + KEY_SPACING_X), arrowUpY, KEY_WIDTH, KEY_HEIGHT, KeyType::ARROW, GLFW_KEY_UP);

    // -----------------------------
    // Navigation Keys (Repositioned)
    // -----------------------------
    // We want the nav keys to align vertically with the arrow keys (centered on the middle column)
    // and be moved up one key space to create a gap.
    // Previously: float navBlockX = arrowBlockX + (KEY_WIDTH + KEY_SPACING_X);
    // Now, move them left one additional key space:
    float navBlockX = arrowBlockX;  // moved left one key space
    float navBlockY2 = arrowUpY - 2 * (KEY_HEIGHT + KEY_SPACING_Y); // bottom nav row (moved up one extra row)
    float navBlockY1 = navBlockY2 - (KEY_HEIGHT + KEY_SPACING_Y);    // top nav row

    addKey("Ins", navBlockX, navBlockY1, KEY_WIDTH, KEY_HEIGHT, KeyType::NAVIGATION, GLFW_KEY_INSERT);
    addKey("Home", navBlockX + (KEY_WIDTH + KEY_SPACING_X), navBlockY1, KEY_WIDTH, KEY_HEIGHT, KeyType::NAVIGATION, GLFW_KEY_HOME);
    addKey("PgUp", navBlockX + 2 * (KEY_WIDTH + KEY_SPACING_X), navBlockY1, KEY_WIDTH, KEY_HEIGHT, KeyType::NAVIGATION, GLFW_KEY_PAGE_UP);
    addKey("Del", navBlockX, navBlockY2, KEY_WIDTH, KEY_HEIGHT, KeyType::NAVIGATION, GLFW_KEY_DELETE);
    addKey("End", navBlockX + (KEY_WIDTH + KEY_SPACING_X), navBlockY2, KEY_WIDTH, KEY_HEIGHT, KeyType::NAVIGATION, GLFW_KEY_END);
    addKey("PgDn", navBlockX + 2 * (KEY_WIDTH + KEY_SPACING_X), navBlockY2, KEY_WIDTH, KEY_HEIGHT, KeyType::NAVIGATION, GLFW_KEY_PAGE_DOWN);
}

// -------------------------
// GLFW Key Callback
// -------------------------
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (glfwKeyToIndex.find(key) != glfwKeyToIndex.end()) {
        int index = glfwKeyToIndex[key];
        if (action == GLFW_PRESS) {
            keyboardKeys[index].isPressed = true;
            std::thread([]() {
                std::string command = std::string("chuck ") + TEMP_CHUCK_FILENAME;
                system(command.c_str());
                }).detach();
        }
        else if (action == GLFW_RELEASE) {
            keyboardKeys[index].isPressed = false;
        }
    }
}

// -------------------------
// GLFW Mouse Button Callback
// -------------------------
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_leftMouseDown = true;
            // Check which key is under the mouse and trigger it
            for (auto& k : keyboardKeys) {
                if (xpos >= k.pos.x && xpos <= k.pos.x + k.size.x &&
                    ypos >= k.pos.y && ypos <= k.pos.y + k.size.y) {
                    k.isPressed = true;
                    std::thread([]() {
                        std::string command = std::string("chuck ") + TEMP_CHUCK_FILENAME;
                        system(command.c_str());
                        }).detach();
                    break;
                }
            }
        }
        else if (action == GLFW_RELEASE) {
            g_leftMouseDown = false;
            // Release all keys when left button is released
            for (auto& k : keyboardKeys) {
                k.isPressed = false;
            }
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            // Right click toggles keycap removal for the key under the mouse
            for (auto& k : keyboardKeys) {
                if (xpos >= k.pos.x && xpos <= k.pos.x + k.size.x &&
                    ypos >= k.pos.y && ypos <= k.pos.y + k.size.y) {
                    k.keycapRemoved = !k.keycapRemoved;
                    break;
                }
            }
        }
    }
}

// -------------------------
// GLFW Cursor Position Callback (for drag functionality)
// -------------------------
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (g_leftMouseDown) {
        // When dragging, mark as pressed the key currently under the mouse
        for (auto& k : keyboardKeys) {
            if (xpos >= k.pos.x && xpos <= k.pos.x + k.size.x &&
                ypos >= k.pos.y && ypos <= k.pos.y + k.size.y) {
                if (!k.isPressed) {
                    k.isPressed = true;
                    std::thread([]() {
                        std::string command = std::string("chuck ") + TEMP_CHUCK_FILENAME;
                        system(command.c_str());
                        }).detach();
                }
            }
            else {
                k.isPressed = false;
            }
        }
    }
}

// -------------------------
// Write Embedded ChucK Code to a Temporary File
// -------------------------
void initChucK() {
    std::ofstream out(TEMP_CHUCK_FILENAME);
    if (!out) {
        std::cerr << "Error: Could not create temporary ChucK file.\n";
        return;
    }
    out << embeddedChucKCode;
    out.close();
}

// -------------------------
// Main
// -------------------------
int main() {
    if (!glfwInit()) {
        std::cerr << "Error: Failed to initialize GLFW\n";
        return -1;
    }

    // Create a fullscreen window on the primary monitor
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primary);
    int windowWidth = mode->width;
    int windowHeight = mode->height;
    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "Advanced 3D Keyboard Simulator", primary, nullptr);
    if (!window) {
        std::cerr << "Error: Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // Set up callbacks for keyboard and mouse
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);

    // Set up orthographic projection based on window dimensions
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, windowWidth, windowHeight, 0, -100, 100);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Calculate main keyboard block dimensions and position (centered)
    float mainBlockWidth = 948.0f; // based on widest row (Row 2)
    float mainBlockHeight = 6 * KEY_HEIGHT + 5 * KEY_SPACING_Y; // 6 rows total
    g_mainStartX = (windowWidth - mainBlockWidth) / 2.0f;
    g_mainStartY = (windowHeight - mainBlockHeight) / 2.0f;

    initKeyboardLayout();
    g_lastFrameTime = glfwGetTime();
    initChucK();

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - g_lastFrameTime);
        g_lastFrameTime = currentTime;

        // Background color: teal
        glClearColor(0.0f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        // Update & draw all keys
        for (auto& k : keyboardKeys) {
            updateKeyAnimation(k, deltaTime);
            drawKeyMerged(k);
            if (!k.keycapRemoved) {
                float shiftLeft = 10.0f * k.pressAnim;
                float shiftUp = 10.0f * k.pressAnim;
                float labelX = k.pos.x + (k.size.x * 0.5f) - 8.0f - shiftLeft;
                if (k.label == "Backspace")
                    labelX -= 10.0f; // Extra left shift for Backspace label
                float labelY = k.pos.y + (k.size.y * 0.5f) - 8.0f - shiftUp;
                renderText(labelX, labelY, k.label.c_str());
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
