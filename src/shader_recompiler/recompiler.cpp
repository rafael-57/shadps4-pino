// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/frontend/control_flow_graph.h"
#include "shader_recompiler/frontend/decode.h"
#include "shader_recompiler/frontend/structured_control_flow.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/post_order.h"

namespace Shader {

IR::BlockList GenerateBlocks(const IR::AbstractSyntaxList& syntax_list) {
    size_t num_syntax_blocks{};
    for (const auto& node : syntax_list) {
        if (node.type == IR::AbstractSyntaxNode::Type::Block) {
            ++num_syntax_blocks;
        }
    }
    IR::BlockList blocks;
    blocks.reserve(num_syntax_blocks);
    u32 order_index{};
    for (const auto& node : syntax_list) {
        if (node.type == IR::AbstractSyntaxNode::Type::Block) {
            blocks.push_back(node.data.block);
        }
    }
    return blocks;
}

IR::Program TranslateProgram(ObjectPool<IR::Inst>& inst_pool, ObjectPool<IR::Block>& block_pool,
                             std::span<const u32> token, const Info&& info) {
    // Ensure first instruction is expected.
    constexpr u32 token_mov_vcchi = 0xBEEB03FF;
    ASSERT_MSG(token[0] == token_mov_vcchi, "First instruction is not s_mov_b32 vcc_hi, #imm");

    Gcn::GcnCodeSlice slice(token.data(), token.data() + token.size());
    Gcn::GcnDecodeContext decoder;

    // Decode and save instructions
    IR::Program program;
    program.ins_list.reserve(token.size());
    while (!slice.atEnd()) {
        program.ins_list.emplace_back(decoder.decodeInstruction(slice));
    }

    // Create control flow graph
    ObjectPool<Gcn::Block> gcn_block_pool{64};
    Gcn::CFG cfg{gcn_block_pool, program.ins_list};

    // Structurize control flow graph and create program.
    program.info = std::move(info);
    program.syntax_list = Shader::Gcn::BuildASL(inst_pool, block_pool, cfg, program.info);
    program.blocks = GenerateBlocks(program.syntax_list);
    program.post_order_blocks = Shader::IR::PostOrder(program.syntax_list.front());

    fmt::print("Pre SSA passes\n\n{}\n", Shader::IR::DumpProgram(program));
    std::fflush(stdout);

    // Run optimization passes
    Shader::Optimization::SsaRewritePass(program.post_order_blocks);

    fmt::print("Post SSA passes\n\n{}\n", Shader::IR::DumpProgram(program));
    std::fflush(stdout);

    Shader::Optimization::ResourceTrackingPass(program);
    Shader::Optimization::ConstantPropagationPass(program.post_order_blocks);
    Shader::Optimization::IdentityRemovalPass(program.blocks);
    Shader::Optimization::DeadCodeEliminationPass(program);
    Shader::Optimization::CollectShaderInfoPass(program);

    fmt::print("Post passes\n\n{}\n", Shader::IR::DumpProgram(program));
    std::fflush(stdout);

    return program;
}

} // namespace Shader
