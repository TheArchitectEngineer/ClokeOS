/* ClokeOS: Darlings OpenGL.h bringt gl.h und glext.h mit, Apples nicht. Darlings Grafik-Frameworks
 * rufen gl*-Funktionen auf, ohne gl.h selbst einzubinden. */
#define GL_SILENCE_DEPRECATION 1
#include_next <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
