// generation/gpu_terrain/gpu_launch.cu
#include "generation/gpu_terrain/gpu_launch.cuh"
#include "generation/gpu_terrain/kernel.cuh"
#include "generation/terrain_detail.hpp"
#include "settings/settings.hpp"
#include <math.h>

void launchChunkGenGPU(
    BlockType* d_blocks, GpuColumn* d_cols, float* d_n1, float* d_n2,
    int originX, int originY, int originZ, cudaStream_t stream)
{
    using namespace TerrainSettings;
    constexpr int W = CHUNK_SIZE;

    // Same chunk-level skip the CPU uses in generateTerrain.
    const int maxSurfaceY = BASE_HEIGHT
                        + (int)HEIGHT_SCALE
                        + (int)HYBRID_AMP
                        + (int)RIDGE_AMP
                        + 16;
    if (originY > maxSurfaceY) {
        cudaMemsetAsync(d_blocks, 0, CHUNK_VOLUME * sizeof(BlockType), stream);
        return;
    }
    const bool doCaves = (originY < maxSurfaceY);

    // Phase 1 — column cache (1 thread per column)
    {
        dim3 t(8, 8);
        dim3 b((W + t.x - 1) / t.x, (W + t.y - 1) / t.y);
        columnKernel<<<b, t, 0, stream>>>(d_cols, originX, originZ);
    }

    // Phase 2 — cave lattice (1 thread per lattice point), only when needed
    if (doCaves) {
        dim3 t(8, 8, 4);
        dim3 b((CAVE_LX + t.x - 1) / t.x,
               (CAVE_LZ + t.y - 1) / t.y,
               (CAVE_LY + t.z - 1) / t.z);
        caveLatticeKernel<<<b, t, 0, stream>>>(d_n1, d_n2, originX, originY, originZ);
    }

    // Phase 3 — voxels (1 thread per block)
    {
        dim3 t(8, 8, 4);
        dim3 b((W + t.x - 1) / t.x,
                (W + t.y - 1) / t.y,
                (W + t.z - 1) / t.z);
        densityKernel<<<b, t, 0, stream>>>(
            d_blocks, d_cols, d_n1, d_n2,
            originX, originY, originZ, doCaves ? 1 : 0);
    }
}