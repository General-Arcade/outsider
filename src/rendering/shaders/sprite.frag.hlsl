// Copyright (c) 2026 General Arcade (Pte. Ltd.)
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
//
// Sprite batch fragment shader. Mirrors FRAG_SRC in sprite_batch.c: emits
// premultiplied colour like PIXI, unless the source texture is already
// premultiplied (a render target this batch produced).

cbuffer FragmentUniforms : register(b0, space3)
{
    float premultiplied;
};

Texture2D<float4> tex : register(t0, space2);
SamplerState smp      : register(s0, space2);

struct Input
{
    float4 color    : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

float4 main(Input input) : SV_Target
{
    float4 c = tex.Sample(smp, input.texcoord) * input.color;
    return float4(c.rgb * lerp(c.a, 1.0, premultiplied), c.a);
}
