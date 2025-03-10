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
#include <chrono>
#include <thread>  // for std::thread

// Include stb_easy_font header (ensure "stb_easy_font.h" is in your source folder)
#include "stb_easy_font.h"

// -------------------------
// Constants & Global Settings
// -------------------------
constexpr float KEY_WIDTH = 60.0f;
constexpr float KEY_HEIGHT = 60.0f;
constexpr float KEY_SPACING = 10.0f;
constexpr float KEY_DEPTH = 15.0f; // Slightly deeper for a more dramatic sinking effect
constexpr double PRESS_FEEDBACK_DURATION = 0.15; // seconds for press animation

// -------------------------
// Embedded ChucK Code: Cherry MX Blue Switch Simulation (Clicky)
// -------------------------
const char* embeddedChucKCode = R"(
/*
Cherry MX Blue Switch Simulation (Clicky)
*/
fun void blueSwitch() {
    // The click: a very short, high-frequency noise burst.
    Noise click => HPF hpf => ADSR clickEnv => dac;
    0.4 => click.gain;
    3000 => hpf.freq;   // High-pass to emphasize high frequencies
    0.5 => hpf.Q;
    0.2::ms => dur attack;
    1::ms => dur decay;
    0 => float sustain;
    0.5::ms => dur release;
    clickEnv.set(attack, decay, sustain, release);
    
    // The tactile bump: a brief sine tone.
    SinOsc bump => ADSR bumpEnv => dac;
    250 => bump.freq;
    0.2 => bump.gain;
    0.5::ms => dur bAttack;
    1::ms => dur bDecay;
    0 => float bSustain;
    1::ms => dur bRelease;
    bumpEnv.set(bAttack, bDecay, bSustain, bRelease);
    
    // Trigger the click first.
    clickEnv.keyOn();
    (attack + decay) => now;
    clickEnv.keyOff();
    release => now;
    
    // A very short delay before the tactile bump.
    1::ms => now;
    
    // Trigger the tactile bump.
    bumpEnv.keyOn();
    (bAttack + bDecay) => now;
    bumpEnv.keyOff();
    bRelease => now;
}

blueSwitch();
)";

// Temporary filename for the embedded ChucK code.
const char* TEMP_CHUCK_FILENAME = "temp_chuck.ck";

// -------------------------
// Key Structure
// -------------------------
struct Key {
    std::string label;      // e.g., "Q", "F1", "1"
    glm::vec2 pos;          // Top-left position in window coordinates
    glm::vec2 size;         // Width and height
    float pressAnim = 0.0f; // 0.0 (up) to 0.5 (fully pressed)
    bool isPressed = false; // True while key is physically pressed
};

std::vector<Key> keyboardKeys;
// Map GLFW key codes to indices in keyboardKeys.
std::map<int, int> glfwKeyToIndex;

// For animation timing.
double g_lastFrameTime = 0.0;

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
// Function: playKeySound
// Spawns a new thread that launches ChucK on the temporary file,
// so that overlapping key presses generate independent impulses.
// -------------------------
void playKeySound() {
    std::string command = std::string("chuck ") + TEMP_CHUCK_FILENAME;
    // Call system() inside a new thread so that it doesn't block.
    system(command.c_str());
}

