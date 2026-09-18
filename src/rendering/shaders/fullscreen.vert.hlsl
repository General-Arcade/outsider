// Copyright (c) 2026 General Arcade (Pte. Ltd.)
// SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
//
// Passthrough vertex shader for filter passes. Mirrors DEFAULT_VERT_SRC in
// filters.c: positions already sit in clip space.

struct Input
{
    float2 position : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

struct Output
{
    float2 texcoord : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}
