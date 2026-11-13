/*******************************************************************************************
*   raylib PBR specular-glossiness shader (KHR_materials_pbrSpecularGlossiness)
*
*   Embedded GLSL 3.30 / 300 ES shaders used by the glTF loader for materials following
*   the KHR_materials_pbrSpecularGlossiness workflow. Materials using the standard
*   metallic-roughness workflow keep the rlgl default shader, this shader is only assigned
*   to materials that actually require it.
*
*   Uniform names follow raylib conventions so DrawMesh() binds the common ones
*   (matModel/matView/matProjection/matNormal/mvp/colDiffuse/texture slots) automatically;
*   the remaining uniforms are resolved once at load time by the loader.
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
********************************************************************************************/

#ifndef RMODELS_SPECULAR_GLOSSINESS_SHADER_H
#define RMODELS_SPECULAR_GLOSSINESS_SHADER_H

// NOTE: Vertex shader is identical for desktop GL 3.3 and OpenGL ES 3.0
// Vertex shader body (shared by the desktop GL 3.3 and OpenGL ES 3.0 variants)
#define RMODELS_SG_VS_BODY \
"in vec3 vertexPosition;\n" \
"in vec2 vertexTexCoord;\n" \
"in vec3 vertexNormal;\n" \
"in vec4 vertexTangent;\n" \
"in vec4 vertexColor;\n" \
"\n" \
"uniform mat4 mvp;\n" \
"uniform mat4 matView;\n" \
"uniform mat4 matModel;\n" \
"uniform mat4 matNormal;\n" \
"\n" \
"out vec3 fragPosition;\n" \
"out vec2 fragTexCoord;\n" \
"out vec4 fragColor;\n" \
"out vec3 fragNormal;\n" \
"out mat3 fragTBN;\n" \
"\n" \
"void main()\n" \
"{\n" \
"    vec4 worldPosition = matModel*vec4(vertexPosition, 1.0);\n" \
"    fragPosition = worldPosition.xyz;\n" \
"    fragTexCoord = vertexTexCoord;\n" \
"    fragColor = vertexColor;\n" \
"    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));\n" \
"\n" \
"    // Tangent basis for normal mapping (mikktspace style, w signals the bitangent flip)\n" \
"    vec3 tangent = normalize(vec3(matNormal*vec4(vertexTangent.xyz, 0.0)));\n" \
"    tangent = normalize(tangent - dot(tangent, fragNormal)*fragNormal);\n" \
"    vec3 bitangent = cross(fragNormal, tangent)*vertexTangent.w;\n" \
"    fragTBN = mat3(tangent, bitangent, fragNormal);\n" \
"\n" \
"    gl_Position = mvp*vec4(vertexPosition, 1.0);\n" \
"}\n"

// Desktop OpenGL 3.3 and OpenGL ES 3.0 vertex shaders differ only in the version/precision header
#define RMODELS_SG_VS       "#version 330 core\n" RMODELS_SG_VS_BODY
#define RMODELS_SG_VS_ES    "#version 300 es\n" "precision highp float;\n" RMODELS_SG_VS_BODY

