#pragma once

#include "DXSample.h"
#include <directxmath.h>
#include <wincodec.h>
#include <map>
#include <vector>
#include <array>

using namespace DirectX;

using Microsoft::WRL::ComPtr;

class D3DProject : public DXSample
{
public:
	D3DProject(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

	virtual void OnKeyDown(UINT8 key);
	virtual void OnKeyUp(UINT8 key);

private:
	static const UINT FrameCount = 2;

	struct Position {
		float x, z;
	};

	struct Vertex {
		XMFLOAT3 position;
		XMFLOAT2 uv;
	};

	struct ConstantBuffer {
		XMFLOAT4X4 matWorldViewProj;
		XMFLOAT4 padding[(256 - sizeof(XMFLOAT4X4)) / sizeof(XMFLOAT4)];
	};
	static_assert((sizeof(ConstantBuffer) % 256) == 0,
		"Constant Buffer size must be 256-byte aligned");

	std::map<UINT8, bool> keyboardState;
	float angle;
	Position playerPos;

	IWICImagingFactory* IWICFactory;

	// Pipeline objects.
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12Resource> m_depthStencil;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	UINT m_rtvDescriptorSize;

	// App resources.
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	ComPtr<ID3D12Resource> m_constantBuffer;
	ConstantBuffer m_constantBufferData;
	UINT8* m_pCbvDataBegin;
	ComPtr<ID3D12Resource> m_texture;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForPreviousFrame();

	HRESULT LoadBitmapFromFile(
		PCWSTR uri, UINT& width,
		UINT& height, BYTE** ppBits);
};
