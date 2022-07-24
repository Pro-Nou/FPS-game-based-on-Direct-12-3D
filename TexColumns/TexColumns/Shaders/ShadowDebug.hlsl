//***************************************************************************************
// ShadowDebug.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

// Include common HLSL code.

//#include "Common.hlsl"
Texture2D gSsaoMap   : register(t0);

SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

//// Constant data that varies per frame.
//cbuffer cbPerObject : register(b0)
//{
//    float4x4 gWorld;
//    float4x4 gTexTransform;
//};
//
//// Constant data that varies per material.
//cbuffer cbPass : register(b1)
//{
//    float4x4 gView;
//    float4x4 gInvView;
//    float4x4 gProj;
//    float4x4 gInvProj;
//    float4x4 gViewProj;
//    float4x4 gInvViewProj;
//    float3 gEyePosW;
//    float cbPerObjectPad1;
//    float2 gRenderTargetSize;
//    float2 gInvRenderTargetSize;
//    float gNearZ;
//    float gFarZ;
//    float gTotalTime;
//    float gDeltaTime;
//    float4 gAmbientLight;
//
//    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
//    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
//    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
//    // are spot lights for a maximum of MaxLights per object.
//    Light gLights[MaxLights];
//};
//
//cbuffer cbMaterial : register(b2)
//{
//    float4   gDiffuseAlbedo;
//    float3   gFresnelR0;
//    float    gRoughness;
//    float4x4 gMatTransform;
//};


struct VertexIn
{
	float3 PosL    : POSITION;
	float3 Norm	   : NORMAL;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float3 Norm	   : NORMAL;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

    // Already in homogeneous clip space.
    vout.PosH = float4(vin.PosL, 1.000001f);
	
	vout.TexC = vin.TexC;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return float4(gSsaoMap.Sample(gsamLinearWrap, pin.TexC).rgb, 1.0f);
}


