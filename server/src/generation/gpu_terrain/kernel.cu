// kernel.cu
#include "generation/gpu_terrain/kernel.cuh"
#include "generation/terrain_detail.hpp"        // CAVE_STEP_*, CAVE_LX/LY/LZ
#include "generation/device_shared/noise.hpp"
#include "generation/device_shared/spline.hpp"
#include "generation/device_shared/biome_rules.hpp"
#include "generation/device_shared/column_field.hpp"
#include "world/chunk.hpp"

#include <stdexcept>

__constant__ TerrainParams gTerrainParams;

void uploadTerrainParams(const TerrainParams& p) {
    cudaError_t err = cudaMemcpyToSymbol(
        gTerrainParams, &p, sizeof(TerrainParams), 0, cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
        throw std::runtime_error(std::string("uploadTerrainParams failed: ") + cudaGetErrorString(err));
}

// Flat index into the sparse cave lattice (matches buildCaveLattice / trilerp call order)
__device__ __forceinline__
int latIdx(int gy, int gz, int gx) {
    return (gy * CAVE_LZ + gz) * CAVE_LX + gx;
}

// =====================================================================
// PHASE 1 — column cache
// =====================================================================
__global__
void columnKernel(GpuColumn* cols, int originX, int originZ) {
    int lx = blockIdx.x * blockDim.x + threadIdx.x;
    int lz = blockIdx.y * blockDim.y + threadIdx.y;
    if (lx >= CHUNK_SIZE || lz >= CHUNK_SIZE) return;
    
    const TerrainParams& T = gTerrainParams;
    ColumnParams P;
    P.continentScale        = T.continentScale;
    P.heightScale           = T.heightScale;
    P.baseHeight            = T.baseHeight;
    P.waterLevel            = T.waterLevel;
    P.mfLacunarity          = T.mfLacunarity;
    P.mfOctaves             = T.mfOctaves;
    P.hybridH               = T.hybridH;
    P.hybridOffset          = T.hybridOffset;
    P.hybridScale           = T.hybridScale;
    P.hybridAmp             = T.hybridAmp;
    P.ridgeH                = T.ridgeH;
    P.ridgeOffset           = T.ridgeOffset;
    P.ridgeGain             = T.ridgeGain;
    P.ridgeScale            = T.ridgeScale;
    P.ridgeAmp              = T.ridgeAmp;
    P.mountainMaskScale     = T.mountainMaskScale;
    P.mountainMaskBias      = T.mountainMaskBias;
    P.riverScale            = T.riverScale;
    P.riverWidth            = T.riverWidth;
    P.biomeScale            = T.biomeScale;
    float wx = (float)(originX + lx);
    float wz = (float)(originZ + lz);
    ColumnResult cr = computeColumn(wx, wz, P);

    GpuColumn gc;
    gc.surfaceY = cr.surfaceY;
    gc.biome    = (int)cr.biome;
    cols[lz * CHUNK_SIZE + lx] = gc;
}

// =====================================================================
// PHASE 2 — cave lattice
// =====================================================================
__global__
void caveLatticeKernel(float* n1, float* n2, int originX, int originY, int originZ) {
    const TerrainParams& P = gTerrainParams;

    int gx = blockIdx.x * blockDim.x + threadIdx.x;
    int gz = blockIdx.y * blockDim.y + threadIdx.y;
    int gy = blockIdx.z * blockDim.z + threadIdx.z;
    if (gx >= CAVE_LX || gz >= CAVE_LZ || gy >= CAVE_LY) return;

    const float wormFreq = P.caveBaseFreq * 0.2f;
    float wx = (float)(originX + gx * CAVE_STEP_XZ);
    float wy = (float)(originY + gy * CAVE_STEP_Y);
    float wz = (float)(originZ + gz * CAVE_STEP_XZ);

    int i = latIdx(gy, gz, gx);
    n1[i] = warpedFbm3D(wx * wormFreq, wy * wormFreq, wz * wormFreq,
                        P.caveOctaves, P.caveLacunarity, P.caveGain, 6.0f);
    n2[i] = warpedFbm3D(wx * wormFreq + 31.7f, wy * wormFreq + 17.3f, wz * wormFreq + 53.1f,
                        P.caveOctaves, P.caveLacunarity, P.caveGain, 6.0f);
}

// =====================================================================
// PHASE 3 — voxels
// =====================================================================
__global__
void densityKernel(BlockType* blocks, const GpuColumn* cols,
                   const float* n1, const float* n2,
                   int originX, int originY, int originZ, int doCaves) {
    const TerrainParams& P = gTerrainParams;

    int lx = blockIdx.x * blockDim.x + threadIdx.x;
    int lz = blockIdx.y * blockDim.y + threadIdx.y;
    int ly = blockIdx.z * blockDim.z + threadIdx.z;
    if (lx >= CHUNK_SIZE || lz >= CHUNK_SIZE || ly >= CHUNK_SIZE) return;

    int idx = lx + CHUNK_SIZE * (lz + CHUNK_SIZE * ly);   // unchanged layout

    int   wy   = originY + ly;
    float wy_f = (float)wy;
    float wx   = (float)(originX + lx);
    float wz   = (float)(originZ + lz);

    const GpuColumn col = cols[lz * CHUNK_SIZE + lx];

    // 3D surface perturbation — the only genuinely per-voxel field
    float pFreq   = P.heightBaseFreq * 3.0f;
    float perturb = fbm3D(wx * pFreq, wy_f * pFreq, wz * pFreq, 3, 1.8f, 0.5f);
    perturb = (perturb - 0.5f) * 12.0f;

    float density = (col.surfaceY + perturb) - wy_f;

    BlockType block;
    if (density <= 0.0f) {
        block = (wy <= P.waterLevel) ? BlockType::Water : BlockType::Air;
    } else {
        int  depth        = (int)floorf(density);
        bool isUnderwater = (wy <= P.waterLevel);
        block = biomeBlock((Biome)col.biome, depth, (depth == 0), isUnderwater, wy, P.waterLevel);
    }

    // ---- Cave carving (identical to CPU) ----
    if (doCaves && block != BlockType::Air && block != BlockType::Water && wy > 1) {
        if (wy < P.waterLevel - 3 || wy > P.waterLevel + 3) {
            const float cf         = P.caveBaseFreq;
            const float chamber    = worleyNoise3D(wx * cf, wy_f * cf, wz * cf);
            const float depthBelow = (float)P.baseHeight - wy_f;
            const bool  isChambered = (depthBelow > 30.0f) && (chamber < 0.12f);

            const int   gy0 = ly / CAVE_STEP_Y;
            const int   gy1 = min(gy0 + 1, CAVE_LY - 1);
            const float tyF = (float)(ly % CAVE_STEP_Y) / (float)CAVE_STEP_Y;
            const int   gz0 = lz / CAVE_STEP_XZ;
            const int   gz1 = min(gz0 + 1, CAVE_LZ - 1);
            const float tzF = (float)(lz % CAVE_STEP_XZ) / (float)CAVE_STEP_XZ;
            const int   gx0 = lx / CAVE_STEP_XZ;
            const int   gx1 = min(gx0 + 1, CAVE_LX - 1);
            const float txF = (float)(lx % CAVE_STEP_XZ) / (float)CAVE_STEP_XZ;

            // Same argument order as the CPU trilerp call — bit-for-bit parity.
            const float vn1 = trilerp(
                n1[latIdx(gy0,gz0,gx0)], n1[latIdx(gy0,gz0,gx1)],
                n1[latIdx(gy0,gz1,gx0)], n1[latIdx(gy0,gz1,gx1)],
                n1[latIdx(gy1,gz0,gx0)], n1[latIdx(gy1,gz0,gx1)],
                n1[latIdx(gy1,gz1,gx0)], n1[latIdx(gy1,gz1,gx1)],
                txF, tyF, tzF);
            const float vn2 = trilerp(
                n2[latIdx(gy0,gz0,gx0)], n2[latIdx(gy0,gz0,gx1)],
                n2[latIdx(gy0,gz1,gx0)], n2[latIdx(gy0,gz1,gx1)],
                n2[latIdx(gy1,gz0,gx0)], n2[latIdx(gy1,gz0,gx1)],
                n2[latIdx(gy1,gz1,gx0)], n2[latIdx(gy1,gz1,gx1)],
                txF, tyF, tzF);

            const float wormR = P.caveWormRadius;
            if (isChambered || (fabsf(vn1 - 0.5f) < wormR && fabsf(vn2 - 0.5f) < wormR))
                block = BlockType::Air;
        }
    }

    blocks[idx] = block;
}