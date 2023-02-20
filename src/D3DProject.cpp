#include "D3DProject.h"
#include "vertex_shader.h"
#include "pixel_shader.h"

D3DProject::D3DProject(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	angle(0.0f), playerPos{ -0.0f, -10.0f },
	m_frameIndex(0),
	m_pCbvDataBegin(nullptr),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_rtvDescriptorSize(0),
	m_constantBufferData{} {

	// Initalize COM library.
	if (!SUCCEEDED(
		CoInitializeEx(
			NULL,
			COINIT_APARTMENTTHREADED)))
		throw std::runtime_error("Failed to initialize COM library.");

	// Create WIC factory.
	if (!SUCCEEDED(
		CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			__uuidof(IWICImagingFactory),
			reinterpret_cast<LPVOID*>(&IWICFactory))))
		throw std::runtime_error("Failed to create WIC factory.");
}

void D3DProject::OnInit() {
	LoadPipeline();
	LoadAssets();
}

void D3DProject::LoadPipeline() {
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	// Create the device.
	ThrowIfFailed(D3D12CreateDevice(
		nullptr,
		D3D_FEATURE_LEVEL_12_0,
		IID_PPV_ARGS(&m_device)
	));

	// Create the DXGI factory.
	ComPtr<IDXGIFactory7> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	// Initialize and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Initialize and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = 0; // Will be taken from hwnd.
	swapChainDesc.Height = 0; // Will be taken from hwnd.
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
	));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Create a constant buffer view (CBV)
		// and a shader resourve view (SRV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
		cbvSrvHeapDesc.NumDescriptors = 2;
		cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvSrvHeapDesc.NodeMask = 0;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap)));

		// Create a depth stencil view (DSV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.NodeMask = 0;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++) {
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}
	}

	// Create the command allocator.
	ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

