// noise.h
#pragma once
#include "device/host_device.hpp"
#include <math.h>

struct Vec3 { float x, y, z; };

#ifdef __CUDACC__
    static __device__ __constant__ Vec3 kGrads[12] = {
        { 1, 1, 0}, {-1, 1, 0}, { 1,-1, 0}, {-1,-1, 0},
        { 1, 0, 1}, {-1, 0, 1}, { 1, 0,-1}, {-1, 0,-1},
        { 0, 1, 1}, { 0,-1, 1}, { 0, 1,-1}, { 0,-1,-1}
    };
#else
    static const Vec3 kGrads[12] = {
        { 1, 1, 0}, {-1, 1, 0}, { 1,-1, 0}, {-1,-1, 0},
        { 1, 0, 1}, {-1, 0, 1}, { 1, 0,-1}, {-1, 0,-1},
        { 0, 1, 1}, { 0,-1, 1}, { 0, 1,-1}, { 0,-1,-1}
    };
#endif

HD FINLINE unsigned int ihash3(int x, int y, int z) {
    unsigned int h = 2166136261u;
    h ^= (unsigned int)x; h *= 16777619u;
    h ^= (unsigned int)y; h *= 16777619u;
    h ^= (unsigned int)z; h *= 16777619u;
    return h;
}

HD FINLINE float fade(float t) { return t*t*t*(t*(t*6.0f-15.0f)+10.0f); }

HD FINLINE float gradF(int ix, int iy, int iz, float fx, float fy, float fz) {
    unsigned int h = ihash3(ix, iy, iz) % 12;
    Vec3 g = kGrads[h];
    return g.x*fx + g.y*fy + g.z*fz;
}

HD FINLINE float perlinNoise3D(float x, float y, float z) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x-ix, fy = y-iy, fz = z-iz;
    float ux = fade(fx), uy = fade(fy), uz = fade(fz);

    float v000 = gradF(ix,   iy,   iz,   fx,   fy,   fz);
    float v100 = gradF(ix+1, iy,   iz,   fx-1, fy,   fz);
    float v010 = gradF(ix,   iy+1, iz,   fx,   fy-1, fz);
    float v110 = gradF(ix+1, iy+1, iz,   fx-1, fy-1, fz);
    float v001 = gradF(ix,   iy,   iz+1, fx,   fy,   fz-1);
    float v101 = gradF(ix+1, iy,   iz+1, fx-1, fy,   fz-1);
    float v011 = gradF(ix,   iy+1, iz+1, fx,   fy-1, fz-1);
    float v111 = gradF(ix+1, iy+1, iz+1, fx-1, fy-1, fz-1);

    float x00 = v000 + ux*(v100-v000);
    float x10 = v010 + ux*(v110-v010);
    float x01 = v001 + ux*(v101-v001);
    float x11 = v011 + ux*(v111-v011);
    float y0  = x00  + uy*(x10-x00);
    float y1  = x01  + uy*(x11-x01);
    return (y0 + uz*(y1-y0)) * 0.5f + 0.5f;
}

HD FINLINE float perlinNoise2D(float x, float z) {
    return perlinNoise3D(x, 0.0f, z);
}

HD FINLINE float fbm3D(float x, float y, float z, int octaves, float lacunarity, float gain) {
    float value = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        value += amp * perlinNoise3D(x*freq, y*freq, z*freq);
        freq *= lacunarity; amp *= gain;
    }
    return value;
}

HD FINLINE float fbm2D(float x, float z, int octaves, float lacunarity, float gain) {
    float value = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        value += amp * perlinNoise2D(x*freq, z*freq);
        freq *= lacunarity; amp *= gain;
    }
    return value;
}

HD FINLINE float warpedNoise2D(float x, float z, float warpAmp) {
    float ox = perlinNoise2D(x + 1.7f, z + 9.2f) * warpAmp;
    float oz = perlinNoise2D(x + 8.3f, z + 2.8f) * warpAmp;
    return perlinNoise2D(x + ox, z + oz);
}

HD FINLINE float warpedFbm3D(float x, float y, float z, int oct, float lac, float gain, float warpAmp) {
    float ox = fbm3D(x+1.7f, y+9.2f, z+3.4f, 2, lac, gain) * warpAmp;
    float oy = fbm3D(x+8.3f, y+2.8f, z+5.1f, 2, lac, gain) * warpAmp;
    float oz = fbm3D(x+5.1f, y+6.7f, z+1.3f, 2, lac, gain) * warpAmp;
    return fbm3D(x+ox, y+oy, z+oz, oct, lac, gain);
}

HD FINLINE float worleyNoise3D(float x, float y, float z) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float minDist = 1e9f;
    for (int dz=-1; dz<=1; ++dz)
    for (int dy=-1; dy<=1; ++dy)
    for (int dx=-1; dx<=1; ++dx) {
        int cx=ix+dx, cy=iy+dy, cz=iz+dz;
        unsigned int h = ihash3(cx, cy, cz);
        float px = cx + ((h      & 0xFFu) * (1.0f/255.0f));
        float py = cy + ((h>>8   & 0xFFu) * (1.0f/255.0f));
        float pz = cz + ((h>>16  & 0xFFu) * (1.0f/255.0f));
        float ddx=x-px, ddy=y-py, ddz=z-pz;
        float d = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz);
        if (d < minDist) minDist = d;
    }
    return fminf(minDist * 1.4142f, 1.0f);
}

// Trilinear interpolation
HD FINLINE float trilerp(
    float c000, float c100, float c010, float c110,
    float c001, float c101, float c011, float c111,
    float tx,   float ty,   float tz)
{
    float c00 = c000 + (c100 - c000) * tx;
    float c10 = c010 + (c110 - c010) * tx;
    float c01 = c001 + (c101 - c001) * tx;
    float c11 = c011 + (c111 - c011) * tx;
    float c0  = c00  + (c10  - c00 ) * ty;
    float c1  = c01  + (c11  - c01 ) * ty;
    return       c0  + (c1   - c0  ) * tz;
}