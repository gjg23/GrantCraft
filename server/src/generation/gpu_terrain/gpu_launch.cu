// generation/gpu_terrain/gpu_launch.cu
#include "generation/gpu_terrain/gpu_launch.cuh"
#include "generation/gpu_terrain/kernel.cuh"
#include <math.h>

void launchChunkGenGPU(
    BlockType* d_blocks,
    int originX, int originY, int originZ, 
    cudaStream_t stream)
{
    constexpr int W = CHUNK_SIZE;

    dim3 threads(8, 8, 4);
    dim3 blocks((W + threads.x - 1) / threads.x,
                (W + threads.y - 1) / threads.y,
                (W + threads.z - 1) / threads.z);

    terrainKernel<<<blocks, threads, 0, stream>>>(
        d_blocks,
        originX,
        originY,
        originZ
    );
}