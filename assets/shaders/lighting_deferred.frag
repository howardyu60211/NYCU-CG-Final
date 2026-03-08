#version 430 core
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

in vec2 TexCoords; // 來自全螢幕四邊形的 UV

// --- G-Buffer (替代原本的 in 變數與 Material struct) ---
uniform sampler2D gPosition;    // 對應 FragPos
uniform sampler2D gNormal;      // 對應 Normal
uniform sampler2D gAlbedoSpec;  // RGB=Diffuse, A=Metallic
uniform sampler2D gPBRParams;   // R=Roughness, G=AO

// --- 繼承原本的變數結構 ---
struct DirectionLight {
    int enable;
    vec3 direction;
    vec3 lightColor;
};

struct PointLight {
    int enable;
    vec3 position;  
    vec3 lightColor;
    float constant;
    float linear;
    float quadratic;
};

struct Spotlight {
    int enable;
    vec3 position; 
    vec3 direction;
    vec3 lightColor;
    float cutOff;
    float constant;
    float linear;
    float quadratic;      
}; 

// 光源 Uniforms (保持不變)
uniform DirectionLight dl;
#define NR_POINT_LIGHTS 6
uniform PointLight pointLights[NR_POINT_LIGHTS];
#define NR_SPOT_LIGHTS 32
uniform Spotlight spotLights[NR_SPOT_LIGHTS];

// 其他 Uniforms
uniform vec3 viewPos;
uniform samplerCube skybox;
uniform sampler2DShadow shadowMap;
uniform mat4 lightSpaceMatrix;
uniform float fogDensity;
uniform vec3 fogColor;

const float PI = 3.14159265359;

// --- PBR 函數 (保持完全不變) ---
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// --- 改良版 Shadow Calculation ---

// Random number generator
// Returns a random float between 0.0 and 1.0
float random(vec3 seed, int i){
    vec4 seed4 = vec4(seed,i);
    float dot_product = dot(seed4, vec4(12.9898,78.233,45.164,94.673));
    return fract(sin(dot_product) * 43758.5453);
}
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // 1. 透視除法
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // 解決超出邊界的問題
    if(projCoords.z > 1.0)
        return 0.0;

    // 2. Bias
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);

    // PCF (Percentage-Closer Filtering) 5x5 Kernel
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    
    for(int x = -2; x <= 2; ++x) {
        for(int y = -2; y <= 2; ++y) {
            shadow += texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, projCoords.z - bias));
        }
    }
    shadow /= 25.0; 

    return 1.0 - shadow;
}

// --- PBR 光照計算 (保持不變) ---
vec3 calculatePBRLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 F0, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 numerator    = NDF * G * F; 
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;       
    float NdotL = max(dot(N, L), 0.0);        
    return (kD * albedo / PI + specular) * radiance * NdotL;  
}

vec2 EnvBRDFApprox(float Roughness, float NoV) {
    vec4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
    vec4 c1 = vec4(1, 0.0425, 1.04, -0.04);
    vec4 r = Roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return AB;
}

// [NEW] SSR Matrices
uniform mat4 view;
uniform mat4 projection;

