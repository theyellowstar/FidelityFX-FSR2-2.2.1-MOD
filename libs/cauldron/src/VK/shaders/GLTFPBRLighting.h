// Portions Copyright 2019 Advanced Micro Devices, Inc.All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
 
// This shader code was ported from https://github.com/KhronosGroup/glTF-WebGL-PBR
// All credits should go to his original author.

//
// This fragment shader defines a reference implementation for Physically Based Shading of
// a microfacet surface material defined by a glTF model.
//
// References:
// [1] Real Shading in Unreal Engine 4
//     http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// [2] Physically Based Shading at Disney
//     http://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
// [3] README.md - Environment Maps
//     https://github.com/KhronosGroup/glTF-WebGL-PBR/#environment-maps
// [4] "An Inexpensive BRDF Model for Physically based Rendering" by Christophe Schlick
//     https://www.cs.virginia.edu/~jdl/bib/appearance/analytic%20models/schlick94b.pdf

#define USE_PUNCTUAL

struct MaterialInfo
{
    float perceptualRoughness;    // roughness value, as authored by the model creator (input to shader)
    vec3 reflectance0;            // full reflectance color (normal incidence angle)

    float alphaRoughness;         // roughness mapped to a more linear change in the roughness (proposed by [2])
    vec3 diffuseColor;            // color contribution from diffuse lighting

    vec3 reflectance90;           // reflectance color at grazing angle
    vec3 specularColor;           // color contribution from specular lighting
};

//
//  Get SSAO from texture
//
#ifdef ID_SSAO
layout(set = 1, binding = ID_SSAO) uniform sampler2D ssaoSampler;

float GetSSAO(vec2 coords)
{
    return texture(ssaoSampler, coords).r;
}
#else
float GetSSAO(vec2 coords)
{
    return 1.0f;
}
#endif

