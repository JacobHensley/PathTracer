/*
 * MIT License
 *
 * Copyright(c) 2019 Asif Ali
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

 /* References:
 * [1] [Physically Based Shading at Disney] https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
 * [2] [Extending the Disney BRDF to a BSDF with Integrated Subsurface Scattering] https://blog.selfshadow.com/publications/s2015-shading-course/burley/s2015_pbs_disney_bsdf_notes.pdf
 * [3] [The Disney BRDF Explorer] https://github.com/wdas/brdf/blob/main/src/brdfs/disney.brdf
 * [4] [Miles Macklin's implementation] https://github.com/mmacklin/tinsel/blob/master/src/disney.h
 * [5] [Simon Kallweit's project report] http://simon-kallweit.me/rendercompo2015/report/
 * [6] [Microfacet Models for Refraction through Rough Surfaces] https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
 * [7] [Sampling the GGX Distribution of Visible Normals] https://jcgt.org/published/0007/04/01/paper.pdf
 * [8] [Pixar's Foundation for Materials] https://graphics.pixar.com/library/PxrMaterialsCourse2017/paper.pdf
 * [9] [Mitsuba 3] https://github.com/mitsuba-renderer/mitsuba3
 */

#include "assets/shaders/RayTracing/Globals.h"
#include "assets/shaders/RayTracing/Sampling.glsl"

vec3 DisneyEval(Payload payload, vec3 V, vec3 N, vec3 L, out float pdf);

vec3 ToWorld(vec3 X, vec3 Y, vec3 Z, vec3 V)
{
    return V.x * X + V.y * Y + V.z * Z;
}

vec3 ToLocal(vec3 X, vec3 Y, vec3 Z, vec3 V)
{
    return vec3(dot(V, X), dot(V, Y), dot(V, Z));
}

float Luminance(vec3 c)
{
    return 0.212671 * c.x + 0.715160 * c.y + 0.072169 * c.z;
}

void TintColors(Payload payload, float eta, out float F0, out vec3 Csheen, out vec3 Cspec0)
{
    float lum = Luminance(payload.Albedo);
    vec3 ctint = lum > 0.0 ? payload.Albedo / lum : vec3(1.0);

    F0 = (1.0 - eta) / (1.0 + eta);
    F0 *= F0;
    
    Cspec0 = F0 * mix(vec3(1.0), ctint, payload.SpecularTint);
    Csheen = mix(vec3(1.0), ctint, payload.SheenTint);
}

vec3 EvalDisneyDiffuse(Payload payload, vec3 Csheen, vec3 V, vec3 L, vec3 H, out float pdf)
{
    pdf = 0.0;
    if (L.z <= 0.0)
        return vec3(0.0);

    float LDotH = dot(L, H);

    float Rr = 2.0 * payload.Roughness * LDotH * LDotH;

    // Diffuse
    float FL = SchlickWeight(L.z);
    float FV = SchlickWeight(V.z);
    float Fretro = Rr * (FL + FV + FL * FV * (Rr - 1.0));
    float Fd = (1.0 - 0.5 * FL) * (1.0 - 0.5 * FV);

    // Fake subsurface
    float Fss90 = 0.5 * Rr;
    float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1.0 / (L.z + V.z) - 0.5) + 0.5);

    // Sheen
    float FH = SchlickWeight(LDotH);
    vec3 Fsheen = FH * payload.Sheen * Csheen;

    pdf = L.z * INV_PI;
    return INV_PI * payload.Albedo * mix(Fd + Fretro, ss, payload.Subsurface) + Fsheen;
}

vec3 EvalMicrofacetReflection(Payload payload, vec3 V, vec3 L, vec3 H, vec3 F, out float pdf)
{
    pdf = 0.0;
    if (L.z <= 0.0)
        return vec3(0.0);

    float D = GTR2Aniso(H.z, H.x, H.y, payload.ax, payload.ay);
    float G1 = SmithGAniso(abs(V.z), V.x, V.y, payload.ax, payload.ay);
    float G2 = G1 * SmithGAniso(abs(L.z), L.x, L.y, payload.ax, payload.ay);

    pdf = G1 * D / (4.0 * V.z);
    return F * D * G2 / (4.0 * L.z * V.z);
}

