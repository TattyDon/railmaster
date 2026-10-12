// The single-header libraries' implementations, compiled once and apart
// from the client's own code (their headers are included as system
// headers, so the project's warning flags do not apply to them).
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>
