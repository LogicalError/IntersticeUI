#version 450
#extension GL_GOOGLE_include_directive : require

// Writes depth only: the pipeline masks the color out. Magenta, should it ever not.
layout (location = 0) out vec4 outFragColor;

void main()
{
    outFragColor = vec4(1, 0, 1, 1);
}
