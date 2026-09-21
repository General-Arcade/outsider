// Copyright (c) 2026 General Arcade (Pte. Ltd.)
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
//
// Separable Gaussian blur. Mirrors BLUR_FRAG_SRC in filters.c.

cbuffer FragmentUniforms : register(b0, space3)
{
    float2 u_direction;
    float  u_strength;
    float  _pad;
};

Texture2D<float4> tex : register(t0, space2);
SamplerState smp      : register(s0, space2);

struct Input
{
    float2 texcoord : TEXCOORD0;
};

float4 main(Input input) : SV_Target
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float2 off1 = u_direction * 1.3846153846 * u_strength;
    float2 off2 = u_direction * 3.2307692308 * u_strength;
    color += tex.Sample(smp, input.texcoord) * 0.2270270270;
    color += tex.Sample(smp, input.texcoord + off1) * 0.3162162162;
    color += tex.Sample(smp, input.texcoord - off1) * 0.3162162162;
    color += tex.Sample(smp, input.texcoord + off2) * 0.0702702703;
    color += tex.Sample(smp, input.texcoord - off2) * 0.0702702703;
    return color;
}
