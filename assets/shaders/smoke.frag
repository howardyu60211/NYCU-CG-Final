#version 330 core
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

in vec2 TexCoord;
in float Alpha;

uniform sampler2D smokeTexture;

void main()
{
    vec4 texColor = texture(smokeTexture, TexCoord);
    
    vec2 center = vec2(0.5, 0.5);
    float dist = length(TexCoord - center);
    
    float circleAlpha = 1.0 - smoothstep(0.3, 0.5, dist);

    vec4 rawColor = texColor * Alpha * circleAlpha * vec4(0.8, 0.8, 0.8, 1.0);
    vec3 color = rawColor.rgb;

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
    FragColor = vec4(result, rawColor.a);
    
    // Check whether fragment output is higher than threshold, if so output as brightness color
    float brightness = dot(result, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = vec4(result, 1.0);
    else
        BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
    
    // if (FragColor.r < 0.05) discard; 
}