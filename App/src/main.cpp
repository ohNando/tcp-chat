// TCP Chat desktop client: Dear ImGui on GLFW + OpenGL 3.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include "core.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app.h"

static void glfw_error(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// ImGui's built-in font only covers ASCII. Use a system font so names like
// "Şükrü" render; ImGui 1.92 loads glyphs on demand, no ranges needed.
static void load_font(float size) {
    static const char* candidates[] = {
        "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
    };
    ImGuiIO& io = ImGui::GetIO();
    for (const char* path : candidates) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, size)) return;
        }
    }
    std::fprintf(stderr, "warning: no system font found, non-ASCII text may not render\n");
    io.Fonts->AddFontDefault();
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
                 "Usage: %s [--connect host port name] [--say text] [--trust] [--settings]\n"
                 "          [--screenshot out.ppm --wait seconds]\n"
                 "  --connect     connect on start-up\n"
                 "  --say         send a message once joined\n"
                 "  --trust       accept an unknown server key without asking\n"
                 "  --settings    open the settings window on start-up\n"
                 "  --screenshot  render in a hidden window for --wait seconds, save, exit\n",
                 prog);
}

int main(int argc, char** argv) {
    AutoRun autorun;
    const char* screenshot = nullptr;
    double wait = 1.0;

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
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (!std::strcmp(argv[i], "--wait") && i + 1 < argc) {
            wait = std::atof(argv[++i]);
        } else {
            usage(argv[0]);
            return 2;
        }
    }

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

    GLFWwindow* window = glfwCreateWindow(1000, 680, "TCP Chat", nullptr, nullptr);
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
    load_font(18.0f);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app(autorun);
    // Time based: a hidden window may not be throttled by vsync.
    const double screenshot_at = glfwGetTime() + wait;

    while (!glfwWindowShouldClose(window)) {
        // Idle: sleep until input arrives, waking every 50 ms to check the
        // socket, instead of redrawing at full frame rate. Keeps CPU near 0.
        if (screenshot)
            glfwPollEvents();
        else
            glfwWaitEventsTimeout(0.05);
        app.poll();

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