vec3 EvalMicrofacetRefraction(Payload payload, float eta, vec3 V, vec3 L, vec3 H, vec3 F, out float pdf)
{
    pdf = 0.0;
    if (L.z >= 0.0)
        return vec3(0.0);

    float LDotH = dot(L, H);
    float VDotH = dot(V, H);

    float D = GTR2Aniso(H.z, H.x, H.y, payload.ax, payload.ay);
    float G1 = SmithGAniso(abs(V.z), V.x, V.y, payload.ax, payload.ay);
    float G2 = G1 * SmithGAniso(abs(L.z), L.x, L.y, payload.ax, payload.ay);
    float denom = LDotH + VDotH * eta;
    denom *= denom;
    float eta2 = eta * eta;
    float jacobian = abs(LDotH) / denom;

    pdf = G1 * max(0.0, VDotH) * D * jacobian / V.z;
    return pow(payload.Albedo, vec3(0.5)) * (1.0 - F) * D * G2 * abs(VDotH) * jacobian * eta2 / abs(L.z * V.z);
}

vec3 EvalClearcoat(Payload payload, vec3 V, vec3 L, vec3 H, out float pdf)
{
    pdf = 0.0;
    if (L.z <= 0.0)
        return vec3(0.0);

    float VDotH = dot(V, H);

    float F = mix(0.04, 1.0, SchlickWeight(VDotH));
    float D = GTR1(H.z, payload.ClearcoatRoughness);
    float G = SmithG(L.z, 0.25) * SmithG(V.z, 0.25);
    float jacobian = 1.0 / (4.0 * VDotH);

    pdf = D * H.z * jacobian;
    return vec3(F) * D * G;
}

vec3 DisneySample(Payload payload, vec3 V, vec3 N, out vec3 L, out float pdf, inout uint seed)
{
    pdf = 0.0;

    float r1 = RandomValue(seed);
    float r2 = RandomValue(seed);

    // TODO: Tangent and bitangent should be calculated from mesh (provided, the mesh has proper uvs)
    vec3 T, B;
    Onb(N, T, B);

    // Transform to shading space to simplify operations (NDotL = L.z; NDotV = V.z; NDotH = H.z)
    V = ToLocal(T, B, N, V);

    // Tint colors
    vec3 Csheen, Cspec0;
    float F0;
    TintColors(payload, payload.eta, F0, Csheen, Cspec0);

    // Model weights
    float dielectricWt = (1.0 - payload.Metallic) * (1.0 - payload.SpecTrans);
    float metalWt = payload.Metallic;
    float glassWt = (1.0 - payload.Metallic) * payload.SpecTrans;

    // Lobe probabilities
    float schlickWt = SchlickWeight(V.z);

    float diffPr = dielectricWt * Luminance(payload.Albedo);
    float dielectricPr = dielectricWt * Luminance(mix(Cspec0, vec3(1.0), schlickWt));
    float metalPr = metalWt * Luminance(mix(payload.Albedo, vec3(1.0), schlickWt));
    float glassPr = glassWt;
    float clearCtPr = 0.25 * payload.Clearcoat;

    // Normalize probabilities
    float invTotalWt = 1.0 / (diffPr + dielectricPr + metalPr + glassPr + clearCtPr);
    diffPr *= invTotalWt;
    dielectricPr *= invTotalWt;
    metalPr *= invTotalWt;
    glassPr *= invTotalWt;
    clearCtPr *= invTotalWt;

    // CDF of the sampling probabilities
    float cdf[5];
    cdf[0] = diffPr;
    cdf[1] = cdf[0] + dielectricPr;
    cdf[2] = cdf[1] + metalPr;
    cdf[3] = cdf[2] + glassPr;
    cdf[4] = cdf[3] + clearCtPr;

    // Sample a lobe based on its importance
    float r3 = RandomValue(seed);

    if (r3 < cdf[0]) // Diffuse
    {
        L = CosineSampleHemisphere(r1, r2);
    }
    else if (r3 < cdf[2]) // Dielectric + Metallic reflection
    {
        vec3 H = SampleGGXVNDF(V, payload.ax, payload.ay, r1, r2);

        if (H.z < 0.0)
            H = -H;

        L = normalize(reflect(-V, H));
    }
    else if (r3 < cdf[3]) // Glass
    {
        vec3 H = SampleGGXVNDF(V, payload.ax, payload.ay, r1, r2);
        float F = DielectricFresnel(abs(dot(V, H)), payload.eta);

        if (H.z < 0.0)
            H = -H;

        // Rescale random number for reuse
        r3 = (r3 - cdf[2]) / (cdf[3] - cdf[2]);

        // Reflection
        if (r3 < F)
        {
            L = normalize(reflect(-V, H));
        }
        else // Transmission
        {
            L = normalize(refract(-V, H, payload.eta));
        }
    }
    else // Clearcoat
    {
        vec3 H = SampleGTR1(payload.ClearcoatRoughness, r1, r2);

        if (H.z < 0.0)
            H = -H;

        L = normalize(reflect(-V, H));
    }

    L = ToWorld(T, B, N, L);
    V = ToWorld(T, B, N, V);

    return DisneyEval(payload, V, N, L, pdf);
}

