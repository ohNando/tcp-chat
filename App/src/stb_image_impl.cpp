// The one translation unit that compiles stb_image's implementation.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_FAILURE_USERMSG
#include "stb_image.h"