// Fragment shader: Cook-Torrance specular + Lambert diffuse, driven by an explicit
// specular reflectance color and a glossiness scalar (alpha of the spec/gloss texture).
#define RMODELS_SG_FS "#version 330 core\n" \
"#define PI 3.14159265358979323846\n" \
"\n" \
"in vec3 fragPosition;\n" \
"in vec2 fragTexCoord;\n" \
"in vec4 fragColor;\n" \
"in vec3 fragNormal;\n" \
"in mat3 fragTBN;\n" \
"\n" \
"out vec4 finalColor;\n" \
"\n" \
"uniform sampler2D texture0;   // diffuse (base color)\n" \
"uniform sampler2D texture2;   // normal\n" \
"uniform sampler2D texture4;   // occlusion\n" \
"uniform sampler2D texture12;  // specular-glossiness (rgb: specular, a: glossiness)\n" \
"\n" \
"uniform vec4 colDiffuse;      // diffuse factor (includes alpha)\n" \
"uniform vec4 colSpecular;     // specular factor\n" \
"uniform float glossinessValue;\n" \
"uniform float alphaCutoff;    // < 0 disables alpha masking\n" \
"uniform int useMapSpecularGlossiness;\n" \
"uniform int useMapNormal;\n" \
"uniform int useMapOcclusion;\n" \
"uniform int doubleSided;\n" \
"\n" \
"// Single directional light, direction goes from the surface toward the light\n" \
"uniform vec3 lightDirection;\n" \
"uniform vec3 lightColor;\n" \
"uniform vec3 viewPos;\n" \
"\n" \
"vec3 SchlickFresnel(float cosTheta, vec3 f0)\n" \
"{\n" \
"    return f0 + (1.0 - f0)*pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n" \
"}\n" \
"\n" \
"float GgxDistribution(float nDotH, float roughness)\n" \
"{\n" \
"    float a2 = roughness*roughness;\n" \
"    float d = nDotH*nDotH*(a2 - 1.0) + 1.0;\n" \
"    return a2/max(PI*d*d, 0.0000001);\n" \
"}\n" \
"\n" \
"float GeometrySchlickGGX(float nDotTerm, float roughness)\n" \
"{\n" \
"    float r = roughness + 1.0;\n" \
"    float k = (r*r)/8.0;\n" \
"    return nDotTerm/(nDotTerm*(1.0 - k) + k);\n" \
"}\n" \
"\n" \
"void main()\n" \
"{\n" \
"    vec4 diffuseTexel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;\n" \
"    vec3 diffuseColor = diffuseTexel.rgb;\n" \
"\n" \
"    vec3 specularColor = colSpecular.rgb;\n" \
"    float glossiness = glossinessValue;\n" \
"    if (useMapSpecularGlossiness == 1)\n" \
"    {\n" \
"        vec4 sgTexel = texture(texture12, fragTexCoord);\n" \
"        // Specular factor and texture specular are both linear-space in glTF\n" \
"        specularColor *= sgTexel.rgb;\n" \
"        glossiness *= sgTexel.a;\n" \
"    }\n" \
"    glossiness = clamp(glossiness, 0.0, 1.0);\n" \
"    float roughness = clamp(1.0 - glossiness, 0.04, 1.0);\n" \
"\n" \
"    vec3 N = normalize(fragNormal);\n" \
"    if (useMapNormal == 1)\n" \
"    {\n" \
"        vec3 tangentNormal = texture(texture2, fragTexCoord).xyz*2.0 - 1.0;\n" \
"        N = normalize(fragTBN*tangentNormal);\n" \
"    }\n" \
"\n" \
"    vec3 V = normalize(viewPos - fragPosition);\n" \
"    if (doubleSided == 1) N = faceforward(N, N, V);\n" \
"\n" \
"    vec3 L = normalize(lightDirection);\n" \
"    vec3 H = normalize(V + L);\n" \
"\n" \
"    float nDotL = max(dot(N, L), 0.0);\n" \
"    float nDotV = max(dot(N, V), 0.0000001);\n" \
"    float nDotH = max(dot(N, H), 0.0);\n" \
"    float hDotV = max(dot(H, V), 0.0);\n" \
"\n" \
"    // Cook-Torrance microfacet specular BRDF with explicit f0 = specular color\n" \
"    vec3 f0 = clamp(specularColor, vec3(0.0), vec3(1.0));\n" \
"    vec3 F = SchlickFresnel(hDotV, f0);\n" \
"    float D = GgxDistribution(nDotH, roughness);\n" \
"    float G = GeometrySchlickGGX(nDotV, roughness)*GeometrySchlickGGX(nDotL, roughness);\n" \
"    vec3 specularBRDF = (D*G*F)/max(4.0*nDotV*nDotL, 0.0000001);\n" \
"\n" \
"    // KHR_materials_pbrSpecularGlossiness: energy-conserving Lambert diffuse,\n" \
"    // the specular lobe removes its reflected fraction from the diffuse term\n" \
"    vec3 kD = (vec3(1.0) - F);\n" \
"    vec3 diffuseBRDF = kD*diffuseColor/PI;\n" \
"\n" \
"    vec3 ambient = diffuseColor*0.18;\n" \
"    vec3 color = ambient + (diffuseBRDF + specularBRDF)*lightColor*nDotL;\n" \
"\n" \
"    if (useMapOcclusion == 1)\n" \
"    {\n" \
"        float ao = texture(texture4, fragTexCoord).r;\n" \
"        color *= ao;\n" \
"    }\n" \
"\n" \
"    // Linear output; raylib default framebuffer expects sRGB-correct authored colors\n" \
"    color = pow(max(color, vec3(0.0)), vec3(1.0/2.2));\n" \
"\n" \
"    float alpha = diffuseTexel.a;\n" \
"    if (alphaCutoff >= 0.0)\n" \
"    {\n" \
"        if (alpha < alphaCutoff) discard;\n" \
"        alpha = 1.0;\n" \
"    }\n" \
"\n" \
"    finalColor = vec4(color, alpha);\n" \
"}\n"