// SSR Function (World Space)
vec3 SSR(vec3 startPos, vec3 dir) {
    vec3 step = dir * 0.1; // Step size
    vec3 currentPos = startPos + step; // Start slightly offset
    
    // Max Steps for Ray Marching (Performance vs Distance)
    int maxSteps = 32; // [Improved] More steps for better reflection finding
    
    for (int i = 0; i < maxSteps; i++) {
        currentPos += step;
        
        // Convert to Screen Space
        vec4 viewPos = view * vec4(currentPos, 1.0);
        vec4 projPos = projection * viewPos;
        vec3 screenPos = projPos.xyz / projPos.w;
        screenPos = screenPos * 0.5 + 0.5; // [-1,1] -> [0,1]
        
        // Check Bounds
        if (screenPos.x < 0.0 || screenPos.x > 1.0 || screenPos.y < 0.0 || screenPos.y > 1.0) {
            continue; // Out of screen
        }
        
        // Sample Depth from G-Buffer (World Position)
        vec3 gBufferPos = texture(gPosition, screenPos.xy).rgb;
        
        // Check for hit
        // If current ray point is BEHIND (farther from camera) the surface at that pixel
        // We compare distances from camera or better yet, compare world Y or similar.
        // Or simpler: compare View Space depth.
        
        // Let's use View Space comparison
        vec4 gBufferViewPos = view * vec4(gBufferPos, 1.0);
        
        // If ray depth > surface depth (+ bias), we hit something
        // Note: In View Space, Z is negative (looking down -Z). 
        // Larger (less negative) Z means closer to camera.
        // Wait, standard OpenGL view space usually looks down -Z.
        // Distance = -Z.
        float rayDepth = -viewPos.z;
        float surfaceDepth = -gBufferViewPos.z;
        
        // If Ray is BEHIND Surface (RayDepth > SurfaceDepth)
        // And not TOO far behind (Thickness check)
        if (rayDepth > surfaceDepth && rayDepth < surfaceDepth + 1.5) {
             // Hit!
             // Fade out based on distance from screen edge
             float edgeFade = 1.0 - max(abs(screenPos.x - 0.5), abs(screenPos.y - 0.5)) * 2.0;
             edgeFade = clamp(edgeFade, 0.0, 1.0);
             
             // Get Color at hit position
             return texture(gAlbedoSpec, screenPos.xy).rgb * edgeFade;
        }
    }
    return vec3(0.0); // Miss
}

