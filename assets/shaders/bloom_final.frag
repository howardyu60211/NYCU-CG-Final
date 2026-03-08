#version 330 core
out vec4 FragColor;
  
in vec2 TexCoords;

uniform sampler2D scene;
uniform sampler2D bloomBlur;
uniform float exposure;

void main()
{             
    const float gamma = 2.2;
    vec3 hdrColor = texture(scene, TexCoords).rgb;      
    vec3 bloomColor = texture(bloomBlur, TexCoords).rgb;
    
    // Additive blending
    hdrColor += bloomColor; // additive blending

    // ACES Tone Mapping
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    
    // Use exposure if needed, but ACES usually handles HDR range well.
    // Let's multiply by exposure (default 1.0) before simple ACES? 
    // Or just pass into ACES. ACES expects input ~1 for "normal" brightness, >1 for bright.
    
    vec3 mapped = clamp((hdrColor * (a * hdrColor + b)) / (hdrColor * (c * hdrColor + d) + e), 0.0, 1.0);
    
    // Gamma correction       
    mapped = pow(mapped, vec3(1.0 / gamma));
    
    FragColor = vec4(mapped, 1.0);
}
