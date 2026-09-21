// Copyright (c) 2026 General Arcade (Pte. Ltd.)
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
//
// Port of the ColorFilter shader from rmmz_core.js: hue rotation, colour
// tone, blend colour and brightness. Mirrors COLOR_FILTER_FRAG_SRC in
// filters.c.

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 colorTone;
    float4 blendColor;
    float  hue;
    float  brightness;
    float2 _pad;
};

Texture2D<float4> tex : register(t0, space2);
SamplerState smp      : register(s0, space2);

struct Input
{
    float2 texcoord : TEXCOORD0;
};

float3 rgbToHsl(float3 rgb)
{
    float r = rgb.r;
    float g = rgb.g;
    float b = rgb.b;
    float cmin = min(r, min(g, b));
    float cmax = max(r, max(g, b));
    float h = 0.0;
    float s = 0.0;
    float l = (cmin + cmax) / 2.0;
    float delta = cmax - cmin;
    if (delta > 0.0) {
        if (r == cmax) {
            h = fmod((g - b) / delta + 6.0, 6.0) / 6.0;
        } else if (g == cmax) {
            h = ((b - r) / delta + 2.0) / 6.0;
        } else {
            h = ((r - g) / delta + 4.0) / 6.0;
        }
        if (l < 1.0) {
            s = delta / (1.0 - abs(2.0 * l - 1.0));
        }
    }
    return float3(h, s, l);
}

float3 hslToRgb(float3 hsl)
{
    float h = hsl.x;
    float s = hsl.y;
    float l = hsl.z;
    float c = (1.0 - abs(2.0 * l - 1.0)) * s;
    float x = c * (1.0 - abs(fmod(h * 6.0, 2.0) - 1.0));
    float m = l - c / 2.0;
    float cm = c + m;
    float xm = x + m;
    if (h < 1.0 / 6.0) {
        return float3(cm, xm, m);
    } else if (h < 2.0 / 6.0) {
        return float3(xm, cm, m);
    } else if (h < 3.0 / 6.0) {
        return float3(m, cm, xm);
    } else if (h < 4.0 / 6.0) {
        return float3(m, xm, cm);
    } else if (h < 5.0 / 6.0) {
        return float3(xm, m, cm);
    } else {
        return float3(cm, m, xm);
    }
}

float4 main(Input input) : SV_Target
{
    float4 texel = tex.Sample(smp, input.texcoord);
    float a = texel.a;
    if (a <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);

    float3 hsl = rgbToHsl(texel.rgb);
    hsl.x = fmod(hsl.x + hue / 360.0, 1.0);
    hsl.y = hsl.y * (1.0 - colorTone.a / 255.0);
    float3 rgb = hslToRgb(hsl);
    float r = rgb.r;
    float g = rgb.g;
    float b = rgb.b;
    float r2 = colorTone.r / 255.0;
    float g2 = colorTone.g / 255.0;
    float b2 = colorTone.b / 255.0;
    float r3 = blendColor.r / 255.0;
    float g3 = blendColor.g / 255.0;
    float b3 = blendColor.b / 255.0;
    float i3 = blendColor.a / 255.0;
    float i1 = 1.0 - i3;
    r = clamp((r / a + r2) * a, 0.0, 1.0);
    g = clamp((g / a + g2) * a, 0.0, 1.0);
    b = clamp((b / a + b2) * a, 0.0, 1.0);
    r = clamp(r * i1 + r3 * i3 * a, 0.0, 1.0);
    g = clamp(g * i1 + g3 * i3 * a, 0.0, 1.0);
    b = clamp(b * i1 + b3 * i3 * a, 0.0, 1.0);
    r = r * brightness / 255.0;
    g = g * brightness / 255.0;
    b = b * brightness / 255.0;
    return float4(r, g, b, a);
}
