#version 330 core
in vec3 TexCoords;
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;
uniform samplerCube skybox;

// TODO#3-2: fragment shader
// TODO#3-2: fragment shader
void main() {
    vec3 color = texture(skybox, TexCoords).rgb;

    // ACES Tone Mapping (REMOVED)
    // float a = 2.51f;
    // float b = 0.03f;
    // float c = 2.43f;
    // float d = 0.59f;
    // float e = 0.14f;
    // vec3 mapped = clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);

    // Gamma Correction (REMOVED)
    // mapped = pow(mapped, vec3(1.0 / 2.2));

    vec3 result = color;
    FragColor = vec4(result, 1.0);

    // Check whether fragment output is higher than threshold, if so output as brightness color
    float brightness = dot(result, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = vec4(result, 1.0);
    else
        BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}