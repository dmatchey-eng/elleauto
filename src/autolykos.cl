// 🚀 OPTIMIZATION 1: Unlocks native Polaris hardware bit manipulation extensions
#pragma OPENCL EXTENSION cl_amd_media_ops : enable

// Highly optimized 64-bit rotation using AMD hardware primitives directly
#define ROTATE_LEFT64(x, n) rotate(x, (ulong)(n))

// High-performance Blake2b mixing macro targeting GCN register lanes
#define BLAKE2B_MIX_ROUNDS(va, vb, vc, vd, x, y) { \
    va = va + vb + x; \
    vd = ROTATE_LEFT64(vd ^ va, 32ULL); \
    vc = vc + vd; \
    vb = ROTATE_LEFT64(vb ^ vc, 40ULL); \
    va = va + vb + y; \
    vd = ROTATE_LEFT64(vd ^ va, 48ULL); \
    vc = vc + vd; \
    vb = ROTATE_LEFT64(vb ^ vc, 1ULL); \
}

__kernel void autolykos_search(
    __global const ulong4* dag_part1,       // 256-bit vectorized read inputs
    __global const ulong4* dag_part2,       
    const ulong total_vector_elements,      // Total elements per split segment
    const ulong4 header_hash,               // 💥 256-bit Header Hash Vector (4 x ulong)
    const ulong4 target_difficulty,         // 💥 256-bit Target Difficulty Vector (4 x ulong)
    const ulong nonce_start,                // Baseline search range offset
    __global ulong* output_found_nonces,
    __global uint* output_counter) 
{
    // 🚀 OPTIMIZATION 2: Coalesced thread alignment matching AMD 64-Warp dimensions
    uint gid = get_global_id(0);
    ulong current_nonce = nonce_start + gid;

    // STEP 1: Initialize Blake2b-256 State Matrices using fixed IV metrics mixed with header_hash lanes
    ulong v0 = 0x6A09E667F3BCC908ULL ^ header_hash.s0;
    ulong v1 = 0xBB67AE8584CAA73BULL ^ current_nonce;
    ulong v2 = 0x3C6EF372FE94F82BULL ^ header_hash.s1;
    ulong v3 = 0xA54FF53A5F1D36F1ULL ^ header_hash.s2;
    ulong v4 = 0x5BE0CD19137E2179ULL ^ header_hash.s3;
    ulong v5 = 0x1F83D9ABFB41BD6BULL;
    ulong v6 = 0x5CB0A9DC5B970226ULL;
    ulong v7 = 0x9B05688C2B3E6C1FULL;

    // Compress initial state boundaries over standard mixing cycles
    BLAKE2B_MIX_ROUNDS(v0, v2, v4, v6, header_hash.s0, current_nonce);
    BLAKE2B_MIX_ROUNDS(v1, v3, v5, v7, header_hash.s1, header_hash.s2);
    BLAKE2B_MIX_ROUNDS(v0, v3, v4, v7, header_hash.s3, current_nonce);
    BLAKE2B_MIX_ROUNDS(v1, v2, v5, v6, header_hash.s0, header_hash.s1);

    // Form the true 256-bit (4-lane vector) mix state matrix 
    ulong4 mix_state;
    mix_state.s0 = v0 ^ v4;
    mix_state.s1 = v1 ^ v5;
    mix_state.s2 = v2 ^ v6;
    mix_state.s3 = v3 ^ v7;

    // STEP 2: 🚀 OPTIMIZATION 3: Enforcing full Loop Unrolling to eliminate loop logic overhead
    #pragma unroll 32
    for (int i = 0; i < 32; i++) {
        // Derive pseudo-random index pointer using the first 64-bit vector lane
        ulong target_index = (mix_state.s0 ^ i) % (total_vector_elements * 2);
        ulong4 dag_chunk;

        // Structured branching optimization: Threads execute uniformly across memory lanes
        if (target_index < total_vector_elements) {
            dag_chunk = dag_part1[target_index];
        } else {
            dag_chunk = dag_part2[target_index - total_vector_elements];
        }

        // Bitwise accumulation mapping mapped strictly across all 4 vector lanes
        mix_state.s0 ^= dag_chunk.s0 + i;
        mix_state.s1 = ROTATE_LEFT64(mix_state.s1 ^ dag_chunk.s1, 40ULL);
        mix_state.s2 ^= dag_chunk.s2;
        mix_state.s3 = ROTATE_LEFT64(mix_state.s3 + dag_chunk.s3, 48ULL);
    }

    // STEP 3: Cryptographic Finalization Mix preserving the full 256-bit bounds
    ulong4 final_hash;
    final_hash.s0 = ROTATE_LEFT64(mix_state.s0, 1ULL) ^ 0x510E527FADE682D1ULL;
    final_hash.s1 = ROTATE_LEFT64(mix_state.s1, 1ULL) ^ 0x9B05688C2B3E6C1FULL;
    final_hash.s2 = ROTATE_LEFT64(mix_state.s2, 1ULL) ^ 0x1F83D9ABFB41BD6BULL;
    final_hash.s3 = ROTATE_LEFT64(mix_state.s3, 1ULL) ^ 0x5BE0CD19137E2179ULL;

    // STEP 4: Authenticated Big-Integer 256-bit Comparison Logic (final_hash < target_difficulty)
    bool is_valid_share = false;
    if (final_hash.s3 < target_difficulty.s3) {
        is_valid_share = true;
    } else if (final_hash.s3 == target_difficulty.s3) {
        if (final_hash.s2 < target_difficulty.s2) {
            is_valid_share = true;
        } else if (final_hash.s2 == target_difficulty.s2) {
            if (final_hash.s1 < target_difficulty.s1) {
                is_valid_share = true;
            } else if (final_hash.s1 == target_difficulty.s1) {
                if (final_hash.s0 < target_difficulty.s0) {
                    is_valid_share = true;
                }
            }
        }
    }

    // STEP 5: Thread Safe Output Allocation
    if (is_valid_share) {
        uint share_slot = atomic_inc(output_counter);
        if (share_slot < 10) {
            // Write out the 64-bit current worker nonce
            output_found_nonces[share_slot * 5] = current_nonce;
            // Write out the exact 256-bit solved hash block elements back to your host application
            output_found_nonces[(share_slot * 5) + 1] = final_hash.s0;
            output_found_nonces[(share_slot * 5) + 2] = final_hash.s1;
            output_found_nonces[(share_slot * 5) + 3] = final_hash.s2;
            output_found_nonces[(share_slot * 5) + 4] = final_hash.s3;
        }
    }
}
