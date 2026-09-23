#include <metal_stdlib>
using namespace metal;

// rmreplay reads the triangle back; rmclient_test exercises lookup of cube_vs.
vertex float4 vmain(const device float2 *positions [[buffer(0)]],
                   uint vertex_id [[vertex_id]]) {
    return float4(positions[vertex_id], 0, 1);
}

vertex float4 cube_vs(const device float2 *positions [[buffer(0)]],
                     uint vertex_id [[vertex_id]]) {
    return float4(positions[vertex_id], 0, 1);
}

fragment float4 fmain() {
    return float4(1.0, 0.5, 0.25, 1.0);
}