void D3DProject::LoadAssets() {

	// Create a root signature.
	{
		D3D12_DESCRIPTOR_RANGE ranges[2] = {
			{
				.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
				.NumDescriptors = 1,
				.BaseShaderRegister = 0,
				.RegisterSpace = 0,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
			}, {
				.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				.NumDescriptors = 1,
				.BaseShaderRegister = 0,
				.RegisterSpace = 0,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
			}
		};

		D3D12_ROOT_PARAMETER rootParameters[] = {
			{
				.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
				.DescriptorTable = { 1, &ranges[0] },
				.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX
			}, {
				.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
				.DescriptorTable = { 1, &ranges[1] },
				.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
			}
		};

		D3D12_STATIC_SAMPLER_DESC texSamplerDesc = {
			.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.MipLODBias = 0,
			.MaxAnisotropy = 0,
			.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
			.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
			.MinLOD = 0.0f,
			.MaxLOD = D3D12_FLOAT32_MAX,
			.ShaderRegister = 0,
			.RegisterSpace = 0,
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
		};

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {
			.NumParameters = _countof(rootParameters),
			.pParameters = rootParameters,
			.NumStaticSamplers = 1,
			.pStaticSamplers = &texSamplerDesc,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
		};

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
			{
				.SemanticName = "POSITION",
				.SemanticIndex = 0,
				.Format = DXGI_FORMAT_R32G32B32_FLOAT,
				.InputSlot = 0,
				.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
				.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
				.InstanceDataStepRate = 0
			}, {
				.SemanticName = "TEXCOORD",
				.SemanticIndex = 0,
				.Format = DXGI_FORMAT_R32G32_FLOAT,
				.InputSlot = 0,
				.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
				.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
				.InstanceDataStepRate = 0
			}
		};

		// Initialize and create the graphics pipeline state object (PSO).
		D3D12_BLEND_DESC blendState = {
			.AlphaToCoverageEnable = FALSE,
			.IndependentBlendEnable = FALSE,
			.RenderTarget = {
				{
					.BlendEnable = FALSE,
					.LogicOpEnable = FALSE,
					.SrcBlend = D3D12_BLEND_ONE,
					.DestBlend = D3D12_BLEND_ZERO,
					.BlendOp = D3D12_BLEND_OP_ADD,
					.SrcBlendAlpha = D3D12_BLEND_ONE,
					.DestBlendAlpha = D3D12_BLEND_ZERO,
					.BlendOpAlpha = D3D12_BLEND_OP_ADD,
					.LogicOp = D3D12_LOGIC_OP_NOOP,
					.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
				}
			}
		};

		D3D12_RASTERIZER_DESC rasterizerState = {
			.FillMode = D3D12_FILL_MODE_SOLID,
			.CullMode = D3D12_CULL_MODE_BACK,
			.FrontCounterClockwise = FALSE,
			.DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
			.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
			.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
			.DepthClipEnable = TRUE,
			.MultisampleEnable = FALSE,
			.AntialiasedLineEnable = FALSE,
			.ForcedSampleCount = 0,
			.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = { vs_main, sizeof(vs_main) };
		psoDesc.PS = { ps_main, sizeof(ps_main) };
		psoDesc.RasterizerState = rasterizerState;
		psoDesc.BlendState = blendState;
		psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		psoDesc.DepthStencilState = {
			.DepthEnable = TRUE,
			.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
			.DepthFunc = D3D12_COMPARISON_FUNC_LESS,
			.StencilEnable = FALSE,
			.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
			.StencilWriteMask = D3D12_DEFAULT_STENCIL_READ_MASK,
			.FrontFace = {
				.StencilFailOp = D3D12_STENCIL_OP_KEEP,
				.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
				.StencilPassOp = D3D12_STENCIL_OP_KEEP,
				.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
			},
			.BackFace = {
				.StencilFailOp = D3D12_STENCIL_OP_KEEP,
				.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
				.StencilPassOp = D3D12_STENCIL_OP_KEEP,
				.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
			}
		};
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
	}

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

	// Create the vertex buffer.
	{
		Vertex vertices[] = {
			{ { -2.0f, -2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 1.0f, 0.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 1.0f, 1.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, -2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 1.0f, 1.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 1.0f, 0.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 1.0f, 1.0f } },

			{ { -2.0f, -2.0f, 4.0f }, { 0.0f, 1.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, 2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 0.0f, 1.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, 2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },

			{ { -2.0f, -2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, -2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },

			{ { 2.0f, -2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, 2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, 2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },

			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, 2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, 2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, 2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { 2.0f, 2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, 2.0f, 4.0f }, { 1.0f, 1.0f } },

			{ { -2.0f, -2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, -2.0f, 0.0f }, { 0.0f, 1.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { 2.0f, -2.0f, 4.0f }, { 1.0f, 0.0f } },
			{ { 2.0f, -2.0f, 0.0f }, { 0.0f, 0.0f } },
			{ { -2.0f, -2.0f, 4.0f }, { 1.0f, 1.0f } },
		};

		const UINT vertexBufferSize = sizeof(vertices);

		D3D12_HEAP_PROPERTIES heapProps = {
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1,
		};

		D3D12_RESOURCE_DESC resourceDesc = {
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = vertexBufferSize,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {.Count = 1, .Quality = 0 },
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE
		};

		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);
		ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, vertices, sizeof(vertices));
		m_vertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// Create the constant buffer.
	{
		const UINT constantBufferSize = sizeof(ConstantBuffer);

		D3D12_HEAP_PROPERTIES heapProps = {
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};

		D3D12_RESOURCE_DESC resourceDesc = {
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = constantBufferSize,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {.Count = 1, .Quality = 0 },
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE,
		};

		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_constantBuffer)));

		// Create a constant buffer view.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = constantBufferSize;
		m_device->CreateConstantBufferView(&cbvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

		// Map and initialize the constant buffer.
		XMStoreFloat4x4(&m_constantBufferData.matWorldViewProj, XMMatrixIdentity());
		D3D12_RANGE range(0, 0);
		ThrowIfFailed(m_constantBuffer->Map(0, &range, reinterpret_cast<void**>(&m_pCbvDataBegin)));
		memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
	}

	// Create the depth stencil view.
	{
		D3D12_HEAP_PROPERTIES heapProps = {
			.Type = D3D12_HEAP_TYPE_DEFAULT,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};

		D3D12_RESOURCE_DESC resourceDesc = {
			.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
			.Alignment = 0,
			.Width = m_width,
			.Height = m_height,
			.DepthOrArraySize = 1,
			.MipLevels = 0,
			.Format = DXGI_FORMAT_D32_FLOAT,
			.SampleDesc = {.Count = 1, .Quality = 0 },
			.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
			.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		};

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {
			.Format = DXGI_FORMAT_D32_FLOAT,
			.DepthStencil = {.Depth = 1.0f, .Stencil = 0 }
		};

		D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {
			.Format = DXGI_FORMAT_D32_FLOAT,
			.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
			.Flags = D3D12_DSV_FLAG_NONE,
			.Texture2D = {}
		};

		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&m_depthStencil)));

		NAME_D3D12_OBJECT(m_depthStencil);
		m_device->CreateDepthStencilView(
			m_depthStencil.Get(),
			&depthStencilDesc,
			m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	ComPtr<ID3D12Resource> textureUploadHeap;

	// Create the texture.
	{
		UINT TextureWidth = 0;
		UINT TextureHeight = 0;
		UINT TexturePixelSize = 4;
		BYTE* bitmap = nullptr;

		LoadBitmapFromFile(
			TEXT("../../../bitmaps/planks.png"),
			TextureWidth, TextureHeight, &bitmap
		);

		D3D12_HEAP_PROPERTIES heapProps = {
			.Type = D3D12_HEAP_TYPE_DEFAULT,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1,
		};

		D3D12_RESOURCE_DESC resourceDesc = {
			.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
			.Alignment = 0,
			.Width = TextureWidth,
			.Height = TextureHeight,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc = {.Count = 1, .Quality = 0 },
			.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
			.Flags = D3D12_RESOURCE_FLAG_NONE
		};

		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_texture)));

		// Create the GPU upload buffer.
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);

		D3D12_HEAP_PROPERTIES uploadHeapProps = {
			.Type = D3D12_HEAP_TYPE_UPLOAD,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1
		};

		D3D12_RESOURCE_DESC uploadResourceDesc = {
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = uploadBufferSize,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {.Count = 1, .Quality = 0 },
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE
		};

		ThrowIfFailed(m_device->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadResourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap)));

		// Copy the texture data to the upload buffer.
		D3D12_SUBRESOURCE_DATA textureData = {
			.pData = bitmap,
			.RowPitch = TextureWidth * TexturePixelSize,
			.SlicePitch = TextureWidth * TextureHeight * TexturePixelSize
		};

		UpdateSubresources(
			m_commandList.Get(),
			m_texture.Get(),
			textureUploadHeap.Get(),
			0, 0, 1, &textureData);

		D3D12_RESOURCE_BARRIER uploadResourceBarrier = {
			.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
			.Transition = {
				.pResource = m_texture.Get(),
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
				.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			}
		};
		m_commandList->ResourceBarrier(1, &uploadResourceBarrier);

		// Describe and create a SRV for the texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = resourceDesc.Format,
			.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1,
				.PlaneSlice = 0,
				.ResourceMinLODClamp = 0.0f
			}
		};

		D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle =
			m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart();
		cpuDescHandle.ptr += m_device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, cpuDescHandle);
	}

	// Close the command list and execute it to begin the initial GPU setup.
	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Create fences and wait for the frame to be rendered.
	{
		ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

		// Wait for the command list to execute.
		WaitForPreviousFrame();
	}
}

