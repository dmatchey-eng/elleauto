// Enable AMD media optimization extensions for fast bit manipulation
#pragma OPENCL EXTENSION cl_amd_media_ops : enable

// Native 64-bit integer bitwise cyclic rotation macro targeting GCN architecture
#define ROTATE_LEFT64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

// Blake2b Core Hashing Mixing Function
void blake2b_mix(ulong* va, ulong* vb, ulong* vc, ulong* vd, ulong x, ulong y) {
    *va = *va + *vb + x;
    *vd = ROTATE_LEFT64(*vd ^ *va, 32ULL);
    *vc = *vc + *vd;
    *vb = ROTATE_LEFT64(*vb ^ *vc, 40ULL);
    *va = *va + *vb + y;
    *vd = ROTATE_LEFT64(*vd ^ *va, 48ULL);
    *vc = *vc + *vd;
    *vb = ROTATE_LEFT64(*vb ^ *vc, 1ULL);
}

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

    // STEP 1: Initialize Blake2b-256 State Matrices using IV constants
    ulong state0 = 0x6A09E667F3BCC908ULL ^ header_hash;
    ulong state1 = 0xBB67AE8584CAA73BULL ^ current_nonce;
    ulong state2 = 0x3C6EF372FE94F82BULL;
    ulong state3 = 0xA54FF53A5F1D36F1ULL;

    // Run primary compression mixing block
    blake2b_mix(&state0, &state1, &state2, &state3, header_hash, current_nonce);
    blake2b_mix(&state0, &state2, &state1, &state3, current_nonce, header_hash);

    // Compress active state matrices down into a unified 64-bit lookup seed pointer
    ulong mix_state = state0 ^ state1 ^ state2 ^ state3;

    // STEP 2: The 32-Round Memory-Hard Lookups Loop
    // This loop forces continuous asynchronous VRAM transactions over the 256-bit bus line
    for (int i = 0; i < 32; i++) {
        // Derive a pseudo-random memory index pointer from the shifting hash state
        ulong target_index = (mix_state ^ i) % (total_vector_elements * 2);
        ulong4 dag_chunk;

        // Structured branching execution path layout
        if (target_index < total_vector_elements) {
            dag_chunk = dag_part1[target_index];
        } else {
            dag_chunk = dag_part2[target_index - total_vector_elements];
        }

        // Mix data lanes sequentially back into state accumulator using rotations
        mix_state ^= dag_chunk.s0 + i;
        mix_state = ROTATE_LEFT64(mix_state ^ dag_chunk.s1, 40ULL);
        mix_state ^= dag_chunk.s2;
        mix_state = ROTATE_LEFT64(mix_state, 48ULL);
        mix_state += dag_chunk.s3;
    }

    // STEP 3: Cryptographic Finalization Mix
    ulong final_hash = ROTATE_LEFT64(mix_state, 1ULL) ^ 0x510E527FADE682D1ULL;

    // STEP 4: Authenticated Target Evaluation
    // A solution is valid only if final_hash falls below the derived pool ceiling difficulty
    if (final_hash < target_difficulty) {
        // Safely declare found match to host thread via cross-compute unit atomics
        uint share_slot = atomic_inc(output_counter);
        if (share_slot < 10) {
            output_found_nonces[share_slot] = current_nonce;
        }
    }
}
