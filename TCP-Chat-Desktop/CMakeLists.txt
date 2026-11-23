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
target_link_libraries(imgui PUBLIC PkgConfig::GLFW OpenGL::GL)

# ---- Shared C core --------------------------------------------------------
# Protocol and crypto modules are compiled straight from the backend, so the
# desktop client can never drift from the server's wire format.

set(BACKEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../TCP-Chat-Backend)
add_library(chat_core STATIC
  ${BACKEND_DIR}/src/bytebuf.c
  ${BACKEND_DIR}/src/frame.c
  ${BACKEND_DIR}/src/framer.c
  ${BACKEND_DIR}/src/message.c
  ${BACKEND_DIR}/src/validate.c
  ${BACKEND_DIR}/src/identity.c
  ${BACKEND_DIR}/src/handshake.c
  ${BACKEND_DIR}/src/secure.c
  ${BACKEND_DIR}/src/known_servers.c
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
)
target_compile_options(tcp-chat-desktop PRIVATE -Wall -Wextra)
target_link_libraries(tcp-chat-desktop PRIVATE imgui chat_client)

# Headless test against a running server: client_smoke <host> <port>
add_executable(client_smoke tests/client_smoke.cpp)
target_link_libraries(client_smoke PRIVATE chat_client)
