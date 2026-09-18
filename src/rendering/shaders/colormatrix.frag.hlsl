// Copyright (c) 2026 General Arcade (Pte. Ltd.)
// SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
//
// Colour matrix filter. Mirrors COLOR_MATRIX_FRAG_SRC in filters.c: the
// input is premultiplied, so it is unpremultiplied before the matrix and
// premultiplied again afterwards.

cbuffer FragmentUniforms : register(b0, space3)
{
    float4x4 u_colorMatrix;
    float4   u_colorOffset;
};

Texture2D<float4> tex : register(t0, space2);
SamplerState smp      : register(s0, space2);

struct Input
{
    float2 texcoord : TEXCOORD0;
};

float4 main(Input input) : SV_Target
{
    float4 c = tex.Sample(smp, input.texcoord);
    if (c.a > 0.0) {
        c.rgb /= c.a;
    }
    float4 result = mul(u_colorMatrix, c) + u_colorOffset;
    result = clamp(result, 0.0, 1.0);
    result.rgb *= result.a;
    return result;
}