// OpenGL ES 3.0 variant of the fragment shader (precision qualifiers only)
#define RMODELS_SG_FS_ES "#version 300 es\n" \
"precision highp float;\n" \
"#define PI 3.14159265358979323846\n" \
"\n" \
"in vec3 fragPosition;\n" \
"in vec2 fragTexCoord;\n" \
"in vec4 fragColor;\n" \
"in vec3 fragNormal;\n" \
"in mat3 fragTBN;\n" \
"\n" \
"out vec4 finalColor;\n" \
"\n" \
"uniform sampler2D texture0;\n" \
"uniform sampler2D texture2;\n" \
"uniform sampler2D texture4;\n" \
"uniform sampler2D texture12;\n" \
"\n" \
"uniform vec4 colDiffuse;\n" \
"uniform vec4 colSpecular;\n" \
"uniform float glossinessValue;\n" \
"uniform float alphaCutoff;\n" \
"uniform int useMapSpecularGlossiness;\n" \
"uniform int useMapNormal;\n" \
"uniform int useMapOcclusion;\n" \
"uniform int doubleSided;\n" \
"\n" \
"uniform vec3 lightDirection;\n" \
"uniform vec3 lightColor;\n" \
"uniform vec3 viewPos;\n" \
"\n" \
"vec3 SchlickFresnel(float cosTheta, vec3 f0)\n" \
"{\n" \
"    return f0 + (1.0 - f0)*pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n" \
"}\n" \
"\n" \
"float GgxDistribution(float nDotH, float roughness)\n" \
"{\n" \
"    float a2 = roughness*roughness;\n" \
"    float d = nDotH*nDotH*(a2 - 1.0) + 1.0;\n" \
"    return a2/max(PI*d*d, 0.0000001);\n" \
"}\n" \
"\n" \
"float GeometrySchlickGGX(float nDotTerm, float roughness)\n" \
"{\n" \
"    float r = roughness + 1.0;\n" \
"    float k = (r*r)/8.0;\n" \
"    return nDotTerm/(nDotTerm*(1.0 - k) + k);\n" \
"}\n" \
"\n" \
"void main()\n" \
"{\n" \
"    vec4 diffuseTexel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;\n" \
"    vec3 diffuseColor = diffuseTexel.rgb;\n" \
"\n" \
"    vec3 specularColor = colSpecular.rgb;\n" \
"    float glossiness = glossinessValue;\n" \
"    if (useMapSpecularGlossiness == 1)\n" \
"    {\n" \
"        vec4 sgTexel = texture(texture12, fragTexCoord);\n" \
"        specularColor *= sgTexel.rgb;\n" \
"        glossiness *= sgTexel.a;\n" \
"    }\n" \
"    glossiness = clamp(glossiness, 0.0, 1.0);\n" \
"    float roughness = clamp(1.0 - glossiness, 0.04, 1.0);\n" \
"\n" \
"    vec3 N = normalize(fragNormal);\n" \
"    if (useMapNormal == 1)\n" \
"    {\n" \
"        vec3 tangentNormal = texture(texture2, fragTexCoord).xyz*2.0 - 1.0;\n" \
"        N = normalize(fragTBN*tangentNormal);\n" \
"    }\n" \
"\n" \
"    vec3 V = normalize(viewPos - fragPosition);\n" \
"    if (doubleSided == 1) N = faceforward(N, N, V);\n" \
"\n" \
"    vec3 L = normalize(lightDirection);\n" \
"    vec3 H = normalize(V + L);\n" \
"\n" \
"    float nDotL = max(dot(N, L), 0.0);\n" \
"    float nDotV = max(dot(N, V), 0.0000001);\n" \
"    float nDotH = max(dot(N, H), 0.0);\n" \
"    float hDotV = max(dot(H, V), 0.0);\n" \
"\n" \
"    vec3 f0 = clamp(specularColor, vec3(0.0), vec3(1.0));\n" \
"    vec3 F = SchlickFresnel(hDotV, f0);\n" \
"    float D = GgxDistribution(nDotH, roughness);\n" \
"    float G = GeometrySchlickGGX(nDotV, roughness)*GeometrySchlickGGX(nDotL, roughness);\n" \
"    vec3 specularBRDF = (D*G*F)/max(4.0*nDotV*nDotL, 0.0000001);\n" \
"\n" \
"    vec3 kD = (vec3(1.0) - F);\n" \
"    vec3 diffuseBRDF = kD*diffuseColor/PI;\n" \
"\n" \
"    vec3 ambient = diffuseColor*0.18;\n" \
"    vec3 color = ambient + (diffuseBRDF + specularBRDF)*lightColor*nDotL;\n" \
"\n" \
"    if (useMapOcclusion == 1)\n" \
"    {\n" \
"        float ao = texture(texture4, fragTexCoord).r;\n" \
"        color *= ao;\n" \
"    }\n" \
"\n" \
"    color = pow(max(color, vec3(0.0)), vec3(1.0/2.2));\n" \
"\n" \
"    float alpha = diffuseTexel.a;\n" \
"    if (alphaCutoff >= 0.0)\n" \
"    {\n" \
"        if (alpha < alphaCutoff) discard;\n" \
"        alpha = 1.0;\n" \
"    }\n" \
"\n" \
"    finalColor = vec4(color, alpha);\n" \
"}\n"

#endif // RMODELS_SPECULAR_GLOSSINESS_SHADER_H
