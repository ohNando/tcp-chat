cmake_minimum_required(VERSION 3.20)
project(tcp_chat_desktop LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---- Dependencies ---------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

# Dear ImGui is pinned to a release tag and built from source.
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.9b
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# stb_image decodes wallpaper images (header only; stb has no release tags,
# so a commit is pinned).
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        6b5799bae8a1fe3792691ef4b72ae26434c3e799
)
FetchContent_MakeAvailable(stb)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# 32-bit characters: emoji live above U+FFFF.
target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
target_link_libraries(imgui PUBLIC PkgConfig::GLFW OpenGL::GL)

# Fonts, so the desktop app renders text exactly like the web client does on
# Linux: Noto Sans in the weights the web CSS uses, Noto Sans Mono for codes and
# Noto Color Emoji (the bitmap "CBDT" build, drawn by src/emoji_font.cpp).
# All SIL OFL 1.1, pinned by commit and checked by hash. Optional: without
# them (offline build) the app falls back to system fonts.
set(FONT_DIR ${CMAKE_BINARY_DIR}/fonts)
function(fetch_font url file sha256)
  if(NOT EXISTS ${FONT_DIR}/${file})
    file(DOWNLOAD ${url} ${FONT_DIR}/${file} EXPECTED_HASH SHA256=${sha256} STATUS st)
    list(GET st 0 rc)
    if(NOT rc EQUAL 0)
      file(REMOVE ${FONT_DIR}/${file})
      message(WARNING "Could not download ${file}; using system fonts instead")
    endif()
  endif()
endfunction()
set(NOTO https://raw.githubusercontent.com/notofonts/notofonts.github.io/d14cc173a70dc57962ed2d4ad278fc9df83bf9f3/fonts)
fetch_font(${NOTO}/NotoSans/hinted/ttf/NotoSans-Regular.ttf NotoSans-Regular.ttf
           478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823)
fetch_font(${NOTO}/NotoSans/hinted/ttf/NotoSans-Medium.ttf NotoSans-Medium.ttf
           635d93d1131d791f2576de90b3bb0f7cdf61929906e8420a61b5f7f8e76420bb)
fetch_font(${NOTO}/NotoSans/hinted/ttf/NotoSans-SemiBold.ttf NotoSans-SemiBold.ttf
           a4e91fd530ac2b4ef5367240144ff37d7d65d66cf76f2e9a2187b93c676f92d0)
fetch_font(${NOTO}/NotoSans/hinted/ttf/NotoSans-Bold.ttf NotoSans-Bold.ttf
           1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913)
fetch_font(${NOTO}/NotoSans/hinted/ttf/NotoSans-Italic.ttf NotoSans-Italic.ttf
           467e3f89eeca4108bb8710a2b9e0cf2281ac56d5b0609211a83776d0505eecb5)
fetch_font(${NOTO}/NotoSansMono/hinted/ttf/NotoSansMono-Regular.ttf NotoSansMono-Regular.ttf
           65b5e2b2c4a1fba9ae8be1f026cb35b03dcb8886d9b2a4147054fde12f7e767d)
fetch_font(https://raw.githubusercontent.com/googlefonts/noto-emoji/8998f5dd683424a73e2314a8c1f1e359c19e8742/fonts/NotoColorEmoji.ttf
           NotoColorEmoji.ttf 72a635cb3d2f3524c51620cdde406b217204e8a6a06c6a096ff8ed4b5fd6e27b)
if(NOT EXISTS ${FONT_DIR}/OFL.txt)
  file(DOWNLOAD https://raw.githubusercontent.com/google/fonts/06bfe0d62a98375cb09c942fa18e68bd32cff057/ofl/notosans/OFL.txt
       ${FONT_DIR}/OFL.txt)
endif()

# ---- Shared C core --------------------------------------------------------
# Protocol and crypto modules are compiled straight from the backend, so the
# desktop client can never drift from the server's wire format.

set(BACKEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Backend)
add_library(chat_core STATIC
  ${BACKEND_DIR}/src/bytebuf.c
  ${BACKEND_DIR}/src/frame.c
  ${BACKEND_DIR}/src/framer.c
  ${BACKEND_DIR}/src/message.c
  ${BACKEND_DIR}/src/validate.c
  ${BACKEND_DIR}/src/identity.c
  ${BACKEND_DIR}/src/handshake.c
  ${BACKEND_DIR}/src/secure.c
  ${BACKEND_DIR}/src/config.c
  ${BACKEND_DIR}/src/known_servers.c
  ${BACKEND_DIR}/src/dm.c
)
target_include_directories(chat_core PUBLIC ${BACKEND_DIR}/include)
target_compile_options(chat_core PRIVATE -Wall -Wextra)
target_link_libraries(chat_core PUBLIC PkgConfig::SODIUM)

# ---- Application ----------------------------------------------------------

# Networking without any UI, so it can be tested headless.
add_library(chat_client STATIC src/chat_client.cpp)
target_include_directories(chat_client PUBLIC src)
target_compile_options(chat_client PRIVATE -Wall -Wextra)
target_link_libraries(chat_client PUBLIC chat_core)

add_executable(tcp-chat-desktop
  src/main.cpp
  src/app.cpp
  src/settings.cpp
  src/theme.cpp
  src/chat_view.cpp
  src/settings_ui.cpp
  src/stb_image_impl.cpp
  src/wallpaper.cpp
  src/ui.cpp
  src/emoji_font.cpp
)
target_include_directories(tcp-chat-desktop PRIVATE ${stb_SOURCE_DIR})
target_compile_options(tcp-chat-desktop PRIVATE -Wall -Wextra)
target_compile_definitions(tcp-chat-desktop PRIVATE FONT_DIR="${FONT_DIR}")
target_link_libraries(tcp-chat-desktop PRIVATE imgui chat_client Threads::Threads)

# Headless test against a running server: client_smoke <host> <port>
add_executable(client_smoke tests/client_smoke.cpp)
target_link_libraries(client_smoke PRIVATE chat_client)
