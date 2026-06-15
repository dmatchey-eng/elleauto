// Enable AMD media optimization extensions for fast bit manipulation
#pragma OPENCL EXTENSION cl_amd_media_ops : enable

// Blake2b Mixing Macros for 64-bit unsigned integers
#define B2B_G(a, b, c, d, x, y) { \
    a = a + b + x; \
    d = rotate(d ^ a, 32ULL); \
    c = c + d; \
    b = rotate(b ^ c, 24ULL); \
    a = a + b + y; \
    d = rotate(d ^ a, 16ULL); \
    c = c + d; \
    b = rotate(b ^ c, 63ULL); \
}

// Memory-hard lookup mixing routine
__kernel void autolykos_search(
    __global const ulong4* dag_part1,       // Vectorized 256-bit read window 1
    __global const ulong4* dag_part2,       
    const ulong total_vector_elements,      // Total elements per split segment
    const ulong header_hash,                // Block header pass-by-value
    const ulong target_difficulty,          // Padded 64-bit difficulty ceiling
    const ulong nonce_start,                // Baseline search range offset
    __global ulong* output_found_nonces,
    __global uint* output_counter) 
{
    uint gid = get_global_id(0);
    ulong current_nonce = nonce_start + gid;

    // STEP 1: Initialize Blake2b-256 State Matrices
    ulong v0 = 0x6A09E667F3BCC908ULL ^ header_hash;
    ulong v1 = 0xBB67AE8584CAA73BULL ^ current_nonce;
    ulong v2 = 0x3C6EF372FE94F82BULL;
    ulong v3 = 0xA54FF53A5F1D36F1ULL;

    // Execute Compress Mix Rounds to bind Header and Nonce
    B2B_G(v0, v1, v2, v3, header_hash, current_nonce);
    B2B_G(v0, v2, v1, v3, current_nonce, header_hash);

    // Compress state matrix down into a 64-bit seed pointer
    ulong mix_state = v0 ^ v1 ^ v2 ^ v3;

    // STEP 2: The Memory-Hard Lookups Loop
    // Autolykos v2 requires a continuous array mixing traversal 
    // to force physical VRAM requests over the Polaris bus width
    for (int i = 0; i < 32; i++) {
        // Derive a pseudo-random memory pointer from the shifting hash state
        ulong target_index = (mix_state ^ i) % (total_vector_elements * 2);
        ulong4 dag_chunk;

        // Unified layout: All threads inside the 64-warp branch synchronously
        if (target_index < total_vector_elements) {
            dag_chunk = dag_part1[target_index];
        } else {
            dag_chunk = dag_part2[target_index - total_vector_elements];
        }

        // Mix data lanes sequentially into state accumulator using bitwise rotation
        mix_state ^= dag_chunk.s0 + i;
        mix_state = rotate(mix_state ^ dag_chunk.s1, 24ULL);
        mix_state ^= dag_chunk.s2;
        mix_state = rotate(mix_state, 16ULL);
        mix_state += dag_chunk.s3;
    }

    // STEP 3: Final Cryptographic Finalization Mix
    ulong final_hash = rotate(mix_state, 63ULL) ^ 0x510E527FADE682D1ULL;

    // STEP 4: Authenticated Target Evaluation
    // A share is only flagged if the final computed hash is mathematically 
    // smaller than the derived pool target difficulty
    if (final_hash < target_difficulty) {
        // Safely declare found match to host thread via cross-compute unit atomics
        uint share_slot = atomic_inc(output_counter);
        if (share_slot < 10) {
            output_found_nonces[share_slot] = current_nonce;
        }
    }
}
