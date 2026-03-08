#version 430

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 color;

uniform sampler2D ourTexture;
uniform vec3 viewPos;

struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
    float reflectivity;
};

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

uniform Material material;
uniform DirectionLight dl;
uniform PointLight pl;
uniform Spotlight sl;
uniform samplerCube skybox;
uniform sampler2D shadowMap;
in vec4 FragPosLightSpace;

// TODO#3-4: fragment shader

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // 1. 透視除法
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // 2. 轉換到 [0,1] 範圍
    projCoords = projCoords * 0.5 + 0.5;
    
    // 處理超出範圍 (超過遠平面不應有陰影)
    if(projCoords.z > 1.0)
        return 0.0;

    // 3. 取得當前片段深度
    float currentDepth = projCoords.z;
    
    // 4. 計算 Bias (避免陰影失真)
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);

    // --- 新增：PCF 柔化陰影 ---
    float shadow = 0.0;
    // textureSize 返回貼圖的大小 (例如 vec2(1024, 1024))
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    
    // 採樣周圍 3x3 的區域
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            // 讀取周圍像素的深度
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
            // 累加陰影值
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    // 取平均 (除以 9)
    shadow /= 9.0;
    
    return shadow;
}

vec3 calculateDirectionLight(vec3 normal, vec3 viewDir) {
    if (dl.enable == 0) return vec3(0.0);
    
    vec3 lightDir = normalize(-dl.direction);
    
    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    
    vec3 ambient  = dl.lightColor * material.ambient;
    vec3 diffuse  = dl.lightColor * diff * material.diffuse;
    vec3 specular = dl.lightColor * spec * material.specular;

    // 新增：計算陰影值 (1.0 代表全陰影，0.0 代表無陰影)
    float shadow = ShadowCalculation(FragPosLightSpace, normal, lightDir);
    
    // 將漫反射和鏡面反射乘以 (1.0 - shadow)。環境光不受陰影影響。
    return (ambient + (1.0 - shadow) * (diffuse + specular));
    
    return (ambient + diffuse + specular);
}

vec3 calculatePointLight(vec3 normal, vec3 viewDir) {
    if (pl.enable == 0) return vec3(0.0);
    
    vec3 lightDir = normalize(pl.position - FragPos);
    float distance = length(pl.position - FragPos);
    float attenuation = 1.0 / (pl.constant + pl.linear * distance + pl.quadratic * (distance * distance));
    
    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    
    vec3 ambient  = pl.lightColor * material.ambient;
    vec3 diffuse  = pl.lightColor * diff * material.diffuse;
    vec3 specular = pl.lightColor * spec * material.specular;
    
    // Note: Following comment "lighting = ambient + attenuation * (diffuse + specular)"
    // Standard OpenGL is usually attenuation * (ambient + ...), but respecting instruction:
    return ambient + attenuation * (diffuse + specular);
}

vec3 calculateSpotLight(vec3 normal, vec3 viewDir) {
    if (sl.enable == 0) return vec3(0.0);
    
    vec3 lightDir = normalize(sl.position - FragPos);
    float distance = length(sl.position - FragPos);
    
    // Calculate attenuation
    float attenuation = 1.0 / (sl.constant + sl.linear * distance + sl.quadratic * (distance * distance));
    
    // Check if inside the spotlight cone
    float theta = dot(lightDir, normalize(-sl.direction));
    
    if(theta > sl.cutOff) {
        // --- Inside the Circle ---
        
        // Ambient
        vec3 ambient = sl.lightColor * material.ambient;
        
        // Diffuse
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 diffuse = sl.lightColor * diff * material.diffuse;
        
        // Specular
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
        vec3 specular = sl.lightColor * spec * material.specular;
        
        return attenuation * (ambient + diffuse + specular);
        
    } else {
        // --- Outside the Circle ---
        // To get "just a circle", we return 0.0 here.
        // This ensures the spotlight adds NO light (green or otherwise) to the rest of the room.
        return vec3(0.0, 0.0, 0.0); 
    }
}

void main() {
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);
    
    // Lighting
    vec3 result = vec3(0.0);
    result += calculateDirectionLight(norm, viewDir);
    result += calculatePointLight(norm, viewDir);
    result += calculateSpotLight(norm, viewDir);
    
    // Texture Color
    vec4 texColor = texture(ourTexture, TexCoord);
    
    // Reflection (Environment Mapping)
    // Calculate reflection vector
    vec3 I = normalize(FragPos - viewPos);
    vec3 R = reflect(I, norm);
    vec3 reflectionColor = texture(skybox, R).rgb;
    
    // Mix lighting result with reflection based on reflectivity
    // The lighting result usually multiplies with the object texture color first
    vec3 lightingColor = result * texColor.rgb;
    
    vec3 finalColor = mix(lightingColor, reflectionColor, material.reflectivity);
    
    color = vec4(finalColor, 1.0);
}