// Copyright (c) 2026 General Arcade (Pte. Ltd.)
// SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
//
// Sprite batch vertex shader. Mirrors VERT_SRC in sprite_batch.c.

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 projection;
};

struct Input
{
    float2 position : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
    float4 color    : TEXCOORD2;
};

struct Output
{
    float4 color    : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    // `projection` is uploaded column-major, matching HLSL's default packing,
    // so this is the same product as GLSL's `u_projection * vec4(...)`.
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}