// Calculation of the lighting contribution from an optional Image Based Light source.
// Precomputed Environment Maps are required uniform inputs and are computed as outlined in [1].
// See our README.md on Environment Maps [3] for additional discussion.
#ifdef USE_IBL
vec3 getIBLContribution(MaterialInfo materialInfo, vec3 n, vec3 v)
{
    float NdotV = clamp(dot(n, v), 0.0, 1.0);
    
    float u_MipCount = 9.0; // resolution of 512x512 of the IBL
    float lod = clamp(materialInfo.perceptualRoughness * float(u_MipCount), 0.0, float(u_MipCount));
    vec3 reflection = normalize(reflect(-v, n));

    vec2 brdfSamplePoint = clamp(vec2(NdotV, materialInfo.perceptualRoughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
    // retrieve a scale and bias to F0. See [1], Figure 3
    vec2 brdf = texture(u_brdfLUT, brdfSamplePoint).rg;

    vec3 diffuseLight = texture(u_DiffuseEnvSampler, n).rgb;

#ifdef USE_TEX_LOD
    vec3 specularLight = textureLod(u_SpecularEnvSampler, reflection, lod).rgb;
#else
    vec3 specularLight = texture(u_SpecularEnvSampler, reflection).rgb;
#endif

    vec3 diffuse = diffuseLight * materialInfo.diffuseColor;
    vec3 specular = specularLight * (materialInfo.specularColor * brdf.x + brdf.y);

    return diffuse + specular;
}
#endif

// Lambert lighting
// see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
vec3 diffuse(MaterialInfo materialInfo)
{
    return materialInfo.diffuseColor / M_PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from [4], Equation 15
vec3 specularReflection(MaterialInfo materialInfo, AngularInfo angularInfo)
{
    return materialInfo.reflectance0 + (materialInfo.reflectance90 - materialInfo.reflectance0) * pow(clamp(1.0 - angularInfo.VdotH, 0.0, 1.0), 5.0);
}

// Smith Joint GGX
// Note: Vis = G / (4 * NdotL * NdotV)
// see Eric Heitz. 2014. Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs. Journal of Computer Graphics Techniques, 3
// see Real-Time Rendering. Page 331 to 336.
// see https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg)
float visibilityOcclusion(MaterialInfo materialInfo, AngularInfo angularInfo)
{
    float NdotL = angularInfo.NdotL;
    float NdotV = angularInfo.NdotV;
    float alphaRoughnessSq = materialInfo.alphaRoughness * materialInfo.alphaRoughness;

    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);

    float GGX = GGXV + GGXL;
    if (GGX > 0.0)
    {
        return 0.5 / GGX;
    }
    return 0.0;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float microfacetDistribution(MaterialInfo materialInfo, AngularInfo angularInfo)
{
    float alphaRoughnessSq = materialInfo.alphaRoughness * materialInfo.alphaRoughness;
    float f = (angularInfo.NdotH * alphaRoughnessSq - angularInfo.NdotH) * angularInfo.NdotH + 1.0;
    return alphaRoughnessSq / (M_PI * f * f + 0.000001f);
}

vec3 getPointShade(vec3 pointToLight, MaterialInfo materialInfo, vec3 normal, vec3 view)
{
    AngularInfo angularInfo = getAngularInfo(pointToLight, normal, view);

    if (angularInfo.NdotL > 0.0 || angularInfo.NdotV > 0.0)
    {
        // Calculate the shading terms for the microfacet specular shading model
        vec3 F = specularReflection(materialInfo, angularInfo);
        float Vis = visibilityOcclusion(materialInfo, angularInfo);
        float D = microfacetDistribution(materialInfo, angularInfo);

        // Calculation of analytical lighting contribution
        vec3 diffuseContrib = (1.0 - F) * diffuse(materialInfo);
        vec3 specContrib = F * Vis * D;

        // Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
        return angularInfo.NdotL * (diffuseContrib + specContrib);
    }

    return vec3(0.0, 0.0, 0.0);
}

// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_lights_punctual/README.md#range-property
float getRangeAttenuation(float range, float distance)
{
    if (range < 0.0)
    {
        // negative range means unlimited
        return 1.0;
    }
    return max(mix(1,0, distance/ range),0);//max(min(1.0 - pow(distance / range, 4.0), 1.0), 0.0) / pow(distance, 2.0);
}

// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_lights_punctual/README.md#inner-and-outer-cone-angles
float getSpotAttenuation(vec3 pointToLight, vec3 spotDirection, float outerConeCos, float innerConeCos)
{
    float actualCos = dot(normalize(spotDirection), normalize(-pointToLight));
    if (actualCos > outerConeCos)
    {
        if (actualCos < innerConeCos)
        {
            return smoothstep(outerConeCos, innerConeCos, actualCos);
        }
        return 1.0;
    }
    return 0.0;
}

vec3 applyDirectionalLight(Light light, MaterialInfo materialInfo, vec3 normal, vec3 view)
{
    vec3 pointToLight = light.direction;
    vec3 shade = getPointShade(pointToLight, materialInfo, normal, view);
    return light.intensity * light.color * shade;
}

vec3 applyPointLight(Light light, MaterialInfo materialInfo, vec3 normal, vec3 worldPos, vec3 view)
{
    vec3 pointToLight = light.position - worldPos;
    float distance = length(pointToLight);
    float attenuation = getRangeAttenuation(light.range, distance);
    vec3 shade = getPointShade(pointToLight, materialInfo, normal, view);
    return attenuation * light.intensity * light.color * shade;
}

vec3 applySpotLight(Light light, MaterialInfo materialInfo, vec3 normal, vec3 worldPos, vec3 view)
{
    vec3 pointToLight = light.position - worldPos;
    float distance = length(pointToLight);
    float rangeAttenuation = getRangeAttenuation(light.range, distance);
    float spotAttenuation = getSpotAttenuation(pointToLight, -light.direction, light.outerConeCos, light.innerConeCos);
    vec3 shade = getPointShade(pointToLight, materialInfo, normal, view);
    return rangeAttenuation * spotAttenuation * light.intensity * light.color * shade;
}

vec3 doPbrLighting(VS2PS Input, PerFrame perFrame, vec3 diffuseColor, vec3 specularColor, float perceptualRoughness)
{
#ifdef MATERIAL_UNLIT
    vec3 outColor = vec4((baseColor.rgb), baseColor.a);
    return outColor;
#endif

    // Roughness is authored as perceptual roughness; as is convention,
    // convert to material roughness by squaring the perceptual roughness [2].
    float alphaRoughness = perceptualRoughness * perceptualRoughness;

    // Compute reflectance.
    float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

    vec3 specularEnvironmentR0 = specularColor.rgb;
    // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
    vec3 specularEnvironmentR90 = vec3(clamp(reflectance * 50.0, 0.0, 1.0));

    MaterialInfo materialInfo = MaterialInfo(
        perceptualRoughness,
        specularEnvironmentR0,
        alphaRoughness,
        diffuseColor,
        specularEnvironmentR90,
        specularColor
    );

    // LIGHTING

    vec3 color = vec3(0.0, 0.0, 0.0);
    vec3 normal = getPixelNormal(Input);
    vec3 worldPos = Input.WorldPos;
    vec3 view = normalize(myPerFrame.u_CameraPos.xyz - Input.WorldPos);

#if (DEF_doubleSided == 1)
    if (dot(normal, view) < 0)
    {
        normal = -normal;
    }
#endif

#ifdef USE_PUNCTUAL
    for (int i = 0; i < myPerFrame.u_lightCount; ++i)
    {
        Light light = myPerFrame.u_lights[i];

        float shadowFactor = DoSpotShadow(worldPos, light);

        if (light.type == LightType_Directional)
        {
            color += applyDirectionalLight(light, materialInfo, normal, view) * shadowFactor;
        }
        else if (light.type == LightType_Point)
        {
            color += applyPointLight(light, materialInfo, normal, worldPos, view);
        }
        else if (light.type == LightType_Spot)
        {
            
            color += applySpotLight(light, materialInfo, normal, worldPos, view) * shadowFactor;
        }
    }
#endif

    // Calculate lighting contribution from image based lighting source (IBL)
#ifdef USE_IBL
    color += getIBLContribution(materialInfo, normal, view) * myPerFrame.u_iblFactor  * GetSSAO(gl_FragCoord.xy * perFrame.u_invScreenResolution);
#endif

    float ao = 1.0;
    // Apply optional PBR terms for additional (optional) shading
#ifdef ID_occlusionTexture
    ao = texture(u_OcclusionSampler, getOcclusionUV(Input)).r;
    color = color * ao; //mix(color, color * ao, myPerFrame.u_OcclusionStrength);
#endif

    vec3 emissive = vec3(0);
#ifdef ID_emissiveTexture
    emissive = (texture(u_EmissiveSampler, getEmissiveUV(Input), myPerFrame.u_LodBias)).rgb * u_pbrParams.myPerObject_u_EmissiveFactor.rgb * myPerFrame.u_EmissiveFactor;
#else
    emissive = u_pbrParams.myPerObject_u_EmissiveFactor.rgb * perFrame.u_EmissiveFactor;
#endif
    color += emissive;

#ifndef DEBUG_OUTPUT // no debug

    // regular shading
    vec3 outColor = color;

#else // debug output

#ifdef DEBUG_METALLIC
    outColor.rgb = vec3(metallic);
#endif

#ifdef DEBUG_ROUGHNESS
    outColor.rgb = vec3(perceptualRoughness);
#endif

#ifdef DEBUG_NORMAL
#ifdef ID_normalTexCoord
    outColor.rgb = texture(u_NormalSampler, getNormalUV()).rgb;
#else
    outColor.rgb = vec3(0.5, 0.5, 1.0);
#endif
#endif

#ifdef DEBUG_BASECOLOR
    outColor.rgb = (baseColor.rgb);
#endif

#ifdef DEBUG_OCCLUSION
    outColor.rgb = vec3(ao);
#endif

#ifdef DEBUG_EMISSIVE
    outColor.rgb = (emissive);
#endif

#ifdef DEBUG_F0
    outColor.rgb = vec3(f0);
#endif

#ifdef DEBUG_ALPHA
    outColor.rgb = vec3(baseColor.a);
#endif

#endif // !DEBUG_OUTPUT

    return outColor;
}
