// Copyright (c) 2026 General Arcade (Pte. Ltd.)
// SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
//
// Sprite/Graphics mask. Mirrors MASK_FRAG_SRC in filters.c: content
// multiplied by the mask's alpha, both already rendered to textures.

Texture2D<float4> tex  : register(t0, space2);
SamplerState      smp  : register(s0, space2);
Texture2D<float4> mask : register(t1, space2);
SamplerState      msmp : register(s1, space2);

struct Input
{
    float2 texcoord : TEXCOORD0;
};

float4 main(Input input) : SV_Target
{
    return tex.Sample(smp, input.texcoord) * mask.Sample(msmp, input.texcoord).a;
}
