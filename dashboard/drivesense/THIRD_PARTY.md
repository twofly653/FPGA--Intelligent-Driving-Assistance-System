# Third-party assets and licenses

## Car model

The detailed car is a modified copy of **Car Concept** in the Khronos Group
[glTF Sample Assets repository](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CarConcept).
The repository credits **Eric Chadwick** for the model and textures; the work
started from a public-domain model by **Unity Fan**. The Car Concept model and
textures are licensed under [Creative Commons Attribution 4.0](https://creativecommons.org/licenses/by/4.0/).

Changes in this demo: the Khronos and 3D Commerce logo artwork and the
corresponding plate/emblem meshes were removed; marked tire textures were
replaced; the body paint and glass appearance were adjusted in the renderer;
the model was embedded for offline viewing. The original repository's
[license notice](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/CarConcept/LICENSE.md)
excludes trademark rights. This demo is not endorsed by the Khronos Group.

## Renderer

The demo includes **three.js r146** and its GLTFLoader and RoomEnvironment
examples, licensed under the MIT License. The full notice is included in
`vendor/THREE_LICENSE.txt`. Source: <https://github.com/mrdoob/three.js>.

2026-10-17 integration changes in this copy: added local UART/SSE input and
source/validity labels; corrected pose axis/sign mapping in the renderer.
The detailed model and its original attribution remain unchanged.