vec3 DisneyEval(Payload payload, vec3 V, vec3 N, vec3 L, out float pdf)
{
    pdf = 0.0;
    vec3 f = vec3(0.0);

    // TODO: Tangent and bitangent should be calculated from mesh (provided, the mesh has proper uvs)
    vec3 T, B;
    Onb(N, T, B);

    // Transform to shading space to simplify operations (NDotL = L.z; NDotV = V.z; NDotH = H.z)
    V = ToLocal(T, B, N, V);
    L = ToLocal(T, B, N, L);

    vec3 H;
    if (L.z > 0.0)
        H = normalize(L + V);
    else
        H = normalize(L + V * payload.eta);

    if (H.z < 0.0)
        H = -H;

    // Tint colors
    vec3 Csheen, Cspec0;
    float F0;
    TintColors(payload, payload.eta, F0, Csheen, Cspec0);

    // Model weights
    float dielectricWt = (1.0 - payload.Metallic) * (1.0 - payload.SpecTrans);
    float metalWt = payload.Metallic;
    float glassWt = (1.0 - payload.Metallic) * payload.SpecTrans;

    // Lobe probabilities
    float schlickWt = SchlickWeight(V.z);

    float diffPr = dielectricWt * Luminance(payload.Albedo);
    float dielectricPr = dielectricWt * Luminance(mix(Cspec0, vec3(1.0), schlickWt));
    float metalPr = metalWt * Luminance(mix(payload.Albedo, vec3(1.0), schlickWt));
    float glassPr = glassWt;
    float clearCtPr = 0.25 * payload.Clearcoat;

    // Normalize probabilities
    float invTotalWt = 1.0 / (diffPr + dielectricPr + metalPr + glassPr + clearCtPr);
    diffPr *= invTotalWt;
    dielectricPr *= invTotalWt;
    metalPr *= invTotalWt;
    glassPr *= invTotalWt;
    clearCtPr *= invTotalWt;

    bool reflect = L.z * V.z > 0;

    float tmpPdf = 0.0;
    float VDotH = abs(dot(V, H));

    // Diffuse
    if (diffPr > 0.0 && reflect)
    {
        f += EvalDisneyDiffuse(payload, Csheen, V, L, H, tmpPdf) * dielectricWt;
        pdf += tmpPdf * diffPr;
    }

    // Dielectric Reflection
    if (dielectricPr > 0.0 && reflect)
    {
        // Normalize for interpolating based on Cspec0
        float F = (DielectricFresnel(VDotH, 1.0 / payload.ior) - F0) / (1.0 - F0);

        f += EvalMicrofacetReflection(payload, V, L, H, mix(Cspec0, vec3(1.0), F), tmpPdf) * dielectricWt;
        pdf += tmpPdf * dielectricPr;
    }

    // Metallic Reflection
    if (metalPr > 0.0 && reflect)
    {
        // Tinted to base color
        vec3 F = mix(payload.Albedo, vec3(1.0), SchlickWeight(VDotH));

        f += EvalMicrofacetReflection(payload, V, L, H, F, tmpPdf) * metalWt;
        pdf += tmpPdf * metalPr;
    }

    // Glass/Specular BSDF
    if (glassPr > 0.0)
    {
        // Dielectric fresnel (achromatic)
        float F = DielectricFresnel(VDotH, payload.eta);

        if (reflect)
        {
            f += EvalMicrofacetReflection(payload, V, L, H, vec3(F), tmpPdf) * glassWt;
            pdf += tmpPdf * glassPr * F;
        }
        else
        {
            f += EvalMicrofacetRefraction(payload, payload.eta, V, L, H, vec3(F), tmpPdf) * glassWt;
            pdf += tmpPdf * glassPr * (1.0 - F);
        }
    }

    // Clearcoat
    if (clearCtPr > 0.0 && reflect)
    {
        f += EvalClearcoat(payload, V, L, H, tmpPdf) * 0.25 * payload.Clearcoat;
        pdf += tmpPdf * clearCtPr;
    }

    return f * abs(L.z);
}