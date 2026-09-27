#ifndef	dx103DFluidRenderer_included
#define	dx103DFluidRenderer_included
#pragma once

class dx103DFluidData;

class dx103DFluidRenderer
{
public:
	enum Renderer_RT
	{
		RRT_RayDataTex = 0,
		RRT_RayDataTexSmall,
		RRT_RayCastTex,
		RRT_EdgeTex,
		RRT_NumRT
	};

public:
	dx103DFluidRenderer();
	~dx103DFluidRenderer();

	void	Initialize(int gridWidth, int gridHeight, int gridDepth);
	void	Destroy();

	void	SetScreenSize(int width, int height);

	void	Draw(const dx103DFluidData &FluidData);

	static LPCSTR*	GetRTNames() { return m_pRTNames; }
	static LPCSTR*	GetResourceRTNames() { return m_pResourceRTNames; }

private:
	enum RendererShader
	{
		RS_CompRayData_Back = 0,
		RS_CompRayData_Front,
		RS_QuadDownSampleRayDataTexture,
		
		RS_QuadEdgeDetect,
		RS_QuadRaycastFog,
		RS_QuadRaycastCopyFog,
		RS_QuadRaycastFire,
		RS_QuadRaycastCopyFire,

		RS_NumShaders
	};

public:
	struct FogLighting
	{
		Fvector3	m_vLightIntencity;		// the part lighting the whole volume evenly: hemi + ambient (+ overflow lights)

		// Dynamic lights shaded per ray sample (fluid_common_render.h), so a torch lights the smoke where its
		// beam goes instead of tinting the entire volume.
		enum { MAX_LIGHTS = 4 };
		u32			m_iNumLights;
		Fvector4	m_vLightPos[MAX_LIGHTS];	// xyz world position, w range
		Fvector4	m_vLightColor[MAX_LIGHTS];	// rgb colour (already scaled like the old uniform term), w 1 = spot
		Fvector4	m_vLightDir[MAX_LIGHTS];	// xyz direction (spot), w cos(half cone)

		void Reset() { ZeroMemory(this, sizeof(*this));}
	};

private:
	void	InitShaders();
	void	DestroyShaders();

	void	CreateGridBox();
	void	CreateScreenQuad();
	void	CreateJitterTexture();
	void	CreateHHGGTexture();

	void	CalculateRenderTextureSize(int screenWidth, int screenHeight);
	void	CreateRayDataResources( int width, int height );

	void	ComputeRayData();
	void	ComputeEdgeTexture();

	void	DrawScreenQuad();
	void	DrawBox();

	void	CalculateLighting(const dx103DFluidData &FluidData, FogLighting  &LightData);

private:
	bool		m_bInited;

	Fvector3	m_vGridDim;
	float		m_fMaxDim;

	int			m_iRenderTextureWidth;
	int			m_iRenderTextureHeight;

	DirectX::XMMATRIX m_gridMatrix;
	//Fmatrix		m_gridMatrix;

	D3DFORMAT				RTFormats[ RRT_NumRT ];
	ref_rt					RT[ RRT_NumRT ];
	static LPCSTR			m_pRTNames[ RRT_NumRT ];
	static LPCSTR			m_pResourceRTNames[ RRT_NumRT ];

	ref_selement	m_RendererTechnique[ RS_NumShaders ];

	ref_texture		m_JitterTexture;
	ref_texture		m_HHGGTexture;

	ref_geom					m_GeomGridBox;
	ID3DVertexBuffer			*m_pGridBoxVertexBuffer;
	ID3DIndexBuffer				*m_pGridBoxIndexBuffer;
	int							m_iGridBoxVertNum;
	int							m_iGridBoxFaceNum;

	ref_geom					m_GeomQuadVertex;
	ID3DVertexBuffer			*m_pQuadVertexBuffer;

	//	Cache vectors to avoid memory reallocations
	//	TODO: DX10: Reserve memory on object creation
	xr_vector<ISpatial*>		m_lstRenderables;
};

#endif	//	dx103DFluidRenderer_included