// Update frame-based values.
void D3DProject::OnUpdate() {
	const float rotationSpeed = 0.015625f;
	const float moveSpeed = 0.04f;
	const float DEG_TO_RAD = 0.0174532925;

	if (keyboardState['W']) {
		playerPos.x -= sin(angle * DEG_TO_RAD) * moveSpeed;
		playerPos.z -= -cos(angle * DEG_TO_RAD) * moveSpeed;
	}

	if (keyboardState['S']) {
		playerPos.x += sin(angle * DEG_TO_RAD) * moveSpeed;
		playerPos.z += -cos(angle * DEG_TO_RAD) * moveSpeed;
	}

	if (keyboardState['A'])
		angle += rotationSpeed;

	if (keyboardState['D'])
		angle -= rotationSpeed;

	XMMATRIX wvp_matrix;
	wvp_matrix = XMMatrixMultiply(
		XMMatrixRotationY(2.5f * angle),
		XMMatrixTranslation(-playerPos.x, 0.0f, -playerPos.z)
	);
	wvp_matrix = XMMatrixMultiply(
		wvp_matrix,
		XMMatrixPerspectiveFovLH(
			45.0f, m_aspectRatio, 1.0f, 100.0f
		)
	);
	wvp_matrix = XMMatrixTranspose(wvp_matrix);
	XMStoreFloat4x4(
		&m_constantBufferData.matWorldViewProj,
		wvp_matrix
	);

	memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
}

