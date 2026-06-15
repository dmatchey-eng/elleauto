// Enable AMD media optimization extensions for fast bit manipulation
#pragma OPENCL EXTENSION cl_amd_media_ops : enable

// Helper macro for Blake2b style mixing/rotation
#define ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

__kernel void autolykos_search(
    __global const ulong4* dag_part1,       // Vectorized 256-bit read window 1
    __global const ulong4* dag_part2,       // Vectorized 256-bit read window 2
    const ulong total_vector_elements,      // Total elements per split segment
    const ulong header_hash,                // Block header pass-by-value
    const ulong target_difficulty,          // Target network threshold bound
    const ulong nonce_start,                // Baseline search range offset
    __global ulong* output_found_nonces,
    __global uint* output_counter) 
{
    uint gid = get_global_id(0);
    ulong current_nonce = nonce_start + gid;

    // STEP 1: Compute Initial Hashing State (Pre-Mix)
    // Simulating the Blake2b initial state mixing header and nonce
    ulong state0 = header_hash ^ 0x6A09E667F3BCC908ULL;
    ulong state1 = current_nonce ^ 0xBB67AE8584CAA73BULL;
    
    // Mix the initial values using fast hardware rotations
    state0 += state1;
    state1 = ROTR64(state1 ^ state0, 32);
    state0 = (state0 << 13) | (state0 >> 51);

    // STEP 2: The Memory-Hard Lookups Loop
    // Autolykos requires hopping around the massive DAG to prevent ASIC optimization
    ulong mix_state = state0 ^ state1;
    
    // We unroll a 4-step lookup window to hide VRAM bus latency on Polaris
    #pragma unroll 4
    for (int i = 0; i < 4; i++) {
        // Derive pseudo-random array index based on current hash state
        ulong target_index = (mix_state ^ i) % (total_vector_elements * 2);
        ulong4 dag_chunk;

        // Structured execution layout: All threads in the 64-warp branch uniformly
        if (target_index < total_vector_elements) {
            dag_chunk = dag_part1[target_index];
        } else {
            dag_chunk = dag_part2[target_index - total_vector_elements];
        }

        // Mix data lanes sequentially into state accumulator
        mix_state ^= dag_chunk.s0 + i;
        mix_state = ROTR64(mix_state ^ dag_chunk.s1, 24);
        mix_state ^= dag_chunk.s2;
        mix_state = ROTR64(mix_state, 16);
        mix_state += dag_chunk.s3;
    }

    // STEP 3: Final Blake2b Finalization Mix
    ulong final_hash = ROTR64(mix_state, 63) ^ 0x3C6EF372FE94F82BULL;

    // STEP 4: Nonce Target Evaluation Boundary Check
    if (final_hash < target_difficulty) {
        // Safely declare found match to host thread via cross-compute unit atomics
        uint share_slot = atomic_inc(output_counter);
        if (share_slot < 10) {
            output_found_nonces[share_slot] = current_nonce;
        }
    }
}