void main() {
    // 1. [關鍵改變] 從 G-Buffer 讀取幾何資訊
    vec3 FragPos = texture(gPosition, TexCoords).rgb;
    vec3 Normal  = texture(gNormal, TexCoords).rgb;
    vec3 Albedo  = texture(gAlbedoSpec, TexCoords).rgb;
    float Metallic = texture(gAlbedoSpec, TexCoords).a;
    float Roughness = texture(gPBRParams, TexCoords).r;
    float AO = texture(gPBRParams, TexCoords).g;

    // 2. 恢復向量
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    vec3 R = reflect(-V, N);

    // 3. 基礎反射率 F0
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, Albedo, Metallic);

    if (Metallic < 0.1) F0 = vec3(0.08);

    vec3 Lo = vec3(0.0);

    // --- 光照計算 (邏輯與 Forward 相同) ---

    // 1. Directional Light
    if (dl.enable == 1) {
        vec3 L = normalize(-dl.direction);
        vec3 radiance = dl.lightColor * 3.0; 
        
        // [新增] 在 Fragment Shader 中計算光空間座標
        vec4 FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
        
        float shadow = ShadowCalculation(FragPosLightSpace, N, L);
        vec3 lightOut = calculatePBRLight(N, V, L, radiance, F0, Albedo, Metallic, Roughness);
        Lo += lightOut * (1.0 - shadow);
    }

    // 2. Point Lights
    for(int i = 0; i < NR_POINT_LIGHTS; i++) {
        if (pointLights[i].enable == 1) {
            vec3 L = normalize(pointLights[i].position - FragPos);
            float distance = length(pointLights[i].position - FragPos);
            float attenuation = 1.0 / (pointLights[i].constant + pointLights[i].linear * distance + pointLights[i].quadratic * (distance * distance));
            vec3 radiance = pointLights[i].lightColor * attenuation * 3.0;
            Lo += calculatePBRLight(N, V, L, radiance, F0, Albedo, Metallic, Roughness);
        }
    }

    // 3. Spot Lights
    for(int i = 0; i < NR_SPOT_LIGHTS; i++) {
        if (spotLights[i].enable == 1) {
           vec3 L = normalize(spotLights[i].position - FragPos);
           float theta = dot(L, normalize(-spotLights[i].direction));
           if (theta > spotLights[i].cutOff) {
                float distance = length(spotLights[i].position - FragPos);
                float attenuation = 1.0 / (spotLights[i].constant + spotLights[i].linear * distance + spotLights[i].quadratic * (distance * distance));
                vec3 radiance = spotLights[i].lightColor * attenuation * 3.0; 
                Lo += calculatePBRLight(N, V, L, radiance, F0, Albedo, Metallic, Roughness);
           }
        }
    }

    float NdotV = max(dot(N, V), 0.0);

    // 1. Diffuse IBL
    vec3 irradiance = textureLod(skybox, N, 6.0).rgb; 
    irradiance = pow(irradiance, vec3(2.2));          
    
    vec3 F = fresnelSchlickRoughness(NdotV, F0, Roughness);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - Metallic;
    
    vec3 diffuseIBL = irradiance * Albedo * kD;

    // 2. Specular IBL
    const float MAX_REFLECTION_LOD = 4.0;
    
    // (A) 採樣環境光 (Skybox)
    vec3 prefilteredColor = textureLod(skybox, R, Roughness * MAX_REFLECTION_LOD).rgb;
    prefilteredColor = pow(prefilteredColor, vec3(2.2)); 

    // (B) 簡單的 SSR (Screen Space Reflection) Implementation
    // Only perform SSR on fairly smooth surfaces AND if SSR Mask is active
    vec3 ssrColor = vec3(0.0);
    float SSRMask = texture(gPBRParams, TexCoords).b; // Read mask from Blue channel
    
    if (Roughness < 0.4 && SSRMask > 0.5) {
        // Try SSR
        vec3 reflection = SSR(FragPos, R);
        if (length(reflection) > 0.0) {
            // If SSR found something, verify validity by mixing
            // Actually, physics would say we just see the reflection from SSR
            // But we don't have full scene in Screen Space.
            // Let's add it or replace environment map.
            // For puddles, we want strong reflections.
           
            // Simple replace:
            ssrColor = reflection;
        }
    }
    
    // Mix Skybox Reflection and SSR
    // Ideally SSR is primary, fallback to Skybox
    if (length(ssrColor) > 0.01) {
        // Fade between SSR and Skybox based on how strong SSR is?
        // Or just add them (incorrect but looks brighter)?
        // Let's blend:
        // Assume SSR is perfect blocked, but here it's an overlay layer
        prefilteredColor = mix(prefilteredColor, ssrColor, 1.0); // [Updated] Use 100% SSR if found needed for puddles
    }

    vec2 envBRDF = EnvBRDFApprox(Roughness, NdotV);
    vec3 specularIBL = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

    float specularOcclusion = clamp(pow(AO, Roughness * 0.5 + 0.5), 0.0, 1.0);
    specularIBL *= AO; 

    // 4. 合成
    // 4. 合成
    float iblStrength = 1.5; // [Modified] Increase from 0.5 to 1.5
    vec3 ambient = (diffuseIBL + specularIBL * AO) * iblStrength; 
    
    // Note: Previously AO was applied to specularIBL separately. 
    // Now applying AO to specularIBL inside, and multiplying everything by strength.
    // Original: specularIBL *= AO; vec3 ambient = (diffuseIBL + specularIBL) * iblStrength;
    // New: vec3 ambient = (diffuseIBL + specularIBL) * iblStrength; (AO applied above)
    // Actually, let's keep it clean:
    // specularIBL *= AO; // already done above
    // ambient = (diffuseIBL + specularIBL) * iblStrength;
    
    // --- Post Processing ---
    // --- Post Processing ---
    vec3 colorLinear = ambient + Lo;
    
    // [NEW] Fog Calculation
    float dist = length(viewPos - FragPos);
    
    // Exponential Fog (Squared): 1 / exp((dist * density)^2)
    // Adjust density to control thickness. 0.02 is moderate.
    // float density = 0.02; 
    // vec3 fogColor = vec3(0.5, 0.6, 0.7); // Blue-ish grey
    // Using uniforms:
    float fogFactor = 1.0 / exp(pow(dist * fogDensity, 2.0));
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    
    if (length(Normal) > 0.1) { // Only fog geometry, not void/skybox (if skybox is drawn later)
        colorLinear = mix(fogColor, colorLinear, fogFactor);
    }
    
    float exposure = 1.0;
    colorLinear = colorLinear * exposure;

    // ACES Tone Mapping (REMOVED)
    // float a = 2.51f; float b = 0.03f; float c = 2.43f; float d = 0.59f; float e = 0.14f;
    // vec3 mapped = clamp((colorLinear*(a*colorLinear+b))/(colorLinear*(c*colorLinear+d)+e), 0.0, 1.0);

    // Gamma Correction (REMOVED)
    // mapped = pow(mapped, vec3(1.0/2.2));

    FragColor = vec4(colorLinear, 1.0);
    
    // Check whether fragment output is higher than threshold, if so output as brightness color
    float brightness = dot(FragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = vec4(FragColor.rgb, 1.0);
    else
        BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}