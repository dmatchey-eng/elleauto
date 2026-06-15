// Force the AMD opencl compiler to prioritize raw loop unrolling and inline extensions
#pragma OPENCL EXTENSION cl_amd_media_ops : enable

__kernel void autolykos_search(
    __global const ulong4* dag_part1,       // 256-bit vectorized read inputs
    __global const ulong4* dag_part2,       
    const ulong half_dag_elements,          // Total element split tracking bounds
    __global const ulong* header_hash,
    const ulong target_difficulty,
    const ulong nonce_start,
    __global ulong* output_found_nonces,
    __global uint* output_counter) 
{
    // 1. Calculate thread position within the AMD 64-thread Wavefront
    uint gid = get_global_id(0);
    ulong current_nonce = nonce_start + gid;

    // 2. Vectorized coalesced index mapping to maximize 256-bit bus line access
    // This ensures all 64 items in the warp request continuous sequential addresses
    ulong total_elements = half_dag_elements >> 2; // Adjusted for ulong4 vector size (4 elements per fetch)
    ulong target_vector_index = gid % (total_elements * 2);

    ulong4 current_dag_chunk;

    // 3. Structured Branching: All items in a wavefront group pull down a singular branch path
    if (target_vector_index < total_elements) {
        current_dag_chunk = dag_part1[target_vector_index];
    } else {
        current_dag_chunk = dag_part2[target_vector_index - total_elements];
    }

    // 4. Optimization: Unrolled bitwise hashing instead of generic loop structures
    // Mixing the header hash against the vector dataset channels
    ulong hash_mix_result = current_dag_chunk.s0 ^ header_hash[0];
    hash_mix_result      += current_dag_chunk.s1 ^ current_nonce;
    hash_mix_result      = (hash_mix_result << 13) | (hash_mix_result >> 51); // Fast bitwise rotate
    hash_mix_result      ^= current_dag_chunk.s2 + current_dag_chunk.s3;

    // 5. Atomic Solution Verification Check
    if (hash_mix_result < target_difficulty) {
        // Use an atomic increment to safely register found nonces across active compute units
        uint share_slot = atomic_inc(output_counter);
        if (share_slot < 10) { // Bound safeguard check
            output_found_nonces[share_slot] = current_nonce;
        }
    }
}
