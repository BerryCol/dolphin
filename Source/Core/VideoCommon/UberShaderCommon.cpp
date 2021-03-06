// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/UberShaderCommon.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/ShaderGenCommon.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/XFMemory.h"

namespace UberShader
{
void WriteUberShaderCommonHeader(ShaderCode& out, APIType api_type,
                                 const ShaderHostConfig& host_config)
{
  // ==============================================
  //  BitfieldExtract for APIs which don't have it
  // ==============================================
  if (!host_config.backend_bitfield)
  {
    out.Write("uint bitfieldExtract(uint val, int off, int size) {{\n"
              "	// This built-in function is only support in OpenGL 4.0+ and ES 3.1+\n"
              "	// Microsoft's HLSL compiler automatically optimises this to a bitfield extract "
              "instruction.\n"
              "	uint mask = uint((1 << size) - 1);\n"
              "	return uint(val >> off) & mask;\n"
              "}}\n\n");
  }
}

void WriteLightingFunction(ShaderCode& out)
{
  // ==============================================
  // Lighting channel calculation helper
  // ==============================================
  out.Write("int4 CalculateLighting(uint index, uint attnfunc, uint diffusefunc, float3 pos, "
            "float3 normal) {{\n"
            "  float3 ldir, h, cosAttn, distAttn;\n"
            "  float dist, dist2, attn;\n"
            "\n"
            "  switch (attnfunc) {{\n");
  out.Write("  case {}u: // LIGNTATTN_NONE\n", LIGHTATTN_NONE);
  out.Write("  case {}u: // LIGHTATTN_DIR\n", LIGHTATTN_DIR);
  out.Write("    ldir = normalize(" I_LIGHTS "[index].pos.xyz - pos.xyz);\n"
            "    attn = 1.0;\n"
            "    if (length(ldir) == 0.0)\n"
            "      ldir = normal;\n"
            "    break;\n\n");
  out.Write("  case {}u: // LIGHTATTN_SPEC\n", LIGHTATTN_SPEC);
  out.Write("    ldir = normalize(" I_LIGHTS "[index].pos.xyz - pos.xyz);\n"
            "    attn = (dot(normal, ldir) >= 0.0) ? max(0.0, dot(normal, " I_LIGHTS
            "[index].dir.xyz)) : 0.0;\n"
            "    cosAttn = " I_LIGHTS "[index].cosatt.xyz;\n");
  out.Write("    if (diffusefunc == {}u) // LIGHTDIF_NONE\n", LIGHTDIF_NONE);
  out.Write("      distAttn = " I_LIGHTS "[index].distatt.xyz;\n"
            "    else\n"
            "      distAttn = normalize(" I_LIGHTS "[index].distatt.xyz);\n"
            "    attn = max(0.0, dot(cosAttn, float3(1.0, attn, attn*attn))) / dot(distAttn, "
            "float3(1.0, attn, attn*attn));\n"
            "    break;\n\n");
  out.Write("  case {}u: // LIGHTATTN_SPOT\n", LIGHTATTN_SPOT);
  out.Write("    ldir = " I_LIGHTS "[index].pos.xyz - pos.xyz;\n"
            "    dist2 = dot(ldir, ldir);\n"
            "    dist = sqrt(dist2);\n"
            "    ldir = ldir / dist;\n"
            "    attn = max(0.0, dot(ldir, " I_LIGHTS "[index].dir.xyz));\n"
            "    attn = max(0.0, " I_LIGHTS "[index].cosatt.x + " I_LIGHTS
            "[index].cosatt.y * attn + " I_LIGHTS "[index].cosatt.z * attn * attn) / dot(" I_LIGHTS
            "[index].distatt.xyz, float3(1.0, dist, dist2));\n"
            "    break;\n\n");
  out.Write("  default:\n"
            "    attn = 1.0;\n"
            "    ldir = normal;\n"
            "    break;\n"
            "  }}\n"
            "\n"
            "  switch (diffusefunc) {{\n");
  out.Write("  case {}u: // LIGHTDIF_NONE\n", LIGHTDIF_NONE);
  out.Write("    return int4(round(attn * float4(" I_LIGHTS "[index].color)));\n\n");
  out.Write("  case {}u: // LIGHTDIF_SIGN\n", LIGHTDIF_SIGN);
  out.Write("    return int4(round(attn * dot(ldir, normal) * float4(" I_LIGHTS
            "[index].color)));\n\n");
  out.Write("  case {}u: // LIGHTDIF_CLAMP\n", LIGHTDIF_CLAMP);
  out.Write("    return int4(round(attn * max(0.0, dot(ldir, normal)) * float4(" I_LIGHTS
            "[index].color)));\n\n");
  out.Write("  default:\n"
            "    return int4(0, 0, 0, 0);\n"
            "  }}\n"
            "}}\n\n");
}

void WriteVertexLighting(ShaderCode& out, APIType api_type, std::string_view world_pos_var,
                         std::string_view normal_var, std::string_view in_color_0_var,
                         std::string_view in_color_1_var, std::string_view out_color_0_var,
                         std::string_view out_color_1_var)
{
  out.Write("// Lighting\n");
  out.Write("{}for (uint chan = 0u; chan < {}u; chan++) {{\n",
            api_type == APIType::D3D ? "[loop] " : "", NUM_XF_COLOR_CHANNELS);
  out.Write("  uint colorreg = xfmem_color(chan);\n"
            "  uint alphareg = xfmem_alpha(chan);\n"
            "  int4 mat = " I_MATERIALS "[chan + 2u]; \n"
            "  int4 lacc = int4(255, 255, 255, 255);\n"
            "\n");

  out.Write("  if ({} != 0u)\n", BitfieldExtract("colorreg", LitChannel().matsource));
  out.Write("    mat.xyz = int3(round(((chan == 0u) ? {}.xyz : {}.xyz) * 255.0));\n",
            in_color_0_var, in_color_1_var);

  out.Write("  if ({} != 0u)\n", BitfieldExtract("alphareg", LitChannel().matsource));
  out.Write("    mat.w = int(round(((chan == 0u) ? {}.w : {}.w) * 255.0));\n", in_color_0_var,
            in_color_1_var);
  out.Write("  else\n"
            "    mat.w = " I_MATERIALS " [chan + 2u].w;\n"
            "\n");

  out.Write("  if ({} != 0u) {{\n", BitfieldExtract("colorreg", LitChannel().enablelighting));
  out.Write("    if ({} != 0u)\n", BitfieldExtract("colorreg", LitChannel().ambsource));
  out.Write("      lacc.xyz = int3(round(((chan == 0u) ? {}.xyz : {}.xyz) * 255.0));\n",
            in_color_0_var, in_color_1_var);
  out.Write("    else\n"
            "      lacc.xyz = " I_MATERIALS " [chan].xyz;\n"
            "\n");
  out.Write("    uint light_mask = {} | ({} << 4u);\n",
            BitfieldExtract("colorreg", LitChannel().lightMask0_3),
            BitfieldExtract("colorreg", LitChannel().lightMask4_7));
  out.Write("    uint attnfunc = {};\n", BitfieldExtract("colorreg", LitChannel().attnfunc));
  out.Write("    uint diffusefunc = {};\n", BitfieldExtract("colorreg", LitChannel().diffusefunc));
  out.Write(
      "    for (uint light_index = 0u; light_index < 8u; light_index++) {{\n"
      "      if ((light_mask & (1u << light_index)) != 0u)\n"
      "        lacc.xyz += CalculateLighting(light_index, attnfunc, diffusefunc, {}, {}).xyz;\n",
      world_pos_var, normal_var);
  out.Write("    }}\n"
            "  }}\n"
            "\n");

  out.Write("  if ({} != 0u) {{\n", BitfieldExtract("alphareg", LitChannel().enablelighting));
  out.Write("    if ({} != 0u) {{\n", BitfieldExtract("alphareg", LitChannel().ambsource));
  out.Write("      if ((components & ({}u << chan)) != 0u) // VB_HAS_COL0\n", VB_HAS_COL0);
  out.Write("        lacc.w = int(round(((chan == 0u) ? {}.w : {}.w) * 255.0));\n", in_color_0_var,
            in_color_1_var);
  out.Write("      else if ((components & {}u) != 0u) // VB_HAS_COLO0\n", VB_HAS_COL0);
  out.Write("        lacc.w = int(round({}.w * 255.0));\n", in_color_0_var);
  out.Write("      else\n"
            "        lacc.w = 255;\n"
            "    }} else {{\n"
            "      lacc.w = " I_MATERIALS " [chan].w;\n"
            "    }}\n"
            "\n");
  out.Write("    uint light_mask = {} | ({} << 4u);\n",
            BitfieldExtract("alphareg", LitChannel().lightMask0_3),
            BitfieldExtract("alphareg", LitChannel().lightMask4_7));
  out.Write("    uint attnfunc = {};\n", BitfieldExtract("alphareg", LitChannel().attnfunc));
  out.Write("    uint diffusefunc = {};\n", BitfieldExtract("alphareg", LitChannel().diffusefunc));
  out.Write("    for (uint light_index = 0u; light_index < 8u; light_index++) {{\n\n"
            "      if ((light_mask & (1u << light_index)) != 0u)\n\n"
            "        lacc.w += CalculateLighting(light_index, attnfunc, diffusefunc, {}, {}).w;\n",
            world_pos_var, normal_var);
  out.Write("    }}\n"
            "  }}\n"
            "\n");

  out.Write("  lacc = clamp(lacc, 0, 255);\n"
            "\n"
            "  // Hopefully GPUs that can support dynamic indexing will optimize this.\n"
            "  float4 lit_color = float4((mat * (lacc + (lacc >> 7))) >> 8) / 255.0;\n"
            "  switch (chan) {{\n"
            "  case 0u: {} = lit_color; break;\n",
            out_color_0_var);
  out.Write("  case 1u: {} = lit_color; break;\n", out_color_1_var);
  out.Write("  }}\n"
            "}}\n"
            "\n");
}
}  // namespace UberShader
