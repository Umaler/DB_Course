#version 460

layout(location = 0) in vec2 aPos;

uniform double xMult;
uniform double xShift;
uniform double yMult;
uniform double yShift;

void main() {
   gl_Position = vec4(0, 0, 0, 1);
   gl_Position = vec4(aPos.x * xMult + xShift, aPos.y * yMult + yShift, 0.0, 1.0);
}