// -------------------------
// Function: drawKey3D
// Draws a skeuomorphic 3D key that "sinks" when pressed.
// It shifts left, sinks (offset in z), and compresses depth.
// -------------------------
void drawKey3D(float bx, float by, float bw, float bh, float depth, float pressAnim, bool darkTheme = false) {
    // Using pressAnim in [0, 0.5], where 0.5 means fully pressed.
    float shiftLeft = 10.0f * pressAnim;               // SHIFT: move left by up to 10 pixels
    float pressOffsetZ = depth * pressAnim;              // SINK: front face offset into the screen
    float newDepth = depth * (1.0f - 0.5f * pressAnim);  // COMPRESS: reduce depth by up to 50%

    // Compute the adjusted x position for the front face.
    float x = bx - shiftLeft;
    float y = by;

    if (darkTheme) {
        // DARK THEME COLORS
        float frontColor = 0.3f - 0.1f * (pressAnim * 2.0f);
        glColor3f(frontColor, frontColor, frontColor);
        glBegin(GL_QUADS);
        glVertex3f(x, y, -pressOffsetZ);
        glVertex3f(x + bw, y, -pressOffsetZ);
        glVertex3f(x + bw, y + bh, -pressOffsetZ);
        glVertex3f(x, y + bh, -pressOffsetZ);
        glEnd();

        glColor3f(0.4f, 0.4f, 0.4f); // TOP FACE
        glBegin(GL_QUADS);
        glVertex3f(x, y, -pressOffsetZ);
        glVertex3f(x + bw, y, -pressOffsetZ);
        glVertex3f(x + bw - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
        glVertex3f(x - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
        glEnd();

        glColor3f(0.25f, 0.25f, 0.25f); // RIGHT FACE
        glBegin(GL_QUADS);
        glVertex3f(x + bw, y, -pressOffsetZ);
        glVertex3f(x + bw, y + bh, -pressOffsetZ);
        glVertex3f(x + bw - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
        glVertex3f(x + bw - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
        glEnd();

        glColor3f(0.35f, 0.35f, 0.35f); // BOTTOM FACE
        glBegin(GL_QUADS);
        glVertex3f(x, y + bh, -pressOffsetZ);
        glVertex3f(x + bw, y + bh, -pressOffsetZ);
        glVertex3f(x + bw - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
        glVertex3f(x - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
        glEnd();

        glColor3f(0.28f, 0.28f, 0.28f); // LEFT FACE
        glBegin(GL_QUADS);
        glVertex3f(x, y, -pressOffsetZ);
        glVertex3f(x, y + bh, -pressOffsetZ);
        glVertex3f(x - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
        glVertex3f(x - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
        glEnd();
    }
    else {
        // LIGHT THEME COLORS
        float frontColor = 0.8f - 0.2f * (pressAnim * 2.0f);
        glColor3f(frontColor, frontColor, frontColor);
        glBegin(GL_QUADS);
        glVertex3f(x, y, -pressOffsetZ);
        glVertex3f(x + bw, y, -pressOffsetZ);
        glVertex3f(x + bw, y + bh, -pressOffsetZ);
        glVertex3f(x, y + bh, -pressOffsetZ);
        glEnd();

        glColor3f(0.9f, 0.9f, 0.9f); // TOP FACE
        glBegin(GL_QUADS);
        glVertex3f(x, y, -pressOffsetZ);
        glVertex3f(x + bw, y, -pressOffsetZ);
        glVertex3f(x + bw - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
        glVertex3f(x - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
        glEnd();

        glColor3f(0.6f, 0.6f, 0.6f); // RIGHT FACE
        glBegin(GL_QUADS);
        glVertex3f(x + bw, y, -pressOffsetZ);
        glVertex3f(x + bw, y + bh, -pressOffsetZ);
        glVertex3f(x + bw - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
        glVertex3f(x + bw - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
        glEnd();

        glColor3f(0.7f, 0.7f, 0.7f); // BOTTOM FACE
        glBegin(GL_QUADS);
        glVertex3f(x, y + bh, -pressOffsetZ);
        glVertex3f(x + bw, y + bh, -pressOffsetZ);
        glVertex3f(x + bw - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
        glVertex3f(x - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
        glEnd();

        glColor3f(0.65f, 0.65f, 0.65f); // LEFT FACE
        glBegin(GL_QUADS);
        glVertex3f(x, y, -pressOffsetZ);
        glVertex3f(x, y + bh, -pressOffsetZ);
        glVertex3f(x - newDepth, y + bh - newDepth, -(pressOffsetZ + newDepth));
        glVertex3f(x - newDepth, y - newDepth, -(pressOffsetZ + newDepth));
        glEnd();
    }
}

// -------------------------
// Function: renderText
// Renders a key label using stb_easy_font.
// -------------------------
void renderText(float x, float y, const char* text, bool darkTheme = false) {
    char buffer[99999];
    int num_quads = stb_easy_font_print(x, y, const_cast<char*>(text), nullptr, buffer, sizeof(buffer));

    glDisable(GL_DEPTH_TEST);
    if (darkTheme)
        glColor3f(0.9f, 0.9f, 0.9f);
    else
        glColor3f(0.0f, 0.0f, 0.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 16, buffer);
    glDrawArrays(GL_QUADS, 0, num_quads * 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glEnable(GL_DEPTH_TEST);
}

// -------------------------
// Function: updateKeyAnimation
// Smoothly transitions a key’s pressAnim value based on its isPressed state.
// -------------------------
void updateKeyAnimation(Key& key, float deltaTime) {
    float animSpeed = 0.5f / static_cast<float>(PRESS_FEEDBACK_DURATION);
    float target = key.isPressed ? 0.5f : 0.0f;
    if (key.pressAnim < target) {
        key.pressAnim += animSpeed * deltaTime;
        if (key.pressAnim > target)
            key.pressAnim = target;
    }
    else if (key.pressAnim > target) {
        key.pressAnim -= animSpeed * deltaTime;
        if (key.pressAnim < target)
            key.pressAnim = target;
    }
}

// -------------------------
// Function: initKeyboard
// Creates a keyboard layout with 5 rows arranged top-to-bottom:
// Row 0: Function keys (F1-F12)
// Row 1: Number keys (1-0)
// Row 2: QWERTYUIOP
// Row 3: ASDFGHJKL
// Row 4: ZXCVBNM
// -------------------------
void initKeyboard(int screenWidth, int screenHeight) {
    keyboardKeys.clear();
    glfwKeyToIndex.clear();

    float startX, startY;

    // Row 0: Function keys F1-F12
    startX = KEY_SPACING;
    startY = KEY_SPACING; // Top row
    std::vector<std::string> funcKeys = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
    for (const auto& label : funcKeys) {
        Key key;
        key.label = label;
        key.pos = glm::vec2(startX, startY);
        key.size = glm::vec2(KEY_WIDTH, KEY_HEIGHT);
        keyboardKeys.push_back(key);
        // Map GLFW function keys: GLFW_KEY_F1 is guaranteed to be sequential.
        int glfwKey = GLFW_KEY_F1 + (&label - &funcKeys[0]); // Using pointer arithmetic on vector element
        glfwKeyToIndex[glfwKey] = keyboardKeys.size() - 1;
        startX += KEY_WIDTH + KEY_SPACING;
    }

    // Row 1: Number keys 1-0
    startX = KEY_SPACING;
    startY += KEY_HEIGHT + KEY_SPACING;
    std::string numRow = "1234567890";
    for (char c : numRow) {
        Key key;
        key.label = std::string(1, c);
        key.pos = glm::vec2(startX, startY);
        key.size = glm::vec2(KEY_WIDTH, KEY_HEIGHT);
        keyboardKeys.push_back(key);
        // Map GLFW number keys.
        int glfwKey = GLFW_KEY_1 + (c - '1');
        // Special handling for '0': GLFW_KEY_0 follows GLFW_KEY_9.
        if (c == '0')
            glfwKey = GLFW_KEY_0;
        glfwKeyToIndex[glfwKey] = keyboardKeys.size() - 1;
        startX += KEY_WIDTH + KEY_SPACING;
    }

    // Row 2: QWERTYUIOP
    startX = KEY_SPACING;
    startY += KEY_HEIGHT + KEY_SPACING;
    std::string row1 = "QWERTYUIOP";
    for (char c : row1) {
        Key key;
        key.label = std::string(1, c);
        key.pos = glm::vec2(startX, startY);
        key.size = glm::vec2(KEY_WIDTH, KEY_HEIGHT);
        keyboardKeys.push_back(key);
        int glfwKey = GLFW_KEY_A + (c - 'A'); // Map letters A-Z.
        glfwKeyToIndex[glfwKey] = keyboardKeys.size() - 1;
        startX += KEY_WIDTH + KEY_SPACING;
    }

    // Row 3: ASDFGHJKL
    startX = KEY_SPACING + (KEY_WIDTH + KEY_SPACING) / 2.0f;
    startY += KEY_HEIGHT + KEY_SPACING;
    std::string row2 = "ASDFGHJKL";
    for (char c : row2) {
        Key key;
        key.label = std::string(1, c);
        key.pos = glm::vec2(startX, startY);
        key.size = glm::vec2(KEY_WIDTH, KEY_HEIGHT);
        keyboardKeys.push_back(key);
        int glfwKey = GLFW_KEY_A + (c - 'A');
        glfwKeyToIndex[glfwKey] = keyboardKeys.size() - 1;
        startX += KEY_WIDTH + KEY_SPACING;
    }

    // Row 4: ZXCVBNM
    startX = KEY_SPACING + (KEY_WIDTH + KEY_SPACING);
    startY += KEY_HEIGHT + KEY_SPACING;
    std::string row3 = "ZXCVBNM";
    for (char c : row3) {
        Key key;
        key.label = std::string(1, c);
        key.pos = glm::vec2(startX, startY);
        key.size = glm::vec2(KEY_WIDTH, KEY_HEIGHT);
        keyboardKeys.push_back(key);
        int glfwKey = GLFW_KEY_A + (c - 'A');
        glfwKeyToIndex[glfwKey] = keyboardKeys.size() - 1;
        startX += KEY_WIDTH + KEY_SPACING;
    }
}

// -------------------------
// GLFW Key Callback
// When an A–Z, number, or function key is pressed or released, update its state and spawn a new thread for sound.
// -------------------------
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (glfwKeyToIndex.find(key) != glfwKeyToIndex.end()) {
        int index = glfwKeyToIndex[key];
        if (index < keyboardKeys.size()) {
            if (action == GLFW_PRESS) {
                keyboardKeys[index].isPressed = true;
                // Spawn a new thread so that overlapping key sounds can occur.
                std::thread(playKeySound).detach();
            }
            else if (action == GLFW_RELEASE) {
                keyboardKeys[index].isPressed = false;
            }
        }
    }
}

// -------------------------
// Main Function
// -------------------------
int main() {
    if (!glfwInit()) {
        std::cerr << "Error: Failed to initialize GLFW\n";
        return -1;
    }

    int screenWidth = 1280;
    int screenHeight = 720;
    GLFWwindow* window = glfwCreateWindow(screenWidth, screenHeight, "3D Keyboard Simulator", nullptr, nullptr);
    if (!window) {
        std::cerr << "Error: Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // Set key callback.
    glfwSetKeyCallback(window, keyCallback);

    // Set up orthographic projection with (0,0) at top-left.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, screenWidth, screenHeight, 0, -100, 100);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Initialize keyboard layout.
    initKeyboard(screenWidth, screenHeight);
    g_lastFrameTime = glfwGetTime();

    // Write embedded ChucK code to temporary file.
    initChucK();

    // Main loop.
    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - g_lastFrameTime);
        g_lastFrameTime = currentTime;

        // Clear the screen.
        glClearColor(0.933f, 0.933f, 0.933f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        // Update animation and draw each key.
        for (auto& key : keyboardKeys) {
            updateKeyAnimation(key, deltaTime);
            drawKey3D(key.pos.x, key.pos.y, key.size.x, key.size.y, KEY_DEPTH, key.pressAnim, false);
            // Render key label (roughly centered; adjust as needed).
            renderText(key.pos.x + 10, key.pos.y + key.size.y / 2 - 8, key.label.c_str(), false);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
