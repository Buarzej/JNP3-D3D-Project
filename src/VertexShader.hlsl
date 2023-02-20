cbuffer vs_const_buffer_t {
	float4x4 matWorldViewProj;
	float4 padding[12];
}; 

struct vs_output_t {
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

vs_output_t main(float3 pos : POSITION, float2 uv : TEXCOORD) {
	vs_output_t result;
	result.position = mul(float4(pos, 1.0f), matWorldViewProj);
	result.uv = uv;
	return result;
}