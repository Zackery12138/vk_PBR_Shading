#version 450
layout(set = 1, binding = 0) uniform sampler2D baseColorTex;
layout(set = 1, binding = 1) uniform sampler2D roughnessTex;
layout(set = 1, binding = 2) uniform sampler2D metalnessTex;
layout(set = 1, binding = 3) uniform sampler2D normalMapTex;

layout(std140,set = 0, binding = 0) uniform UScene
{
	mat4 camera;
	mat4 projection;
	mat4 projCam;
	vec3 cameraPos;
    vec3 lightPos;
    vec3 lightColor;
}uScene;

layout( location = 0 ) in vec2 v2fTexCoords;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fPosition;
layout( location = 3 ) in vec4 v2fTangent;

layout( location = 0 ) out vec4 oColor;

const float PI = 3.14159265359;
const float epsilon = 0.0001; 

float rough2shininess(float roughness) {
  return (2.0 / (pow(roughness,4) + epsilon)) - 2.0;
}

vec3 fresnelSchlick(float HdotV, vec3 F0) // F term
{  
    return F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
}

float BlinnPhongDistribution(float shininess, vec3 N, vec3 H)  // D term
{
    float clampedNdotH = max(dot(N, H), 0.0);
    return ((shininess + 2)/ (2 * PI)) * pow(clampedNdotH, shininess);
}

float CookTorranceMaskingTerm(vec3 N, vec3 H, vec3 L, vec3 V)  // G term
{
    float a = 2 * (max(dot(N, H), 0.0) * max(dot(N, V), 0.0)) / dot(V, H);
    float b = 2 * (max(dot(N, H), 0.0) * max(dot(N, L), 0.0)) / dot(V, H);
    return min(1.0, min(a, b));
}

mat3 computeTangentSpaceMatrix(vec3 N, vec4 tangent)
{
    vec3 T = normalize(tangent.xyz);
    vec3 B = cross(N, T) * tangent.w;
    return mat3(T, B, N);
}


void main() {
    //alpha masking
    float alpha = texture(baseColorTex, v2fTexCoords).a;
    if(alpha < 0.5f) discard; 

    vec3 albedo = texture(baseColorTex, v2fTexCoords).rgb;
    float roughness = texture(roughnessTex, v2fTexCoords).r;
    float metalness = texture(metalnessTex, v2fTexCoords).r;

    mat3 TBN = computeTangentSpaceMatrix(normalize(v2fNormal), v2fTangent);
    vec3 normalFromMap = texture(normalMapTex, v2fTexCoords).rgb * 2.0 - 1.0;

    vec3 N = normalize(TBN * normalFromMap);
    //vec3 N = normalize(v2fNormal);
    vec3 V = normalize(uScene.cameraPos - v2fPosition);
    vec3 L = normalize(uScene.lightPos - v2fPosition);

    vec3 H = normalize(V + L);
    float shininess = rough2shininess(roughness);

    vec3 F0 = mix(vec3(0.04), albedo, metalness);

    vec3 F = fresnelSchlick(dot(H, V), F0);
    float D = BlinnPhongDistribution(shininess, N, H);
    float G = CookTorranceMaskingTerm(N, H, L, V);
    vec3 Ldiffuse = (albedo/PI) * (vec3(1.0f) - F) * (1.0f - metalness);


    vec3 nominator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + epsilon;
    vec3 BRDF = Ldiffuse + (nominator / denominator);

    vec3 Lambient = vec3(0.02) * albedo;

    float NdotL = max(dot(N, L), 0.0);

    vec3 result = Lambient + BRDF * uScene.lightColor * NdotL;


    //oColor = vec4(N*0.5f +vec3(0.5f), alpha);
    oColor = vec4(result, alpha);


}