// Render the scene.
void D3DProject::OnRender() {

	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(1, 0));

	WaitForPreviousFrame();
}

void D3DProject::OnDestroy() {
	WaitForPreviousFrame();
	CloseHandle(m_fenceEvent);
}

void D3DProject::OnKeyDown(UINT8 key) {
	if (key == 'W' || key == 'A' || key == 'S' || key == 'D')
		keyboardState[key] = true;
}

void D3DProject::OnKeyUp(UINT8 key) {
	if (key == 'W' || key == 'A' || key == 'S' || key == 'D')
		keyboardState[key] = false;
}

void D3DProject::PopulateCommandList() {

	// Reset the command allocator and command list.
	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle =
		m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
	m_commandList->SetGraphicsRootDescriptorTable(0, gpuDescHandle);
	gpuDescHandle.ptr += m_device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_commandList->SetGraphicsRootDescriptorTable(1, gpuDescHandle);

	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate that the back buffer will be used as a render target.
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_commandList->ResourceBarrier(1, &barrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_frameIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(
		m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// Record commands.
	const float clearColor[] = { 0.8f, 1.0f, 0.8f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(
		m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
		D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->DrawInstanced(3 * 2 * 2 * 6, 1, 0, 0);

	// Indicate that the back buffer will now be used to present.
	auto barrierRev = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	m_commandList->ResourceBarrier(1, &barrierRev);

	ThrowIfFailed(m_commandList->Close());
}

void D3DProject::WaitForPreviousFrame() {

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence) {
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

HRESULT D3DProject::LoadBitmapFromFile(PCWSTR uri, UINT& width, UINT& height, BYTE** ppBits) {
	HRESULT hr;
	IWICBitmapDecoder* pDecoder = nullptr;
	IWICBitmapFrameDecode* pSource = nullptr;
	IWICFormatConverter* pConverter = nullptr;

	hr = IWICFactory->CreateDecoderFromFilename(
		uri, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
		&pDecoder
	);

	if (SUCCEEDED(hr))
		hr = pDecoder->GetFrame(0, &pSource);

	if (SUCCEEDED(hr))
		hr = IWICFactory->CreateFormatConverter(&pConverter);

	if (SUCCEEDED(hr))
		hr = pConverter->Initialize(
			pSource,
			GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0f,
			WICBitmapPaletteTypeMedianCut
		);

	if (SUCCEEDED(hr))
		hr = pConverter->GetSize(&width, &height);

	if (SUCCEEDED(hr)) {
		*ppBits = new BYTE[4 * width * height];
		hr = pConverter->CopyPixels(
			nullptr, 4 * width, 4 * width * height, *ppBits
		);
	}

	if (pDecoder) pDecoder->Release();
	if (pSource) pSource->Release();
	if (pConverter) pConverter->Release();

	return hr;
}