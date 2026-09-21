// Copyright (c) 2026 General Arcade (Pte. Ltd.)
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
//
// Alpha filter. Mirrors ALPHA_FRAG_SRC in filters.c.

cbuffer FragmentUniforms : register(b0, space3)
{
    float u_alpha;
};

Texture2D<float4> tex : register(t0, space2);
SamplerState smp      : register(s0, space2);

struct Input
{
    float2 texcoord : TEXCOORD0;
};

float4 main(Input input) : SV_Target
{
    return tex.Sample(smp, input.texcoord) * u_alpha;
}
