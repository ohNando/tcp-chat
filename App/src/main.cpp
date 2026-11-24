// TCP Chat desktop client: Dear ImGui on GLFW + OpenGL 3.

#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cstring>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include "core.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app.h"
#include "ui.h"

static void glfw_error(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// Reads the back buffer and writes a binary PPM (convert with ImageMagick).
static bool save_ppm(const char* path, int w, int h) {
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) // OpenGL rows are bottom-up
        std::fwrite(px.data() + static_cast<size_t>(y) * w * 3, 1, static_cast<size_t>(w) * 3, f);
    std::fclose(f);
    return true;
}

static void usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s [--connect host port name] [--say text] [--trust] [--settings] [--emoji] [--dm name]\n"
                 "          [--theme id] [--wallpaper id|image]\n"
                 "          [--size WxH] [--screenshot out.ppm --wait seconds]\n"
                 "  --connect     connect on start-up\n"
                 "  --say         send a message once joined\n"
                 "  --trust       accept an unknown server key without asking\n"
                 "  --settings    open the settings window on start-up\n"
                 "  --emoji       open the emoji picker once joined (for screenshots)\n"
                 "  --dm          open a private chat with name once joined (for screenshots)\n"
                 "  --theme       system, midnight, daylight, ocean, forest, sunset or lavender\n"
                 "  --wallpaper   none, dots, grid, waves, bubbles, aurora, dusk or an image file\n"
                 "                (--theme and --wallpaper apply to this run only)\n"
                 "  --size        window size, e.g. 1000x680 (the default)\n"
                 "  --screenshot  render in a hidden window for --wait seconds, save, exit\n",
                 prog);
}

int main(int argc, char** argv) {
    AutoRun autorun;
    const char* screenshot = nullptr;
    double wait = 1.0;
    int win_w = 1000, win_h = 680;

    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--connect") && i + 3 < argc) {
            autorun.host = argv[++i];
            autorun.port = std::atoi(argv[++i]);
            autorun.name = argv[++i];
            autorun.connect = true;
        } else if (!std::strcmp(argv[i], "--say") && i + 1 < argc) {
            autorun.say = argv[++i];
        } else if (!std::strcmp(argv[i], "--trust")) {
            autorun.trust = true;
        } else if (!std::strcmp(argv[i], "--settings")) {
            autorun.open_settings = true;
        } else if (!std::strcmp(argv[i], "--theme") && i + 1 < argc) {
            autorun.theme = argv[++i];
        } else if (!std::strcmp(argv[i], "--wallpaper") && i + 1 < argc) {
            autorun.wallpaper = argv[++i];
        } else if (!std::strcmp(argv[i], "--dm") && i + 1 < argc) {
            autorun.dm = argv[++i]; // for screenshots
        } else if (!std::strcmp(argv[i], "--emoji")) {
            autorun.open_emoji = true; // for screenshots
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &win_w, &win_h) != 2 || win_w < 320 || win_h < 240) {
                usage(argv[0]);
                return 2;
            }
        } else if (!std::strcmp(argv[i], "--wait") && i + 1 < argc) {
            wait = std::atof(argv[++i]);
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    // Times and dates follow the system locale, like the browser's; numbers
    // stay in the C locale (the settings file writes "0.25", not "0,25").
    std::setlocale(LC_TIME, "");

    if (sodium_init() < 0) {
        std::fprintf(stderr, "libsodium initialization failed\n");
        return 1;
    }
    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    if (screenshot) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(win_w, win_h, "TCP Chat", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // no imgui.ini next to the binary
    ImGui::StyleColorsDark();
    ui::load_fonts();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app(autorun);
    // Time based: a hidden window may not be throttled by vsync.
    const double screenshot_at = glfwGetTime() + wait;
    std::string shown_title = "TCP Chat";

    while (!glfwWindowShouldClose(window)) {
        // Idle: sleep until input arrives, waking every 50 ms to check the
        // socket, instead of redrawing at full frame rate. Keeps CPU near 0.
        if (screenshot)
            glfwPollEvents();
        else
            glfwWaitEventsTimeout(0.05);
        app.setFocused(glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE);
        app.poll();
        std::string title = app.windowTitle();
        if (title != shown_title) {
            glfwSetWindowTitle(window, title.c_str());
            shown_title = title;
        }
        if (app.takeAttentionRequest()) glfwRequestWindowAttention(window);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.draw();
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        ImVec4 bg = app.clearColor();
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (screenshot && glfwGetTime() >= screenshot_at) {
            int rc = save_ppm(screenshot, w, h) ? 0 : 1;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            if (rc) std::fprintf(stderr, "cannot write %s\n", screenshot);
        }
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
