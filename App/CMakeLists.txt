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

# Monochrome Noto Emoji (SIL OFL 1.1), merged into the UI font so emoji
# render. Color emoji fonts need FreeType, which ImGui does not use here.
# Optional: without it (offline build) emoji show as boxes.
set(EMOJI_URL https://raw.githubusercontent.com/google/fonts/06bfe0d62a98375cb09c942fa18e68bd32cff057/ofl/notoemoji)
set(EMOJI_FONT ${CMAKE_BINARY_DIR}/fonts/NotoEmoji.ttf)
if(NOT EXISTS ${EMOJI_FONT})
  file(DOWNLOAD ${EMOJI_URL}/NotoEmoji%5Bwght%5D.ttf ${EMOJI_FONT}
       EXPECTED_HASH SHA256=de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551
       STATUS emoji_status)
  list(GET emoji_status 0 emoji_rc)
  if(emoji_rc EQUAL 0)
    file(DOWNLOAD ${EMOJI_URL}/OFL.txt ${CMAKE_BINARY_DIR}/fonts/NotoEmoji-OFL.txt)
  else()
    file(REMOVE ${EMOJI_FONT})
    message(WARNING "Could not download Noto Emoji; emoji will not render")
  endif()
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
)
target_include_directories(tcp-chat-desktop PRIVATE ${stb_SOURCE_DIR})
target_compile_options(tcp-chat-desktop PRIVATE -Wall -Wextra)
target_compile_definitions(tcp-chat-desktop PRIVATE EMOJI_FONT_PATH="${EMOJI_FONT}")
target_link_libraries(tcp-chat-desktop PRIVATE imgui chat_client Threads::Threads)

# Headless test against a running server: client_smoke <host> <port>
add_executable(client_smoke tests/client_smoke.cpp)
target_link_libraries(client_smoke PRIVATE chat_client)
