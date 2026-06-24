// generation/gpu_terrain/gpu_launch.cuh
#include "world/chunk.hpp"
#include <cuda_runtime.h>

void launchChunkGenGPU(
    BlockType* d_blocks,
    int originX, int originY, int originZ, 
    cudaStream_t stream);