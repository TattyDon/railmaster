# Third-party code

Single-header libraries the client uses to load art. Each is used unmodified.

| Library | Version | Licence | Used for |
|---|---|---|---|
| [cgltf](https://github.com/jkuhlmann/cgltf) | 1.14 | MIT (`cgltf/LICENSE`) | Reading glTF 2.0 models (`.glb`, `.gltf`) |
| [stb_image](https://github.com/nothings/stb) | 2.30 | Public domain or MIT (end of `stb/stb_image.h`) | Reading PNG and JPEG textures |

Their implementations are compiled in `src/client/third_party_impl.cpp`,
with warnings off; the headers are included as system headers.
