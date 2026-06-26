// generation/gpu_terrain/gpu_launch.cuh
#pragma once
#include "world/chunk.hpp"
#include "generation/gpu_terrain/kernel.cuh"   // GpuColumn
#include <cuda_runtime.h>

void launchChunkGenGPU(
    BlockType*  d_blocks,
    GpuColumn*  d_cols,
    float*      d_n1,
    float*      d_n2,
    int originX, int originY, int originZ,
    cudaStream_t stream);