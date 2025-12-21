#version 430 core

layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;
layout (location = 3) out vec3 gPBRParams;

in vec2 TexCoords;
in vec2 TexCoords1; // [NEW]
in vec3 FragPos;
in vec3 Normal;

uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform sampler2D roughnessTextureSampler; // ORM Map: R=AO, G=Roughness, B=Metallic
uniform sampler2D emissiveMap;

uniform int useNormalMap;
uniform int useRoughnessMap;
uniform int useEmissiveMap;
uniform int uIsTrack; // [NEW] 1=Track/Road, 0=Object

uniform vec4 uBaseColorFactor;
uniform float uRoughnessFactor;
uniform float uMetallicFactor;
uniform vec3 uEmissiveFactor;
uniform float wetnessLevel; // [NEW] 0.0 = Dry, 1.0 = Wet

// TBN Matrix calculation using derivatives (no precomputed tangents needed)
mat3 getTBN(vec3 N, vec3 p, vec2 uv) {
    // get edge vectors of the pixel triangle
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    // solve the linear system
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale-invariant frame 
    float invmax = inversesqrt(max(dot(T,T), dot(B,B)));
    return mat3(T * invmax, B * invmax, N);
}

void main() {
    gPosition = FragPos;
    
    // 1. Base Color
    vec4 baseColor = texture(diffuseMap, TexCoords) * uBaseColorFactor;
    if(baseColor.a < 0.1) discard;
    
    // Gamma Correction
    gAlbedoSpec.rgb = pow(baseColor.rgb, vec3(2.2)); 

    // [User Request] Brighten dark Albedo to make shadows visible (Plan 2)
    // If albedo is very dark (length < 0.1), lift it to dark grey.
    if (length(gAlbedoSpec.rgb) < 0.1) {
        gAlbedoSpec.rgb += vec3(0.08);
    }

    // 2. Normal Mapping
    vec3 navg = normalize(Normal);
    vec3 finalNormal = navg;
    if (useNormalMap == 1) {
        mat3 TBN = getTBN(navg, FragPos, TexCoords);
        vec3 mapNormal = texture(normalMap, TexCoords).rgb;
        mapNormal = mapNormal * 2.0 - 1.0;
        finalNormal = normalize(TBN * mapNormal);
    }
    gNormal = finalNormal;

    // 3. PBR (ORM)
    // GLTF ORM: R=Occlusion, G=Roughness, B=Metallic
    // Default values if no map
    float roughness = uRoughnessFactor;
    float metallic = uMetallicFactor;
    float ao = 1.0;
    


    if (useRoughnessMap == 1) {
        vec2 uv = TexCoords;
        if (uIsTrack == 1) {
            // [User Request] Use TexCoords1 (Unique UV) but Flip Y to fix placement for Road
            uv = TexCoords1;
            uv.y = 1.0 - uv.y;
        }
        
        vec3 orm = texture(roughnessTextureSampler, uv).rgb;
        
        // PBR Logic: Map * Factor
        ao = orm.r;

        // [User Request] Logic Update:
        // 1. Rain (wetnessLevel > 0): Ignore texture, make it all reflective (smooth).
        // 2. Dry: Use Texture (Black=Smooth, White=Rough).
        if (wetnessLevel > 0.0 && uIsTrack == 1) {
            roughness = 0.05; // Very smooth/reflective (Road Puddles)
            
            // [Fix] Water fills cracks => Remove Ambient Occlusion (AO=1.0)
            ao = 1.0; 
            
            // [Fix] Wet surfaces are darker (absorb light), which makes reflections "pop"
            gAlbedoSpec.rgb *= 0.3;
        } else {
            // Dry (or Non-Track Object):
            // Use organic roughness from map
            // If it's the car (uIsTrack=0), we don't want to enforce mirror.
            
            float mapRoughness = orm.g;
            
            if (uIsTrack == 1) {
               // Asphalt adjustment
               roughness = mapRoughness * uRoughnessFactor * 0.6; 
            } else {
               // Car/Standard Object
               roughness = mapRoughness * uRoughnessFactor;
               
               // [Optional] Simple wetness for car?
               if (wetnessLevel > 0.0) {
                   // Make it slightly shinier but preserve material definition
                   roughness *= 0.5; 
               }
            }
        }
        
        metallic = orm.b * uMetallicFactor;
    } 

    // SSR Mask logic (Roughness threshold)
    float ssrMask = (roughness < 0.4) ? 1.0 : 0.0;

    // 4. Emissive (Not stored in G-Buffer for now, but read logic is here)
    // Deferred pipeline requires an Emissive Buffer to support this properly.
    // For now, we focus on BaseColor/Normal/ORM/Metallic.
    
    // Write to G-Buffer
    gPBRParams.r = roughness;
    gPBRParams.g = ao;
    gPBRParams.b = ssrMask;

    gAlbedoSpec.a = metallic; 
}