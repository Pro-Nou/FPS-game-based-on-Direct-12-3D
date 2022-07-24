//***************************************************************************************
// TexColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include"../../Common/WaveFile.h"

#include "FrameResource.h"
#include<dsound.h>
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
//#pragma comment(lib, "winMM.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct AIitem
{
	int state = 0;//AI状态机，0进攻，1防守，2射击
	int CtrlItemId = 0;//控制的渲染项ID

	XMFLOAT3 Car2CarPos[6];//与其他车间的相对位置
	XMFLOAT3 SelfPos;//自身位置
	XMFLOAT3 Car2BasePos;//与敌方基地的相对位置
	XMFLOAT3 EnemyBasePos;//敌方基地位置
	int team = 0;//所属阵营
	float tmin = MathHelper::Infinity;//距离自身最近的车距离
	int lockedId = -1;//锁定的目标，-1无目标，0-5对应6台车AI，6对应敌方基地

	int AIcontrol[20] = { 0 };//AI控制数组

	//AI炮塔方向
	float dx = 0.0f;
	float dy = 0.0f;

	XMFLOAT2 TGTPos;///前进坐标
	XMVECTOR LeftDir;//左侧向量
	XMVECTOR FrontDir;//前方向量

	int roadCount = 0;//当前路径节点
	int roadSelect = 0;//当前选择的路径
};

struct RenderItem
{
	RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;
	BoundingOrientedBox Bounds;//初始OBB包围盒
	BoundingOrientedBox WorldBounds;//全局OBB包围盒
    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 BaseWorld = MathHelper::Identity4x4();//初始位置 

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;


	bool canbePicked = false;//是否可被拾取
	int AIcontrol = 0;//AI编号，0为玩家
	bool Gravity = false;//是否启用重力
	bool Collider = false;//是否参与碰撞
	//上述几个参数在渲染项不为车体时默认如上
	
	bool Static = true;//是否为静态
	bool invisible = false;//是否可见，一般来说仅地形的碰撞项使用
	float rotateY = 0.0f;//记录Y旋转角（暂未启用）
	float GravityLM = 0.0f;//重力造成的动量
	int team = 0;//阵营，0地形，1蓝，2红
	int currentHP = 0;//车体、基地专用，剩余耐久
	int maxHP = 0;//车体、基地专用，耐久上限
	XMVECTOR ColliderLM = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);//车体专用，碰撞造成的总动量矢量
	XMVECTOR RotateLM = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);//车体专用，碰撞造成的合角动量矢量
	XMVECTOR CarLM = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);//车体专用，和其他车体碰撞造成的动量矢量
	int ColliderRiCount = 0;//车体专用，有效动量碰撞实体数
	int RotateFcaeCount = 0;//车体专用，有效角动量碰撞面数
	BoundingOrientedBox LastPos;//车体专用，前一帧位置
	bool crashed = false;//车体专用，是否击毁
	int chara = 0;//0地形，1小车，2小车轮，3炮塔，4大车，5大车轮，6大炮塔，7弹头，8哨兵，9基地，
				//51弹头爆炸，52车用阵亡爆炸，53基地用阵亡爆炸，
				//100主菜单，101蓝队获胜，102红队获胜,103准星,104车体方向,105血条,106、107基地血条,108蓝基地图标，109红基地图标
				//110半透明背景，111开始游戏
	float dx;//炮塔专用，旋转角
	float dy;//炮塔专用，俯仰角
	float ExistedTime = 0.0;//弹头、动画专用，存在时间
	bool fired = false;//弹头专用，是否开火
	bool hited = false;//弹头专用，是否命中
	int hitedRitemID = -1;//弹头专用，命中的渲染项id，未命中时为-1
	int Damage = 0;//弹头专用，每一发的伤害
};

enum class RenderLayer : int
{
	Opaque = 0,
	alpha_1,
	HPSprites,
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Debug,
	Sky,
	Count
};

class TexColumnsApp : public D3DApp
{
public:
    TexColumnsApp(HINSTANCE hInstance);
    TexColumnsApp(const TexColumnsApp& rhs) = delete;
    TexColumnsApp& operator=(const TexColumnsApp& rhs) = delete;
    ~TexColumnsApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
	void BuildTreeSpritesGeometry();

    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
	bool PlayBGM(LPWSTR FileName);
	bool Play3DWAV(XMFLOAT3 SPos,LPWSTR FileName);
	bool LoadWAV();
	bool LoadWAVCar();
	void PlayEvnWav(XMFLOAT3 SPos, int id);
	void PlayCarWav(XMFLOAT3 SPos, int id);

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void pick(int sx, int sy);
//	void AIRoadInit();//AI路径初始化
	bool pickButton(int sx, int sy, int id);//按钮拾取，用于菜单
	BoundingOrientedBox UpdateBoundBox(XMMATRIX world, BoundingOrientedBox bounds);//更新包围盒，world全局，bounds初始
	BoundingOrientedBox InitializeBox(UINT vcount, std::vector<GeometryGenerator::Vertex> Vertices);//初始化包围盒，vcount顶点数，vertices顶点数组
	void Graph(int id1, int id2);//碰撞检测，id1撞id2
	void addUI();//添加UI渲染项
	void addGrass(float x,float y,float z,float l,int n);//生成草皮，xyz处以边长l正方形生成n片草
	void AddScar(float x, float y, float z, float angle, int AI, int team);//添加步兵车
	void AddBase(float x, float y, float z, float angle, int team);//添加基地
	void AIupdate();//更新AI控制数组
	void controller(int id, int* control);//角色控制器，id渲染项下标，仅限车体使用，control指令数组
	void LMAnalysis(int id);//角色动量分析，id渲染项下标，仅限车体使用
	void LMGravity();//重力模拟
	void PlayerTurret(int id, float dt);//玩家炮台控制，id渲染项下标，dt计时器，仅限炮台使用
	void AITurret(int id, float dt);//AI炮台控制，id渲染项下标，dt计时器，仅限炮台使用
	void LMBullet(int id, float dt, float BulletSpeed, float ExistedTime);//弹头运动分析，id渲染下标，dt计时器，bulletspeed弹头飞行速度，existedtime弹头存在时间，仅限弹头使用
	float GraphBullet(int id);//弹头碰撞分析，id渲染项下标，仅限弹头使用
	bool cmpFloat4(XMFLOAT4 a, XMFLOAT4 b, float angle);//FLOAT4插值比较，主要用于比较角度变化
	float GetHillsHeight(float x, float z)const;
	bool CanBeShoot(int id1);//AI专用弹道检测，弹道中是否有墙
	void DrawUI();//画UI

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count]; //mOpaqueRitems;
	std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;


	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = 0.2f*XM_PI;
    float mRadius = 15.0f;

    POINT mLastMousePos;


	float myScaleSize = 2.0f;
	int state = -1;//全局状态机
	int winner = 0;//获胜队伍

	Camera mCamera;

	POINT m_mouse;//鼠标位置中转
	RECT rect;//窗体信息中转

	float dx = 0.0f;//鼠标x偏移
	float dy = 0.0f;//鼠标y偏移

	float dsx = 0.0f;//鼠标x偏移
	float dsy = 0.0f;//鼠标y偏移

	int playercontrol[20];//玩家控制数组
	float playerspeed = 0.0f;//全局播放速度
	float rotatespeed = 0.0f;//全局旋转速度
	float globalGravity = 0.05f;//全局重力系数

	RenderItem* mPickedRitem = nullptr;
	int itemCount = 0;//渲染项总数

	bool GUARD = true;//玩家无敌
	int playerID = 6;//玩家ID

	AIitem GameAIs[6];//AI数组
	LPDIRECTSOUNDBUFFER8 g_pDSBuffer8;//BGM缓存
	LPDIRECTSOUNDBUFFER8 EVN_SBuffer[4];//音效缓存
	LPDIRECTSOUNDBUFFER8 Car_SBuffer[6];//行车音效缓存
	bool CarRunning[6] = { false };

	int WAVCount = 4;
	LPDIRECTSOUNDBUFFER  g_pDSBuffer;


	int GlobalRoadLenth = 6;//路径节点数
	int GlobalRoadCount = 6;//路径条数
	XMFLOAT2 AIRoads[6][6] = { {{-50.0f,-20.0f},{-48.0f,12.0f},{-15.0f,-3.0f},{15.0f,0.0f},{50.0f,-10.0f},{50.0f,15.0f}},
								{{-45.0f,-20.0f},{-29.0f,27.0f},{5.0f,27.0f},{31.0f,10.0f},{29.0f,25.0f},{43.0f,25.0f}},
								{{-45.0f,-25.0f}, {-29.0f,-25.0f},{-29.0f,-12.0f},{0.0f,-26.0f},{27.0f,-27.0f},{45.0f,20.0f}},
								{{-50.0f,-20.0f}, {-48.0f,26.0f}, {0.0f,26.0f}, {29.0f,12.0f},{29.0f,25.0f},{45.0f,25.0f}},
								{{-45.0f,-25.0f}, {-29.0f,-25.0f},{-29.0f,-12.0f},{15.0f,0.0f},{50.0f,-10.0f},{50.0f,15.0f}},
								{{-50.0f,-20.0f},{-48.0f,12.0f},{-15.0f,-3.0f},{-5.0f,-26.0f}, {27.0f,-27.0f},{45.0f,20.0f}}};//AI路径

	bool showHelper = false;//显示帮助
	bool teamChoosing = false;
	bool PlayingBGM = false;
	bool FirstCamera = false;//第一人称使能
	XMFLOAT3 PlayerPos{ 0.0f,0.0f,0.0f };
	XMFLOAT3 PlayerUP{ 0.0f,0.0f,0.0f };
	XMFLOAT3 PlayerLook{ 0.0f,0.0f,0.0f };
	UINT squareCount = 0;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        TexColumnsApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

TexColumnsApp::TexColumnsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

TexColumnsApp::~TexColumnsApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TexColumnsApp::Initialize()
{
	for (int i = 0; i < 20; i++)
	{
		playercontrol[i] = 0;
		for (int j = 0; j < 6; j++)
			GameAIs[j].AIcontrol[i] = 0;
	}
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 0.0f, 0.0f);

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

 
	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayout();

    BuildShapeGeometry();
	BuildTreeSpritesGeometry();

	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();
	LoadWAV();
	LoadWAVCar();
	GetWindowRect(mhMainWnd, &rect);

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
void TexColumnsApp::OnResize()
{
    D3DApp::OnResize();
	mCamera.SetLens(0.3f*MathHelper::Pi, AspectRatio(), 0.01f, 1000.0f);

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.3f*MathHelper::Pi, AspectRatio(), 0.01f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);

	GetWindowRect(mhMainWnd, &rect);
	

}

void TexColumnsApp::Update(const GameTimer& gt)
{
	if (!FirstCamera)
	{
		while (ShowCursor(TRUE) < 0)
			ShowCursor(TRUE);
	}
	else if (FirstCamera)
	{
		while (ShowCursor(FALSE) >= 0)
			ShowCursor(FALSE);
	}

	PlayerPos = mCamera.GetPosition3f();
	PlayerLook = mCamera.GetLook3f();
	PlayerUP = mCamera.GetUp3f();
    OnKeyboardInput(gt);
	float dt = gt.DeltaTime();
	playerspeed = 5 * dt;
	rotatespeed = XM_PI * dt;
	if (state == -1)
	{
		for (int i = 0; i < 6; i++)
		{
			if (CarRunning[i])
				Car_SBuffer[i]->Stop();
			CarRunning[i] = false;
		}

		//复位
		if (playerID < 6)
		{
			int idc = GameAIs[playerID].CtrlItemId;

			mAllRitems[idc + 5]->Geo = mGeometries["treeSpritesGeo"].get();
			mAllRitems[idc + 5]->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
			mAllRitems[idc + 5]->IndexCount = mAllRitems[idc + 5]->Geo->DrawArgs["HPstrip"].IndexCount;
			mAllRitems[idc + 5]->StartIndexLocation = mAllRitems[idc + 5]->Geo->DrawArgs["HPstrip"].StartIndexLocation;
			mAllRitems[idc + 5]->BaseVertexLocation = mAllRitems[idc + 5]->Geo->DrawArgs["HPstrip"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::HPSprites].push_back(mAllRitems[idc + 5].get());
		}
		for (int i = 0; i < itemCount; i++)
		{
				XMMATRIX BaseWorld = XMLoadFloat4x4(&mAllRitems[i]->BaseWorld);
				XMStoreFloat4x4(&mAllRitems[i]->World, BaseWorld);
				mAllRitems[i]->NumFramesDirty = 3; //3;
				mAllRitems[i]->currentHP = mAllRitems[i]->maxHP;
				mAllRitems[i]->GravityLM = 0.0f;
				mAllRitems[i]->WorldBounds = UpdateBoundBox(XMLoadFloat4x4(&mAllRitems[i]->World), mAllRitems[i]->Bounds);
		}
		for (int i = 0; i < 3; i++)
		{
			GameAIs[i].roadCount = 0;
			GameAIs[i].roadSelect = i;
			GameAIs[i + 3].roadCount = 0;
			GameAIs[i + 3].roadSelect = i;
		}
		winner = 0;
		state = 0;
		PlayBGM(L"res/MainMenu.wav");
	}
	if (state == 0)
	{
		DrawUI();
	}
	else if (state == 2)
	{
		for (int i = 0; i < 6; i++)
		{
			if (CarRunning[i])
				Car_SBuffer[i]->Stop();
			CarRunning[i] = false;
		}
		for (int i = 0; i < itemCount; i++)
		{
			//基地爆炸
			if (mAllRitems[i]->chara == 53 && mAllRitems[i]->fired)
			{
				if (mAllRitems[i]->ExistedTime < 5.0f)
				{
					mAllRitems[i]->ExistedTime += dt;
					float st = 1.0f + 2*dt;
					XMMATRIX ExpW = XMLoadFloat4x4(&mAllRitems[i]->World);
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(st, st, st)*ExpW);
					mAllRitems[i]->NumFramesDirty = gNumFrameResources; //3
				}
				else if (mAllRitems[i]->ExistedTime >= 2.0f)
				{
					mAllRitems[i]->World = mAllRitems[i]->BaseWorld;
					mAllRitems[i]->NumFramesDirty = gNumFrameResources; //3
					mAllRitems[i]->ExistedTime = 0.0f;
					mAllRitems[i]->fired = false;
				}
			}
		}
		DrawUI();

	}
	if (state == 3)
	{
		DrawUI();
	}
	else if (state == 1)
	{

		//帧数测试
		//XMStoreFloat4x4(&mAllRitems[0]->World, XMMatrixRotationY(0.1*rotatespeed)*testWorld);
		/*if (mAllRitems[0]->ExistedTime < 0.1f)
		{
			XMMATRIX testWorld = XMLoadFloat4x4(&mAllRitems[0]->World);
			float st = 1.0f + 10 * dt;
			XMStoreFloat4x4(&mAllRitems[0]->World, XMMatrixScaling(st, st, st)*testWorld);
			mAllRitems[0]->ExistedTime += dt;
		}
		else if (mAllRitems[0]->ExistedTime >= 0.1f&&mAllRitems[0]->ExistedTime < 0.3f)
		{
			mAllRitems[0]->ExistedTime += dt;
		}
		else if (mAllRitems[0]->ExistedTime >= 0.3f&&mAllRitems[0]->ExistedTime < 0.4f)
		{
			XMMATRIX testWorld = XMLoadFloat4x4(&mAllRitems[0]->World);
			float st = 0.0f;
			XMStoreFloat4x4(&mAllRitems[0]->World, XMMatrixScaling(st, st, st)*testWorld);
			mAllRitems[0]->ExistedTime += dt;
		}
		else if (mAllRitems[0]->ExistedTime >= 0.4f)
		{
			mAllRitems[0]->World._11 = 1.0f;
			mAllRitems[0]->World._22 = 1.0f;
			mAllRitems[0]->World._33 = 1.0f;
			mAllRitems[0]->ExistedTime = 0.0f;
		}
		mAllRitems[0]->NumFramesDirty = gNumFrameResources; //3;*/
		

		//车体位置记录，AI位置更新
		for (int i = 0; i < itemCount; i++)
			if (mAllRitems[i]->chara == 1)
			{
				mAllRitems[i]->LastPos = mAllRitems[i]->WorldBounds;
			    GameAIs[mAllRitems[i]->AIcontrol].SelfPos = mAllRitems[i]->LastPos.Center;
				XMMATRIX CW = XMLoadFloat4x4(&mAllRitems[i]->World);
				XMVECTOR LeftAI = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
				XMVECTOR FrontAI = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
				GameAIs[mAllRitems[i]->AIcontrol].LeftDir = XMVector3TransformNormal(LeftAI, CW);
				GameAIs[mAllRitems[i]->AIcontrol].FrontDir = XMVector3TransformNormal(FrontAI, CW);
			}

		//AI模块
		AIupdate();

		//角色控制器
		for (int i = 0; i < 6; i++)
		{
			if (i == playerID)
				controller(GameAIs[i].CtrlItemId, playercontrol);
			else
				controller(GameAIs[i].CtrlItemId, GameAIs[i].AIcontrol);
		}
			
		//重力模拟
		LMGravity();

		
		//全局化包围盒
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->Static == false)
				mAllRitems[i]->WorldBounds = UpdateBoundBox(XMLoadFloat4x4(&mAllRitems[i]->World), mAllRitems[i]->Bounds);
		}

		//碰撞检测
		for (int i = 0; i < itemCount; i++)
			if (mAllRitems[i]->chara == 1)
				for (int j = 0; j < itemCount; j++)
					if (mAllRitems[j]->Collider&& j != i)
						Graph(i, j);

		//车与车间的碰撞
		for (int i = 0; i < itemCount; i++)
			if (mAllRitems[i]->chara == 1)
			{
				XMMATRIX W = XMLoadFloat4x4(&mAllRitems[i]->World);
				XMStoreFloat4x4(&mAllRitems[i]->World, W*XMMatrixTranslationFromVector(mAllRitems[i]->CarLM));
				mAllRitems[i]->NumFramesDirty = gNumFrameResources; //3;
				mAllRitems[i]->CarLM = XMVectorZero();
			}

		//全局化包围盒
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->Static == false)
				mAllRitems[i]->WorldBounds = UpdateBoundBox(XMLoadFloat4x4(&mAllRitems[i]->World), mAllRitems[i]->Bounds);
		}


		//动量分析
		for (int i = 0; i < itemCount; i++)
			if (mAllRitems[i]->chara == 1)
				LMAnalysis(i);
				
		//全局化包围盒
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->Static == false)
				mAllRitems[i]->WorldBounds = UpdateBoundBox(XMLoadFloat4x4(&mAllRitems[i]->World), mAllRitems[i]->Bounds);
		}

		//炮台+弹头控制
		for (int i = 0; i < itemCount; i++)
			if (mAllRitems[i]->chara == 3)
			{
				if (mAllRitems[i]->AIcontrol == playerID)
					PlayerTurret(i, dt);
				else
					AITurret(i, dt);
				LMBullet(i + 1, dt, 200.0f*dt, 0.1f);
			}
		//伤害检测
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->chara == 7 && mAllRitems[i]->hitedRitemID > -1)
			{
				if (mAllRitems[mAllRitems[i]->hitedRitemID]->chara == 1 && mAllRitems[mAllRitems[i]->hitedRitemID]->team != mAllRitems[i]->team)
				{
					if (mAllRitems[mAllRitems[i]->hitedRitemID]->AIcontrol != playerID)
						mAllRitems[mAllRitems[i]->hitedRitemID]->currentHP -= mAllRitems[i]->Damage;
					else if (mAllRitems[mAllRitems[i]->hitedRitemID]->AIcontrol == playerID && !GUARD)
						mAllRitems[mAllRitems[i]->hitedRitemID]->currentHP -= mAllRitems[i]->Damage;
					mAllRitems[i]->hitedRitemID = -1;
				}
				else if (mAllRitems[mAllRitems[i]->hitedRitemID]->chara == 9 && mAllRitems[mAllRitems[i]->hitedRitemID]->team != mAllRitems[i]->team)
				{
					mAllRitems[mAllRitems[i]->hitedRitemID]->currentHP -= mAllRitems[i]->Damage;
					mAllRitems[i]->hitedRitemID = -1;
				}
			}
			else if (mAllRitems[i]->chara == 9)
			{
				float restoreCD = 0.2;
				if (mAllRitems[i]->currentHP < mAllRitems[i]->maxHP&&mAllRitems[i]->currentHP>0)
				{
					if (mAllRitems[i]->ExistedTime == 0.0f)
					{
						mAllRitems[i]->currentHP++;
						mAllRitems[i]->ExistedTime += dt;
					}
					else if(mAllRitems[i]->ExistedTime > 0.0f&&mAllRitems[i]->ExistedTime < restoreCD)
						mAllRitems[i]->ExistedTime += dt;
					else if(mAllRitems[i]->ExistedTime >= restoreCD)
						mAllRitems[i]->ExistedTime = 0.0f;
				}
			}
		}

		//死亡判定
		for (int i = 0; i < itemCount; i++)
		{
			//击毁判定
			if (mAllRitems[i]->chara == 1 && mAllRitems[i]->currentHP <= 0 && !mAllRitems[i]->crashed)
			{
				mAllRitems[i]->crashed = true;
				XMFLOAT3 CarPos = { mAllRitems[i]->World._41,mAllRitems[i]->World._42,mAllRitems[i]->World._43 };
				PlayEvnWav(CarPos, 2);

				mAllRitems[i + 4]->World = mAllRitems[i]->World;
				mAllRitems[i + 4]->fired = true;
				mAllRitems[i + 4]->NumFramesDirty = gNumFrameResources;
				/*mAllRitems[i]->currentHP = mAllRitems[i]->maxHP;
				for (int j = 0; j < 2; j++)
				{
					XMStoreFloat4x4(&mAllRitems[i + j]->World, XMLoadFloat4x4(&mAllRitems[i + j]->BaseWorld));
					mAllRitems[i + j]->NumFramesDirty = gNumFrameResources; //3;
				}*/
			}
			else if (mAllRitems[i]->chara == 9 && mAllRitems[i]->currentHP <= 0)
			{
				XMFLOAT3 BasePos = { mAllRitems[i]->World._41,mAllRitems[i]->World._42,mAllRitems[i]->World._43 };
				PlayEvnWav(BasePos, 3);
				state = 2;
				mAllRitems[i + 1]->fired = true;
				mAllRitems[i + 1]->World = mAllRitems[i]->World;
				mAllRitems[i + 1]->NumFramesDirty = gNumFrameResources;
				if (mAllRitems[i]->team == 1)
					winner = 2;
				else if (mAllRitems[i]->team == 2)
					winner = 1;
				FirstCamera = false;
			}

			//死亡动画
			if (mAllRitems[i]->chara == 52 && mAllRitems[i]->fired)
			{
				if (mAllRitems[i]->ExistedTime < 0.15f)
				{
					XMMATRIX ExpW = XMLoadFloat4x4(&mAllRitems[i]->World);
					float st = 1.0f + 10 * dt;
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(st, st, st)*ExpW);
					mAllRitems[i]->NumFramesDirty = gNumFrameResources;
					mAllRitems[i]->ExistedTime += dt;
				}
				else if (mAllRitems[i]->ExistedTime >= 0.15f&&mAllRitems[i]->ExistedTime < 0.3f)
				{
					mAllRitems[i]->ExistedTime += dt;
				}
				else if (mAllRitems[i]->ExistedTime >= 0.3&&mAllRitems[i]->ExistedTime < 2.0f)
				{
					mAllRitems[i]->World = mAllRitems[i]->BaseWorld;
					mAllRitems[i]->NumFramesDirty = gNumFrameResources;
					mAllRitems[i]->ExistedTime += dt;
				}
				else if (mAllRitems[i]->ExistedTime >= 2.0f)
				{
					mAllRitems[i]->ExistedTime = 0.0f;
					mAllRitems[i]->fired = false;
					mAllRitems[i - 4]->crashed = false;
					mAllRitems[i - 4]->currentHP = mAllRitems[i - 4]->maxHP;
					GameAIs[mAllRitems[i - 4]->AIcontrol].roadCount = 0;
					GameAIs[mAllRitems[i - 4]->AIcontrol].roadSelect = rand() % GlobalRoadCount;
					for (int j = 0; j < 5; j++)
					{
						mAllRitems[i - j]->World = mAllRitems[i - j]->BaseWorld;
						mAllRitems[i - j]->NumFramesDirty = gNumFrameResources; //3;
					}

				}
			}
		}

		//全局化包围盒
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->Static == false)
				mAllRitems[i]->WorldBounds = UpdateBoundBox(XMLoadFloat4x4(&mAllRitems[i]->World), mAllRitems[i]->Bounds);
		}
		//UI测试
		DrawUI();

	}



	//摄像机更新
	mCamera.UpdateViewMatrix();


	//控制器初始化
	for (int i = 0; i < 20; i++)
	{
		if (i != 8)
			playercontrol[i] = 0;
		for (int j = 0; j < 6; j++)
			GameAIs[j].AIcontrol[i] = 0;
	}


    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}
void TexColumnsApp::AIupdate()
{
	for (int i = 0; i < itemCount; i++)
		if (mAllRitems[i]->chara == 9)
		{
			float HPOffset = (float)mAllRitems[i]->currentHP / (float)mAllRitems[i]->maxHP;
			for (int j = 0; j < 6; j++)
			{
				if (j == 1 || j == 4)
				{
					GameAIs[j].state = 0;
				}
				else
				{

					if (HPOffset >= 0.7f&&mAllRitems[i]->team == GameAIs[j].team)//基地耐久充足，进攻态
						GameAIs[j].state = 0;
					else if (HPOffset < 0.7f&&mAllRitems[i]->team == GameAIs[j].team)//基地耐久不足，防守态
						GameAIs[j].state = 1;
				}
			}
		}

	//AI状态机变化
	for (int i = 0; i < 6; i++)
	{
		if (i<=2)
		{
			GameAIs[i].TGTPos = AIRoads[GameAIs[i].roadSelect][GameAIs[i].roadCount];
		}
		else
		{
			GameAIs[i].TGTPos = { -AIRoads[GameAIs[i].roadSelect][GameAIs[i].roadCount].x,-AIRoads[GameAIs[i].roadSelect][GameAIs[i].roadCount].y };
		}


		//重置相对距离、锁定目标
		GameAIs[i].tmin = MathHelper::Infinity;
		GameAIs[i].lockedId = -1;

		//获取敌方基地相对位置
		for (int j = 0; j < 1; j++)
		{
			float t = MathHelper::Infinity;
			GameAIs[i].Car2BasePos= { GameAIs[i].EnemyBasePos.x - GameAIs[i].SelfPos.x,
									GameAIs[i].EnemyBasePos.y - GameAIs[i].SelfPos.y,
									GameAIs[i].EnemyBasePos.z - GameAIs[i].SelfPos.z };
			t = sqrt(pow(GameAIs[i].Car2BasePos.x, 2) + pow(GameAIs[i].Car2BasePos.y, 2) + pow(GameAIs[i].Car2BasePos.z, 2));
			if (t < GameAIs[i].tmin)
			{
				GameAIs[i].tmin = t;
				GameAIs[i].lockedId = 6;
			}
		}

		//获取其他阵营车相对位置
		for (int j = 0; j < 6; j++)
		{
			float t = MathHelper::Infinity;
			if (GameAIs[i].team == GameAIs[j].team || mAllRitems[GameAIs[j].CtrlItemId]->crashed)
				GameAIs[i].Car2CarPos[j] = { MathHelper::Infinity,MathHelper::Infinity,MathHelper::Infinity };
			else
			{
				GameAIs[i].Car2CarPos[j] = { GameAIs[j].SelfPos.x - GameAIs[i].SelfPos.x,
											GameAIs[j].SelfPos.y - GameAIs[i].SelfPos.y,
											GameAIs[j].SelfPos.z - GameAIs[i].SelfPos.z };
				t = sqrt(pow(GameAIs[i].Car2CarPos[j].x, 2) + pow(GameAIs[i].Car2CarPos[j].y, 2) + pow(GameAIs[i].Car2CarPos[j].z, 2));
			}
			if (t < GameAIs[i].tmin)
			{
				GameAIs[i].tmin = t;
				GameAIs[i].lockedId = j;
			}
		}


		//敌方角色处于射程内且没有隔墙，射击态
		if (GameAIs[i].lockedId == 6)
		{
			if (GameAIs[i].tmin <= 21.0f)
			{
				if (CanBeShoot(i))
					GameAIs[i].state = 2;
			}
		}
		else if (GameAIs[i].lockedId < 6)
		{
			if (GameAIs[i].tmin <= 18.0f&& mAllRitems[GameAIs[GameAIs[i].lockedId].CtrlItemId]->currentHP > 0)
			{
				if (CanBeShoot(i))
					GameAIs[i].state = 2;
			}
		}

	}
	
	for (int i = 0; i < 6; i++)
	{
		if (GameAIs[i].state == 1 || GameAIs[i].state == 0)
		{
			GameAIs[i].dx = 0.0f;
			GameAIs[i].dy = 0.0f;
			GameAIs[i].dx += atan(GameAIs[i].FrontDir.m128_f32[0] / GameAIs[i].FrontDir.m128_f32[2]);
			if (GameAIs[i].FrontDir.m128_f32[2] < 0.0f)
				GameAIs[i].dx += XM_PI;
			//if (i == 2)
			{
				XMFLOAT2 AINorL{ GameAIs[i].LeftDir.m128_f32[0],GameAIs[i].LeftDir.m128_f32[2] };
				XMFLOAT2 AINorF{ GameAIs[i].FrontDir.m128_f32[0],GameAIs[i].FrontDir.m128_f32[2] };
				XMFLOAT2 TGTNor{ GameAIs[i].TGTPos.x - GameAIs[i].SelfPos.x,GameAIs[i].TGTPos.y - GameAIs[i].SelfPos.z };
				float TGTdistance = sqrt(pow(TGTNor.x, 2) + pow(TGTNor.y, 2));
				if (TGTdistance <= 1.5f)
				{
					if (GameAIs[i].roadCount < GlobalRoadLenth - 1 && GameAIs[i].state == 0)
						GameAIs[i].roadCount++;
					else if(GameAIs[i].roadCount > 0 && GameAIs[i].state == 1)
						GameAIs[i].roadCount--;
					GameAIs[i].AIcontrol[0] = 1;
					GameAIs[i].AIcontrol[4] = 0;
					GameAIs[i].AIcontrol[8] = 0;
				}
				else
				{
					float TGTcosL = (AINorL.x*TGTNor.x + AINorL.y*TGTNor.y) / TGTdistance;
					float TGTcosF = (AINorF.x*TGTNor.x + AINorF.y*TGTNor.y) / TGTdistance;
					if(abs(TGTcosL)<=0.2&&TGTcosF>0.0)
						GameAIs[i].AIcontrol[0] = 1;
					else if (abs(TGTcosL) <= 0.2&&TGTcosF < 0.0)
					{
						GameAIs[i].AIcontrol[5] = 1;
						GameAIs[i].AIcontrol[2] = 1;
					}
					if (TGTcosL >= 0.1f)
						GameAIs[i].AIcontrol[5] = 1;
					else if(TGTcosL<= -0.1f)
						GameAIs[i].AIcontrol[4] = 1;
					GameAIs[i].AIcontrol[8] = 0;
				}
			}
			/*else
			{
				GameAIs[i].AIcontrol[0] = 1;
				GameAIs[i].AIcontrol[4] = 1;
				GameAIs[i].AIcontrol[8] = 0;
			}*/
		}
		else if (GameAIs[i].state == 2)
		{
			GameAIs[i].AIcontrol[0] = 0;
			GameAIs[i].AIcontrol[4] = 0;
			GameAIs[i].AIcontrol[8] = 1;
			if (GameAIs[i].lockedId == 6)
			{
				GameAIs[i].dx= atan(GameAIs[i].Car2BasePos.x / GameAIs[i].Car2BasePos.z);
				if (GameAIs[i].Car2BasePos.z < 0)
					GameAIs[i].dx += XM_PI;
				GameAIs[i].dy = -atan(GameAIs[i].Car2BasePos.y / sqrt(pow(GameAIs[i].Car2BasePos.x, 2) + pow(GameAIs[i].Car2BasePos.z, 2)));
			}
			else
			{
				GameAIs[i].dx = atan(GameAIs[i].Car2CarPos[GameAIs[i].lockedId].x/ GameAIs[i].Car2CarPos[GameAIs[i].lockedId].z);
				if (GameAIs[i].Car2CarPos[GameAIs[i].lockedId].z < 0)
					GameAIs[i].dx += XM_PI;
				GameAIs[i].dy = -atan(GameAIs[i].Car2CarPos[GameAIs[i].lockedId].y / sqrt(pow(GameAIs[i].Car2CarPos[GameAIs[i].lockedId].x, 2) + pow(GameAIs[i].Car2CarPos[GameAIs[i].lockedId].z, 2)));
			}
		}
	}


}
void TexColumnsApp::controller(int id, int* control)
{
	if (mAllRitems[id]->crashed)
	{
		if (CarRunning[mAllRitems[id]->AIcontrol])
			Car_SBuffer[mAllRitems[id]->AIcontrol]->Stop();
		CarRunning[mAllRitems[id]->AIcontrol] = false;
		return;
	}
	float ctrlSpeed = 1.5f;
	float ctrlRotateSpeed = 1.0f;
	//目标控制
	if (control[0] == 1)//上
	{
		XMMATRIX world = XMLoadFloat4x4(&mAllRitems[id]->World);
		if (control[2] + control[3] == 0)
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0f, 0.0f, ctrlSpeed*playerspeed)*world);
		else if (control[2] + control[3] == 1)
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0f, 0.0f, sqrt(ctrlSpeed/2.0f)*playerspeed)*world);
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}
	if (control[1] == 1)//下
	{
		XMMATRIX world = XMLoadFloat4x4(&mAllRitems[id]->World);
		if (control[2] + control[3] == 0)
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0f, 0.0f, -ctrlSpeed *playerspeed)*world);
		else if (control[2] + control[3] == 1)
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0f, 0.0f, -sqrt(ctrlSpeed/2.0f)*playerspeed)*world);
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}
	if (control[2] == 1)//左
	{
		XMMATRIX world = XMLoadFloat4x4(&mAllRitems[id]->World);
		if (control[0] + control[1] == 0)
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(-ctrlSpeed *playerspeed, 0.0f, 0.0f)*world);
		else if (control[0] + control[1] == 1)
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(-sqrt(ctrlSpeed/2.0f)*playerspeed, 0.0f, 0.0f)*world);
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}


	if (control[3] == 1)//右
	{
		XMMATRIX world = XMLoadFloat4x4(&mAllRitems[id]->World);
		if (control[0] + control[1] == 0)
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(ctrlSpeed*playerspeed, 0.0f, 0.0f)*world);
		else if (control[0] + control[1] == 1)
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(sqrt(ctrlSpeed/2.0f)*playerspeed, 0.0f, 0.0f)*world);
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}
	if (control[4] == 1)//左旋
	{
		XMMATRIX world = XMLoadFloat4x4(&mAllRitems[id]->World);
		XMStoreFloat4x4(&mAllRitems[id]->World,
			XMMatrixRotationNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f), -ctrlRotateSpeed*rotatespeed)*world);
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}
	if (control[5] == 1)//右旋
	{
		XMMATRIX world = XMLoadFloat4x4(&mAllRitems[id]->World);
		XMStoreFloat4x4(&mAllRitems[id]->World,
			XMMatrixRotationNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f), ctrlRotateSpeed*rotatespeed)*world);
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}
	if (control[6] == 1)//测试用
	{
		//复位
		XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
		mAllRitems[id]->GravityLM = 0.0f;
		mAllRitems[id]->rotateY = 0.0f;
	}
	if (control[7] == 1)
	{
		XMMATRIX world = XMLoadFloat4x4(&mAllRitems[id]->World);
		XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0f, 0.0f, 0.0f)*XMMatrixRotationRollPitchYaw(-XM_PI / 6.0, 0.0, -XM_PI / 3.0));
		mAllRitems[id]->GravityLM = 0.0f;
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}
	if (control[8] == 1)
	{
		mAllRitems[id + 2]->fired = true;
	}
	if (control[0] + control[1] + control[2] + control[3] + control[4] + control[5] > 0  && CarRunning[mAllRitems[id]->AIcontrol] == false)
	{
		XMFLOAT3 CarPos = { mAllRitems[id]->World._41,mAllRitems[id]->World._42,mAllRitems[id]->World._43 };
		PlayCarWav(CarPos, mAllRitems[id]->AIcontrol);
		CarRunning[mAllRitems[id]->AIcontrol] = true;
	}
	else if (control[0] + control[1] + control[2] + control[3] + control[4] + control[5] == 0  && CarRunning[mAllRitems[id]->AIcontrol] == true)
	{
		XMFLOAT3 CarPos = { mAllRitems[id]->World._41,mAllRitems[id]->World._42,mAllRitems[id]->World._43 };
		PlayCarWav(CarPos, mAllRitems[id]->AIcontrol);
		CarRunning[mAllRitems[id]->AIcontrol] = false;
	}
	else if (control[0] + control[1] + control[2] + control[3] + control[4] + control[5] > 0 && CarRunning[mAllRitems[id]->AIcontrol] == true)
	{
		XMFLOAT3 CarPos = { mAllRitems[id]->World._41,mAllRitems[id]->World._42,mAllRitems[id]->World._43 };
		float Slength = sqrt(pow(CarPos.x - PlayerPos.x, 2) + pow(CarPos.y - PlayerPos.y, 2) + pow(CarPos.z - PlayerPos.z, 2));
		int VolumOffset;
		if (Slength < 1.0f)
			VolumOffset = -500;
		else if (Slength > 50.0f)
			VolumOffset = -5000;
		else
			VolumOffset = -500 - 2500 * ((Slength - 1.0f) / 49.0f);
		Car_SBuffer[mAllRitems[id]->AIcontrol]->SetVolume(VolumOffset);//-500~-3000

	}
}
void TexColumnsApp::LMAnalysis(int id)
{
	//受力分析
	if (mAllRitems[id]->ColliderRiCount > 0)
	{
		XMMATRIX W = XMLoadFloat4x4(&mAllRitems[id]->World);
		XMMATRIX toL = XMMatrixInverse(&XMMatrixDeterminant(W), W);
		//XMVECTOR LMRot = mAllRitems[id]->ColliderLM;
		//XMVECTOR LMDir = 2 * playerspeed * mAllRitems[id]->ColliderLM;

		/*if (LMDir.m128_f32[1] > 0.01)
		{
			LMDir.m128_f32[1] = 0.01f;
		}*/

		//速度方向Y向量
		XMFLOAT3 LMFspeed;
		LMFspeed.x = mAllRitems[id]->WorldBounds.Center.x - mAllRitems[id]->LastPos.Center.x;
		LMFspeed.y = 0.0;
		LMFspeed.z = mAllRitems[id]->WorldBounds.Center.z - mAllRitems[id]->LastPos.Center.z;
		//速度方向Y向量长度
		float TotalSpeedY = sqrt(pow(LMFspeed.x, 2) + pow(LMFspeed.z, 2));

		//碰撞方向Y向量
		XMVECTOR LMDir = mAllRitems[id]->ColliderLM;

		XMFLOAT3 LMFDir;

		LMFDir.x = LMDir.m128_f32[0];
		LMFDir.y = 0.0;
		LMFDir.z = LMDir.m128_f32[2];
		//碰撞方向Y向量长度
		float TotalDirY = sqrt(pow(LMFDir.x, 2) + pow(LMFDir.z, 2));

		//向量夹角cos值
		//cos(A,B)=(A.x*B.x+A.y*B.y+A.z*B.z)/(|A|*|B|)
		if (TotalSpeedY > 0.0&&TotalDirY > 0.0)
		{
			float LMcos = (LMFspeed.x*LMFDir.x + LMFspeed.z*LMFDir.z) / (TotalDirY*TotalSpeedY);
			LMDir = LMDir * abs(LMcos) * TotalSpeedY;
		}
		else if (TotalSpeedY == 0.0)
		{
			LMDir = XMVectorZero();
		}
		if (!cmpFloat4(mAllRitems[id]->WorldBounds.Orientation, mAllRitems[id]->LastPos.Orientation, 0.0f))
		{
			LMDir += playerspeed * mAllRitems[id]->ColliderLM;
		}


		//旋转向量
		XMVECTOR LMRot = mAllRitems[id]->RotateLM;
				LMRot = XMVector3Normalize(LMRot);

		float LMPitch;
		float LMroll;
		if (mAllRitems[id]->RotateFcaeCount > 0)
		{
			if (mAllRitems[id]->WorldBounds.Center.y - mAllRitems[id]->LastPos.Center.y > mAllRitems[id]->GravityLM)
				LMDir.m128_f32[1] = mAllRitems[id]->WorldBounds.Center.y - mAllRitems[id]->LastPos.Center.y + 2 * mAllRitems[id]->GravityLM;
			else
				LMDir.m128_f32[1] = mAllRitems[id]->LastPos.Center.y - mAllRitems[id]->WorldBounds.Center.y;

			float LMRotSpeed = XM_PI / 90.0f;
			mAllRitems[id]->GravityLM = 0.0f;
			LMRot = XMVector3TransformNormal(LMRot, toL);
			LMPitch = (float)(atan((double)LMRot.m128_f32[2] / sqrt(pow((double)LMRot.m128_f32[1], 2) + pow((double)LMRot.m128_f32[0], 2))));
			LMroll = (float)(atan((double)LMRot.m128_f32[0] / (double)LMRot.m128_f32[1]));
			/*if (LMPitch > LMRotSpeed)
				LMPitch = LMRotSpeed;
			if (LMroll > LMRotSpeed)
				LMroll = LMRotSpeed;
			if (LMPitch < -LMRotSpeed)
				LMPitch = -LMRotSpeed;
			if (LMroll < -LMRotSpeed)
				LMroll = -LMRotSpeed;
				*/
			LMPitch = 0.2f*LMPitch;
			LMroll = 0.2f*LMroll;
		}
		else
		{
			LMroll = 0.0f;
			LMPitch = 0.0f;
			LMDir.m128_f32[1] = 0.0f;
		}


		//XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixRotationRollPitchYawFromVector(XMVectorSet(0.0f, LMRot.m128_f32[1], 0.0f, 0.0f))*XMMatrixRotationRollPitchYawFromVector(-LMRot/*-LMPitch,0.0f,-LMroll*/)*W*XMMatrixTranslationFromVector(LMDir));
		XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixRotationRollPitchYaw(LMPitch, 0.0f, -LMroll)*W*XMMatrixTranslationFromVector(LMDir));
		mAllRitems[id]->ColliderLM = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		mAllRitems[id]->RotateLM = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		mAllRitems[id]->CarLM = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		mAllRitems[id]->ColliderRiCount = 0;
		mAllRitems[id]->RotateFcaeCount = 0;
	}
}
void TexColumnsApp::LMGravity()
{
	//重力模拟

	for (int i = 0; i < itemCount; i++)
	{
		if (mAllRitems[i]->Gravity == false)
			continue;
		//if (GetAsyncKeyState('G') & 0x8000)
		{
			//if (mAllRitems[i]->GravityLM <= 0.5&&mAllRitems[i]->RotateFcaeCount == 0)
				mAllRitems[i]->GravityLM += globalGravity * playerspeed;
			/*else if (mAllRitems[i]->RotateFcaeCount > 0)
			{
				mAllRitems[i]->RotateFcaeCount = 0;
			}*/
			//物理
			XMMATRIX world = XMLoadFloat4x4(&mAllRitems[i]->World);
			XMStoreFloat4x4(&mAllRitems[i]->World,
				world*XMMatrixTranslation(0.0f, -mAllRitems[i]->GravityLM, 0.0f));
			mAllRitems[i]->NumFramesDirty = gNumFrameResources; //3;
		}
	}

}
void TexColumnsApp::PlayerTurret(int id,float dt)
{
	//第一人称摄像机更新+炮台动作
	if (FirstCamera)
	{
		//计算朝向
		GetCursorPos(&m_mouse);
		int cx = rect.right - rect.left;
		cx = (int)rect.left + (int)cx / 2 + 2;
		int cy = rect.bottom - rect.top;
		cy = (int)rect.top + (int)cy / 2 + 2;

		dx = m_mouse.x - cx;
		if (abs(dx) <= 1)
			dx = 0.0f;
		dy = m_mouse.y - cy;
		if (abs(dy) <= 1)
			dy = 0.0f;

		dx = XMConvertToRadians(0.25f*(dx));
		dy = XMConvertToRadians(0.25f*(dy));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);

		SetCursorPos(cx, cy);

		mAllRitems[id]->dx += dx;
		mAllRitems[id]->dy += dy;

		//限制俯仰
		if (mAllRitems[id]->dy > XM_PI / 9.0f)
		{
			mAllRitems[id]->dy = XM_PI / 9.0f;
		}
		else if (mAllRitems[id]->dy < -XM_PI / 4.0f)
		{
			mAllRitems[id]->dy = -XM_PI / 4.0f;
		}
		XMFLOAT3 POS(0.0, 0.0, 0.0);
		XMFLOAT3 TGT(0.0, 0.0, 1.0);
		XMFLOAT3 UP(0.0, 1.0, 0.0);
		mCamera.LookAt(POS, TGT, UP);
		mCamera.Pitch(mAllRitems[id]->dy);
		mCamera.RotateY(mAllRitems[id]->dx);

		XMMATRIX W = XMLoadFloat4x4(&mAllRitems[id-1]->World);
		XMVECTOR E = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		E = XMVector3TransformNormal(E, W);
		float CL = 0.3f;
		mCamera.SetPosition(mAllRitems[id-1]->World._41 + CL * E.m128_f32[0], mAllRitems[id-1]->World._42 + CL * E.m128_f32[1], mAllRitems[id-1]->World._43 + CL * E.m128_f32[2]);
		XMMATRIX W1 = XMLoadFloat4x4(&mAllRitems[id]->World);
		XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixRotationX(mAllRitems[id]->dy)*XMMatrixRotationY(mAllRitems[id]->dx)*XMMatrixTranslationFromVector(mCamera.GetPosition()));
		dy = 0.0f;
		dx = 0.0f;
		mCamera.Lift(0.5f);
		mCamera.Walk(0.2f);
		mCamera.Strafe(-0.2f);

		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}
	else if (!FirstCamera)
	{
		XMMATRIX W = XMLoadFloat4x4(&mAllRitems[id-1]->World);
		XMVECTOR E = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		E = XMVector3TransformNormal(E, W);
		float CL = 0.3f;
		XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixRotationX(mAllRitems[id]->dy)*XMMatrixRotationY(mAllRitems[id]->dx));
		mAllRitems[id]->World._41 = mAllRitems[id-1]->World._41 + CL * E.m128_f32[0];
		mAllRitems[id]->World._42 = mAllRitems[id-1]->World._42 + CL * E.m128_f32[1];
		mAllRitems[id]->World._43 = mAllRitems[id-1]->World._43 + CL * E.m128_f32[2];
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}



}
void TexColumnsApp::AITurret(int id, float dt)
{
	XMMATRIX W = XMLoadFloat4x4(&mAllRitems[id]->World);
	XMVECTOR E = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	E = XMVector3TransformNormal(E, W);
	float CL = 0.3f;

	if (!mAllRitems[id - 1]->crashed)
	{
		mAllRitems[id]->dy = GameAIs[mAllRitems[id]->AIcontrol].dy;
		mAllRitems[id]->dx = GameAIs[mAllRitems[id]->AIcontrol].dx;

		//限制俯仰
		if (mAllRitems[id]->dy > XM_PI / 9.0f)
		{
			mAllRitems[id]->dy = XM_PI / 9.0f;
		}
		else if (mAllRitems[id]->dy < -XM_PI / 4.0f)
		{
			mAllRitems[id]->dy = -XM_PI / 4.0f;
		}
	}
	XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixRotationX(mAllRitems[id]->dy)*XMMatrixRotationY(mAllRitems[id]->dx));
	mAllRitems[id]->World._41 = mAllRitems[id-1]->World._41 + CL * E.m128_f32[0];
	mAllRitems[id]->World._42 = mAllRitems[id-1]->World._42 + CL * E.m128_f32[1];
	mAllRitems[id]->World._43 = mAllRitems[id-1]->World._43 + CL * E.m128_f32[2];
	mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
}
void TexColumnsApp::LMBullet(int id, float dt, float BulletSpeed, float ExistedTime)
{
	//爆炸动画
	if (mAllRitems[id + 1]->fired)
	{
		mAllRitems[id+1]->ExistedTime += dt;
		if (mAllRitems[id + 1]->ExistedTime > ExistedTime / 2.0)
		{
			mAllRitems[id+1]->ExistedTime = 0.0f;
			mAllRitems[id+1]->fired = false;
		}
	}
	else if (!mAllRitems[id + 1]->fired)
	{
		XMStoreFloat4x4(&mAllRitems[id + 1]->World, XMMatrixTranslation(0.0, -10.0, 0.0));
		mAllRitems[id + 1]->NumFramesDirty = gNumFrameResources; //3;
	}
	//弹头动画
	if (mAllRitems[id]->AIcontrol == playerID && mAllRitems[id]->ExistedTime > 0.0f&&FirstCamera)
		mCamera.Walk(-0.1f + mAllRitems[id]->ExistedTime);
	if (mAllRitems[id]->fired&&mAllRitems[id]->ExistedTime == 0.0f)
	{
		XMMATRIX TW = XMLoadFloat4x4(&mAllRitems[id - 1]->World);
		XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0, 0.0, 0.5f)*XMMatrixRotationX(0.015f*XM_PI*((float)(rand() % 15 - 7) / 7.0f))*XMMatrixRotationY(0.015f*XM_PI*((float)(rand() % 15 - 7) / 7.0f))*XMMatrixTranslation(0.0f, 0.45f, 1.0f)*TW);
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
		XMFLOAT3 BulletPos = { mAllRitems[id]->World._41,mAllRitems[id]->World._42,mAllRitems[id]->World._43 };
		PlayEvnWav(BulletPos, 1);
		mAllRitems[id]->ExistedTime += dt;

	}
	else if (mAllRitems[id]->fired&&mAllRitems[id]->ExistedTime > 0.0f&&!mAllRitems[id]->hited)
	{
		XMMATRIX WB = XMLoadFloat4x4(&mAllRitems[id]->World);
		float f = GraphBullet(id);
		if (f > BulletSpeed)
		{
			mAllRitems[id]->hitedRitemID = -1;
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0, 0.0, BulletSpeed)*WB);
			mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
			mAllRitems[id]->ExistedTime += dt;
			if (mAllRitems[id]->ExistedTime > ExistedTime)
			{
				mAllRitems[id]->ExistedTime = 0.0f;
				mAllRitems[id]->fired = false;
			}
		}
		else if (f <= BulletSpeed)
		{
			XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0, 0.0, f-0.5f)*WB);
			mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
			XMFLOAT3 BulletPos = { mAllRitems[id]->World._41,mAllRitems[id]->World._42,mAllRitems[id]->World._43 };
			PlayEvnWav(BulletPos, 0);
			//Play3DWAV(BulletPos, L"res/explo1.wav");
			mAllRitems[id]->fired = false;
			mAllRitems[id]->hited = true;
			if (mAllRitems[id]->hitedRitemID > -1)
			{
				//if (mAllRitems[mAllRitems[id]->hitedRitemID]->AIcontrol != playerID||!FirstCamera)
				{
					XMMATRIX BW = XMLoadFloat4x4(&mAllRitems[id]->World);
					XMStoreFloat4x4(&mAllRitems[id + 1]->World, BW);
					mAllRitems[id + 1]->NumFramesDirty = gNumFrameResources; //3;
					mAllRitems[id + 1]->fired = true;
				}
			}
			else if (mAllRitems[id]->hitedRitemID == -1)
			{
				XMMATRIX BW = XMLoadFloat4x4(&mAllRitems[id]->World);
				XMStoreFloat4x4(&mAllRitems[id + 1]->World, BW);
				mAllRitems[id + 1]->NumFramesDirty = gNumFrameResources; //3;
				mAllRitems[id + 1]->fired = true;
			}
		}
	}
	else if (mAllRitems[id]->hited&&mAllRitems[id]->ExistedTime > 0.0f)
	{
		XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0, -10.0, 0.0));
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
		mAllRitems[id]->ExistedTime += dt;
		if (mAllRitems[id]->ExistedTime > ExistedTime)
		{
			mAllRitems[id]->ExistedTime = 0.0f;
			mAllRitems[id]->hited = false;
		}
	}
	else if (!mAllRitems[id]->fired)
	{
		XMStoreFloat4x4(&mAllRitems[id]->World, XMMatrixTranslation(0.0, -10.0, 0.0));
		mAllRitems[id]->NumFramesDirty = gNumFrameResources; //3;
	}
}
void TexColumnsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());


	mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);


	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

	mCommandList->SetPipelineState(mPSOs["HPSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::HPSprites]);



	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);




	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);
	mCommandList->SetPipelineState(mPSOs["alpha_1"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::alpha_1]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);


	mCommandList->SetPipelineState(mPSOs["debug"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Debug]);

	//DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TexColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
	if (state == 0 && (btnState & MK_LBUTTON) != 0)
	{
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->chara == 111)
			{
				if (pickButton(dsx, dsy, i))
				{
					showHelper = false;
					teamChoosing = true;
				}
			}
			else if (mAllRitems[i]->chara == 119 && teamChoosing)
			{
				if (pickButton(dsx, dsy, i))
				{
					teamChoosing = false;
					playerID = rand() % 3 + 3;
					state = 1;
					FirstCamera = true;

					int idc = GameAIs[playerID].CtrlItemId;
					mAllRitems[idc + 5]->Geo = mGeometries["shapeGeo"].get();
					mAllRitems[idc + 5]->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
					mAllRitems[idc + 5]->IndexCount = mAllRitems[idc + 5]->Geo->DrawArgs["UIrect"].IndexCount;
					mAllRitems[idc + 5]->StartIndexLocation = mAllRitems[idc + 5]->Geo->DrawArgs["UIrect"].StartIndexLocation;
					mAllRitems[idc + 5]->BaseVertexLocation = mAllRitems[idc + 5]->Geo->DrawArgs["UIrect"].BaseVertexLocation;
					mRitemLayer[(int)RenderLayer::AlphaTested].push_back(mAllRitems[idc + 5].get());

					GetWindowRect(mhMainWnd, &rect);
					int cx = rect.right - rect.left;
					int cy = rect.bottom - rect.top;
					SetCursorPos((int)rect.left + (int)cx / 2 + 2, (int)rect.top + (int)cy / 2 + 2);

					PlayBGM(L"res/EasternWind.wav");

				}
			}
			else if (mAllRitems[i]->chara == 120 && teamChoosing)
			{
				if (pickButton(dsx, dsy, i))
				{
					teamChoosing = false;
					playerID = rand() % 3;
					state = 1;
					FirstCamera = true;

					int idc = GameAIs[playerID].CtrlItemId;
					mAllRitems[idc + 5]->Geo = mGeometries["shapeGeo"].get();
					mAllRitems[idc + 5]->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
					mAllRitems[idc + 5]->IndexCount = mAllRitems[idc + 5]->Geo->DrawArgs["UIrect"].IndexCount;
					mAllRitems[idc + 5]->StartIndexLocation = mAllRitems[idc + 5]->Geo->DrawArgs["UIrect"].StartIndexLocation;
					mAllRitems[idc + 5]->BaseVertexLocation = mAllRitems[idc + 5]->Geo->DrawArgs["UIrect"].BaseVertexLocation;
					mRitemLayer[(int)RenderLayer::AlphaTested].push_back(mAllRitems[idc + 5].get());

					GetWindowRect(mhMainWnd, &rect);
					int cx = rect.right - rect.left;
					int cy = rect.bottom - rect.top;
					SetCursorPos((int)rect.left + (int)cx / 2 + 2, (int)rect.top + (int)cy / 2 + 2);
					PlayBGM(L"res/EasternWind.wav");
				}
			}
			else if (mAllRitems[i]->chara == 121 && teamChoosing)
			{
				if (pickButton(dsx, dsy, i))
				{
					teamChoosing = false;
					playerID = 6;
					state = 1;
					FirstCamera = false;

					mCamera.SetPosition(0.0f, 10.0f, 0.0f);
					XMFLOAT3 POS(0.0, 0.0, 0.0);
					XMFLOAT3 TGT(0.0, 0.0, 1.0);
					XMFLOAT3 UP(0.0, 1.0, 0.0);
					mCamera.LookAt(POS, TGT, UP);
					mCamera.UpdateViewMatrix();

					GetWindowRect(mhMainWnd, &rect);
					int cx = rect.right - rect.left;
					int cy = rect.bottom - rect.top;
					SetCursorPos((int)rect.left + (int)cx / 2 + 2, (int)rect.top + (int)cy / 2 + 2);
					PlayBGM(L"res/EasternWind.wav");
				}
			}
			else if (mAllRitems[i]->chara == 112)
			{
				if (pickButton(dsx, dsy, i))
				{
					showHelper = !showHelper;
					teamChoosing = false;
				}
			}
			else if (mAllRitems[i]->chara == 113)
			{
				if (pickButton(dsx, dsy, i))
					exit(0);
			}
				
		}
		//pick(x, y);

	}
	if (state == 1 && (btnState & MK_LBUTTON) != 0)
	{
		playercontrol[8] = 1;
		//pick(x, y);
	}
	else if (state == 1 && (btnState & MK_RBUTTON) != 0)
	{
		FirstCamera = !FirstCamera;
		if (FirstCamera)
			for (int i = 0; i < itemCount; i++)
			{
				if (mAllRitems[i]->AIcontrol == playerID && mAllRitems[i]->chara == 3)
				{
					XMFLOAT3 POS(0.0, 0.0, 0.0);
					XMFLOAT3 TGT(0.0, 0.0, 1.0);
					XMFLOAT3 UP(0.0, 1.0, 0.0);
					mCamera.LookAt(POS, TGT, UP);
					mCamera.Pitch(mAllRitems[i + 1]->dy);
					mCamera.RotateY(mAllRitems[i + 1]->dx);
				}
			}
		/*else if (!FirstCamera)
		{
			GetWindowRect(mhMainWnd, &rect);
			int cx = rect.right - rect.left;
			int cy = rect.bottom - rect.top;
			SetCursorPos((int)rect.left + (int)cx / 2 + 2, (int)rect.top + (int)cy / 2 + 2);
		}*/
		//pick(x, y, 1);
		//pick(x, y, 3);
		//pick(x, y);
	}
	if (state == 2 && (btnState & MK_LBUTTON) != 0)
	{
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->chara == 117)
			{
				if (pickButton(dsx, dsy, i))
				{
					XMFLOAT3 POS(0.0, 0.0, 0.0);
					XMFLOAT3 TGT(0.0, 0.0, 1.0);
					XMFLOAT3 UP(0.0, 1.0, 0.0);
					mCamera.LookAt(POS, TGT, UP);
					FirstCamera = false;
					state = -1;



				}
			}
		}
	}
	if (state == 3 && (btnState & MK_LBUTTON) != 0)
	{
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->chara == 116)
			{
				if (pickButton(dsx, dsy, i))
				{
					state = 1;
					if (playerID != 6)
						FirstCamera = true;
					else
						FirstCamera = false;
					GetWindowRect(mhMainWnd, &rect);
					int cx = rect.right - rect.left;
					int cy = rect.bottom - rect.top;
					SetCursorPos((int)rect.left + (int)cx / 2 + 2, (int)rect.top + (int)cy / 2 + 2);
				}
			}
			else if (mAllRitems[i]->chara == 117)
			{
				if (pickButton(dsx, dsy, i))
				{
					XMFLOAT3 POS(0.0, 0.0, 0.0);
					XMFLOAT3 TGT(0.0, 0.0, 1.0);
					XMFLOAT3 UP(0.0, 1.0, 0.0);
					mCamera.LookAt(POS, TGT, UP);
					FirstCamera = false;
					state = -1;
				}
			}
		}
	}
    SetCapture(mhMainWnd);
}

void TexColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	if (state == 1 && (btnState & MK_LBUTTON) == 0)
	{
		playercontrol[8] = 0;
	}

	//if (0 < x && x < 100 && 0 < y < 100)
	/*{
		if (squareCount < 4) {
			XMStoreFloat4x4(&mAllRitems[squareCount]->World,
				XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, 1.0f, 0.0f + squareCount * 3.0f));
			mAllRitems[squareCount]->NumFramesDirty =  gNumFrameResources; //3;
			squareCount++;
		}

	}*/
	/*int id = 3;
	XMMATRIX world = XMLoadFloat4x4(&mAllRitems[id]->World);

	XMStoreFloat4x4(&mAllRitems[id]->World,
		world*XMMatrixTranslation(0.0f, 1.0f, 3.0f));
	mAllRitems[id]->NumFramesDirty = gNumFrameResources;*/ //3;

    ReleaseCapture();
}

void TexColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	dsx = x;
	dsy = y;
	if ((btnState & MK_LBUTTON) != 0 && !FirstCamera&&(state == 1||state==2))
	{
		// Make each pixel correspond to a quarter of a degree.
		dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
		dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
		//if (FirstCamera)
			//SetCursorPos(x, y);
		mLastMousePos.x = x;
		mLastMousePos.y = y;

	}

   /* if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.05f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }
	
    mLastMousePos.x = x;
    mLastMousePos.y = y;*/
}
 
void TexColumnsApp::OnKeyboardInput(const GameTimer& gt)
{
	float dt = gt.DeltaTime();
	int tabstate = 0;
	if (GetAsyncKeyState(VK_TAB) & 0x7FFF)
	{
		if (state == 1)
		{
			state = 3;
			FirstCamera = false;
		}
	}



	if ((!FirstCamera && state == 1)||state==2)
	{
	//	if (playerID == 6)
		{
			if (GetAsyncKeyState(VK_LSHIFT) & 0x8000)
				dt = 2 * dt;

			if (GetAsyncKeyState('W') & 0x8000)//上
				mCamera.Walk(10.0f*dt);

			if (GetAsyncKeyState('S') & 0x8000)//下
				mCamera.Walk(-10.0f*dt);

			if (GetAsyncKeyState('A') & 0x8000)//左
				mCamera.Strafe(-10.0f*dt);

			if (GetAsyncKeyState('D') & 0x8000)//右
				mCamera.Strafe(10.0f*dt);

			if (GetAsyncKeyState(' ') & 0x8000)
				mCamera.Lift(10.0f*dt);

			if (GetAsyncKeyState(VK_LCONTROL) & 0x8000)
				mCamera.Lift(-10.0f*dt);

		}

	}

	if (GetAsyncKeyState(VK_F1) & 0x8000 && !FirstCamera)
		{
			const XMFLOAT3 pos(0.0f, 0.0f, 0.0f);
			const XMFLOAT3 target(0.0f, 0.0f, 1.0f);
			const XMFLOAT3 up(0.0f, 1.0f, 0.0f);
			mCamera.LookAt(pos, target, up);
		}

	if (GetAsyncKeyState('W') & 0x8000)//上
		playercontrol[0] = 1;

	if (GetAsyncKeyState('S') & 0x8000)//下
		playercontrol[1] = 1;

	if (GetAsyncKeyState('A') & 0x8000)//左
		playercontrol[2] = 1;

	if (GetAsyncKeyState('D') & 0x8000)//右
		playercontrol[3] = 1;

	if (GetAsyncKeyState('Q') & 0x8000)//左旋
		playercontrol[4] = 1;

	if (GetAsyncKeyState('E') & 0x8000)//右旋
		playercontrol[5] = 1;

	if (GetAsyncKeyState('T') & 0x8000)
		playercontrol[6] = 1;

	if (GetAsyncKeyState('G') & 0x8000)
		playercontrol[7] = 1;

}
 
void TexColumnsApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void TexColumnsApp::AnimateMaterials(const GameTimer& gt)
{
	
}

void TexColumnsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	//for(auto& e : mAllRitems)
	for ( int i = 0; i < itemCount; i++)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		auto &e = mAllRitems[i];
		if (e->invisible == false)
		{
			
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			//e->NumFramesDirty--;
		}
	}
}

void TexColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TexColumnsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();

	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void TexColumnsApp::LoadTextures()
{
	auto ScarTex = std::make_unique<Texture>();
	ScarTex->Name = "ScarTex";
	ScarTex->Filename = L"res/Scar.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), ScarTex->Filename.c_str(),
		ScarTex->Resource, ScarTex->UploadHeap));

	auto ScarEnemyTex = std::make_unique<Texture>();
	ScarEnemyTex->Name = "ScarEnemyTex";
	ScarEnemyTex->Filename = L"res/ScarEnemy.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), ScarEnemyTex->Filename.c_str(),
		ScarEnemyTex->Resource, ScarEnemyTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"res/gameHelper.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"res/mapuv.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tileTex->Filename.c_str(),
		tileTex->Resource, tileTex->UploadHeap));

	auto tagDianzuTex = std::make_unique<Texture>();
	tagDianzuTex->Name = "tagDianzuTex";
	tagDianzuTex->Filename = L"res/mainmenu.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tagDianzuTex->Filename.c_str(),
		tagDianzuTex->Resource, tagDianzuTex->UploadHeap));

	auto UITex = std::make_unique<Texture>();
	UITex->Name = "UITex";
	UITex->Filename = L"res/mainmenu.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), UITex->Filename.c_str(),
		UITex->Resource, UITex->UploadHeap));

	auto ballastTex = std::make_unique<Texture>();
	ballastTex->Name = "ballastTex";
	ballastTex->Filename = L"res/ballasttex_png.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), ballastTex->Filename.c_str(),
		ballastTex->Resource, ballastTex->UploadHeap));

	auto SkyTex = std::make_unique<Texture>();
	SkyTex->Name = "SkyTex";
	SkyTex->Filename = L"res/SkyBoxTexture.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), SkyTex->Filename.c_str(),
		SkyTex->Resource, SkyTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"res/treeArray2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));
	
	auto groundTex = std::make_unique<Texture>();
	groundTex->Name = "groundTex";
	groundTex->Filename = L"res/groundTex.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), groundTex->Filename.c_str(),
		groundTex->Resource, groundTex->UploadHeap));

	auto baseTex = std::make_unique<Texture>();
	baseTex->Name = "baseTex";
	baseTex->Filename = L"res/baseTex.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), baseTex->Filename.c_str(),
		baseTex->Resource, baseTex->UploadHeap));

	auto baseTexEnemy = std::make_unique<Texture>();
	baseTexEnemy->Name = "baseTexEnemy";
	baseTexEnemy->Filename = L"res/baseTexEnemy.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), baseTexEnemy->Filename.c_str(),
		baseTexEnemy->Resource, baseTexEnemy->UploadHeap));

	auto explo2 = std::make_unique<Texture>();
	explo2->Name = "explo2";
	explo2->Filename = L"res/explo2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), explo2->Filename.c_str(),
		explo2->Resource, explo2->UploadHeap));

	auto BlueWin = std::make_unique<Texture>();
	BlueWin->Name = "BlueWin";
	BlueWin->Filename = L"res/BlueWin.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), BlueWin->Filename.c_str(),
		BlueWin->Resource, BlueWin->UploadHeap));

	auto RedWin = std::make_unique<Texture>();
	RedWin->Name = "RedWin";
	RedWin->Filename = L"res/RedWin.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), RedWin->Filename.c_str(),
		RedWin->Resource, RedWin->UploadHeap));

	auto aim = std::make_unique<Texture>();
	aim->Name = "aim";
	aim->Filename = L"res/aim.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), aim->Filename.c_str(),
		aim->Resource, aim->UploadHeap));

	auto CarDir = std::make_unique<Texture>();
	CarDir->Name = "CarDir";
	CarDir->Filename = L"res/CarDir.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), CarDir->Filename.c_str(),
		CarDir->Resource, CarDir->UploadHeap));

	auto HPstripB = std::make_unique<Texture>();
	HPstripB->Name = "HPstripB";
	HPstripB->Filename = L"res/HPstripB.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), HPstripB->Filename.c_str(),
		HPstripB->Resource, HPstripB->UploadHeap));

	auto HPstripR = std::make_unique<Texture>();
	HPstripR->Name = "HPstripR";
	HPstripR->Filename = L"res/HPstripR.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), HPstripR->Filename.c_str(),
		HPstripR->Resource, HPstripR->UploadHeap));

	auto BlueBaseUITex = std::make_unique<Texture>();
	BlueBaseUITex->Name = "BlueBaseUITex";
	BlueBaseUITex->Filename = L"res/BlueBaseUITex.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), BlueBaseUITex->Filename.c_str(),
		BlueBaseUITex->Resource, BlueBaseUITex->UploadHeap));

	auto RedBaseUITex = std::make_unique<Texture>();
	RedBaseUITex->Name = "RedBaseUITex";
	RedBaseUITex->Filename = L"res/RedBaseUITex.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), RedBaseUITex->Filename.c_str(),
		RedBaseUITex->Resource, RedBaseUITex->UploadHeap));

	auto grayBK = std::make_unique<Texture>();
	grayBK->Name = "grayBK";
	grayBK->Filename = L"res/grayBK.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grayBK->Filename.c_str(),
		grayBK->Resource, grayBK->UploadHeap));

	auto buttons = std::make_unique<Texture>();
	buttons->Name = "buttons";
	buttons->Filename = L"res/buttons.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), buttons->Filename.c_str(),
		buttons->Resource, buttons->UploadHeap));

	auto gameHelper = std::make_unique<Texture>();
	gameHelper->Name = "gameHelper";
	gameHelper->Filename = L"res/gameHelper.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), gameHelper->Filename.c_str(),
		gameHelper->Resource, gameHelper->UploadHeap));

	mTextures[ScarTex->Name] = std::move(ScarTex);
	mTextures[ScarEnemyTex->Name] = std::move(ScarEnemyTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[tileTex->Name] = std::move(tileTex);
	mTextures[tagDianzuTex->Name] = std::move(tagDianzuTex);
	mTextures[UITex->Name] = std::move(UITex);
	mTextures[ballastTex->Name] = std::move(ballastTex);
	mTextures[SkyTex->Name] = std::move(SkyTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
	mTextures[groundTex->Name] = std::move(groundTex);
	mTextures[baseTex->Name] = std::move(baseTex);
	mTextures[baseTexEnemy->Name] = std::move(baseTexEnemy);
	mTextures[explo2->Name] = std::move(explo2);
	mTextures[BlueWin->Name] = std::move(BlueWin);
	mTextures[RedWin->Name] = std::move(RedWin);
	mTextures[aim->Name] = std::move(aim);
	mTextures[CarDir->Name] = std::move(CarDir);
	mTextures[HPstripB->Name] = std::move(HPstripB);
	mTextures[HPstripR->Name] = std::move(HPstripR);
	mTextures[BlueBaseUITex->Name] = std::move(BlueBaseUITex);
	mTextures[RedBaseUITex->Name] = std::move(RedBaseUITex);
	mTextures[grayBK->Name] = std::move(grayBK);
	mTextures[buttons->Name] = std::move(buttons);
	mTextures[gameHelper->Name] = std::move(gameHelper);
}

void TexColumnsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 
        1,  // number of descriptors
        0); // register t0

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0); // register b0
    slotRootParameter[2].InitAsConstantBufferView(1); // register b1
    slotRootParameter[3].InitAsConstantBufferView(2); // register b2

	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TexColumnsApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	
	srvHeapDesc.NumDescriptors = 24;//贴图总数，记得改！
	
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto ScarTex = mTextures["ScarTex"]->Resource;
	auto ScarEnemyTex = mTextures["ScarEnemyTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto tileTex = mTextures["tileTex"]->Resource;
	auto tagDianzuTex = mTextures["tagDianzuTex"]->Resource;

	auto UITex = mTextures["UITex"]->Resource;
	auto ballastTex = mTextures["ballastTex"]->Resource;
	auto SkyTex = mTextures["SkyTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
	auto groundTex = mTextures["groundTex"]->Resource;

	auto baseTex = mTextures["baseTex"]->Resource;
	auto baseTexEnemy = mTextures["baseTexEnemy"]->Resource;
	auto explo2 = mTextures["explo2"]->Resource;
	auto BlueWin = mTextures["BlueWin"]->Resource;
	auto RedWin = mTextures["RedWin"]->Resource;

	auto aim = mTextures["aim"]->Resource;
	auto CarDir = mTextures["CarDir"]->Resource;
	auto HPstripB = mTextures["HPstripB"]->Resource;
	auto HPstripR = mTextures["HPstripR"]->Resource;
	auto BlueBaseUITex = mTextures["BlueBaseUITex"]->Resource;

	auto RedBaseUITex = mTextures["RedBaseUITex"]->Resource;
	auto grayBK = mTextures["grayBK"]->Resource;
	auto buttons = mTextures["buttons"]->Resource;
	auto gameHelper = mTextures["gameHelper"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = ScarTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = ScarTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(ScarTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = ScarEnemyTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = ScarEnemyTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(ScarEnemyTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);
	
	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tagDianzuTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tagDianzuTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tagDianzuTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = UITex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = UITex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(UITex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = ballastTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = ballastTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(ballastTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = SkyTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = SkyTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(SkyTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = groundTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = groundTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(groundTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = baseTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = baseTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(baseTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = baseTexEnemy->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = baseTexEnemy->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(baseTexEnemy.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = explo2->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = explo2->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(explo2.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = BlueWin->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = BlueWin->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(BlueWin.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = RedWin->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = RedWin->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(RedWin.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = aim->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = aim->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(aim.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = CarDir->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = CarDir->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(CarDir.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = HPstripB->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = HPstripB->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(HPstripB.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = HPstripR->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = HPstripR->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(HPstripR.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = BlueBaseUITex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = BlueBaseUITex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(BlueBaseUITex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = RedBaseUITex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = RedBaseUITex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(RedBaseUITex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = grayBK->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = grayBK->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(grayBK.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = buttons->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = buttons->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(buttons.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = gameHelper->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = gameHelper->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(gameHelper.Get(), &srvDesc, hDescriptor);


}

void TexColumnsApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	//单面，无光，无A通
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");
	
	//单面，高光，A通
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\NoneLighting.hlsl", alphaTestDefines, "PS", "ps_5_1");

	//菜单
	mShaders["debugVS"] = d3dUtil::CompileShader(L"Shaders\\UI.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugPS"] = d3dUtil::CompileShader(L"Shaders\\UI.hlsl", nullptr, "PS", "ps_5_1");

	//mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\NoneLighting.hlsl", nullptr, "VS", "vs_5_1");
	//双面，高光，A通
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\NoneLighting.hlsl", defines, "PS", "ps_5_1");

	//双面，高光，A通
	mShaders["alpha_1PS"]= d3dUtil::CompileShader(L"Shaders\\NoneLighting.hlsl", alphaTestDefines, "PS", "ps_5_1");
	//mShaders["alpha_1GS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");


	//公告牌，双面，高光，A通
	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

	//公告牌，双面，高光，无A通
	mShaders["HPSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\HPstrip.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["HPSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\HPstrip.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["HPSpritePS"] = d3dUtil::CompileShader(L"Shaders\\HPstrip.hlsl", defines, "PS", "ps_5_1");

	/*mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_0");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_0");*/
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};


	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

}
void TexColumnsApp::BuildTreeSpritesGeometry()
{
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
		XMFLOAT2 TexC;
	};

	//static const int treeCount = 2;
	std::array<TreeSpriteVertex, 2> vertices;
		vertices[0].Pos = XMFLOAT3(0.0f, 0.0f, 0.0f);
		vertices[0].Size = XMFLOAT2(1.0f, 1.0f);
		vertices[0].TexC = XMFLOAT2(0.0f, 0.0f);
		vertices[1].Pos = XMFLOAT3(0.0f, 0.0f, 0.0f);
		vertices[1].Size = XMFLOAT2(2.0f, 0.2f);
		vertices[1].TexC = XMFLOAT2(0.0f, 0.0f);

	std::array<std::uint32_t, 2> indices =
	{
		0,1
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;


	SubmeshGeometry explopointsubmesh;
	explopointsubmesh.IndexCount = 1;
	explopointsubmesh.StartIndexLocation = 0;
	explopointsubmesh.BaseVertexLocation = 0;


	SubmeshGeometry HPstripsubmesh;
	HPstripsubmesh.IndexCount = 1;
	HPstripsubmesh.StartIndexLocation = 1;
	HPstripsubmesh.BaseVertexLocation = 0;

	geo->DrawArgs["explopoint"] = explopointsubmesh;
	geo->DrawArgs["HPstrip"] = HPstripsubmesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}


void TexColumnsApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(2.0f, 2.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateOBJ(0.4f, 0.4f, 0.4f, "res/map4.obj",0);
	GeometryGenerator::MeshData ballast = geoGen.CreateOBJ(0.2f, 0.2f, 0.2f, "res/ballastTRI.obj",0);
	GeometryGenerator::MeshData Scar = geoGen.CreateOBJ(0.2f, 0.2f, 0.2f, "res/Scar.obj",0);
	GeometryGenerator::MeshData ScarTurret = geoGen.CreateOBJ(0.2f, 0.2f, 0.2f, "res/ScarTurret.obj",0);
	GeometryGenerator::MeshData bullet = geoGen.CreateOBJ(0.2f, 0.2f, 0.2f, "res/bullet.obj",0);
	GeometryGenerator::MeshData UIrect = geoGen.CreateUI(-0.3f, 0.3f, 0.3f, -0.3f, 0.5f);
	GeometryGenerator::MeshData quad = geoGen.CreateOBJ(0.4f, 0.4f, 0.4f, "res/ground.obj", 0);
	GeometryGenerator::MeshData collider = geoGen.CreateOBJ(0.4f, 0.4f, 0.4f, "res/map4collider.obj",0);
	GeometryGenerator::MeshData quadCollider = geoGen.CreateOBJ(0.4f, 0.4f, 0.4f, "res/ground1.obj", 0);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData base = geoGen.CreateOBJ(0.4f, 0.4f, 0.4f, "res/base.obj", 0);
	GeometryGenerator::MeshData explo2 = geoGen.CreateOBJ(0.4f, 0.4f, 0.4f, "res/explo2.obj", 0);
	GeometryGenerator::MeshData grass= geoGen.CreateOBJ(0.4f, 0.4f, 0.4f, "res/grass.obj", 0);


	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	//顶点偏移
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT ballastVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT ScarVertexOffset = ballastVertexOffset + (UINT)ballast.Vertices.size();
	UINT ScarTurretVertexOffset = ScarVertexOffset + (UINT)Scar.Vertices.size();
	UINT bulletVertexOffset = ScarTurretVertexOffset + (UINT)ScarTurret.Vertices.size();
	UINT UIrectVertexOffset = bulletVertexOffset + (UINT)bullet.Vertices.size();
	UINT quadVertexOffset = UIrectVertexOffset + (UINT)UIrect.Vertices.size();
	UINT colliderVertexOffset = quadVertexOffset + (UINT)quad.Vertices.size();
	UINT sphereVertexOffset = colliderVertexOffset + (UINT)collider.Vertices.size();
	UINT quadColliderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT baseVertexOffset = quadColliderVertexOffset + (UINT)quadCollider.Vertices.size();
	UINT explo2VertexOffset = baseVertexOffset + (UINT)base.Vertices.size();
	UINT grassVertexOffset = explo2VertexOffset + (UINT)explo2.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	//起始索引
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT ballastIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT ScarIndexOffset = ballastIndexOffset + (UINT)ballast.Indices32.size();
	UINT ScarTurretIndexOffset = ScarIndexOffset + (UINT)Scar.Indices32.size();
	UINT bulletIndexOffset = ScarTurretIndexOffset + (UINT)ScarTurret.Indices32.size();
	UINT UIrectIndexOffset = bulletIndexOffset + (UINT)bullet.Indices32.size();
	UINT quadIndexOffset = UIrectIndexOffset + (UINT)UIrect.Indices32.size();
	UINT colliderIndexOffset = quadIndexOffset + (UINT)quad.Indices32.size();
	UINT sphereIndexOffset = colliderIndexOffset + (UINT)collider.Indices32.size();
	UINT quadColliderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT baseIndexOffset = quadColliderIndexOffset + (UINT)quadCollider.Indices32.size();
	UINT explo2IndexOffset = baseIndexOffset + (UINT)base.Indices32.size();
	UINT grassIndexOffset = explo2IndexOffset + (UINT)explo2.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;
	boxSubmesh.OBBounds = InitializeBox((UINT)box.Vertices.size(), box.Vertices);

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;
	gridSubmesh.OBBounds = InitializeBox((UINT)grid.Vertices.size(), grid.Vertices);


	SubmeshGeometry ballastSubmesh;
	ballastSubmesh.IndexCount = (UINT)ballast.Indices32.size();
	ballastSubmesh.StartIndexLocation = ballastIndexOffset;
	ballastSubmesh.BaseVertexLocation = ballastVertexOffset;
	ballastSubmesh.OBBounds = InitializeBox((UINT)ballast.Vertices.size(), ballast.Vertices);


	SubmeshGeometry ScarSubmesh;
	ScarSubmesh.IndexCount = (UINT)Scar.Indices32.size();
	ScarSubmesh.StartIndexLocation = ScarIndexOffset;
	ScarSubmesh.BaseVertexLocation = ScarVertexOffset;
	ScarSubmesh.OBBounds = InitializeBox((UINT)Scar.Vertices.size(), Scar.Vertices);

	SubmeshGeometry ScarTurretSubmesh;
	ScarTurretSubmesh.IndexCount = (UINT)ScarTurret.Indices32.size();
	ScarTurretSubmesh.StartIndexLocation = ScarTurretIndexOffset;
	ScarTurretSubmesh.BaseVertexLocation = ScarTurretVertexOffset;
	ScarTurretSubmesh.OBBounds = InitializeBox((UINT)ScarTurret.Vertices.size(), ScarTurret.Vertices);

	

	SubmeshGeometry bulletSubmesh;
	bulletSubmesh.IndexCount = (UINT)bullet.Indices32.size();
	bulletSubmesh.StartIndexLocation = bulletIndexOffset;
	bulletSubmesh.BaseVertexLocation = bulletVertexOffset;
	bulletSubmesh.OBBounds = InitializeBox((UINT)bullet.Vertices.size(), bullet.Vertices);

	SubmeshGeometry UIrectSubmesh;
	UIrectSubmesh.IndexCount = (UINT)UIrect.Indices32.size();
	UIrectSubmesh.StartIndexLocation = UIrectIndexOffset;
	UIrectSubmesh.BaseVertexLocation = UIrectVertexOffset;
	UIrectSubmesh.OBBounds = InitializeBox((UINT)UIrect.Vertices.size(), UIrect.Vertices);

	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;
	quadSubmesh.OBBounds = InitializeBox((UINT)quad.Vertices.size(), quad.Vertices);
	
	SubmeshGeometry colliderSubmesh;
	colliderSubmesh.IndexCount = (UINT)collider.Indices32.size();
	colliderSubmesh.StartIndexLocation = colliderIndexOffset;
	colliderSubmesh.BaseVertexLocation = colliderVertexOffset;
	colliderSubmesh.OBBounds = InitializeBox((UINT)collider.Vertices.size(), collider.Vertices);

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry quadColliderSubmesh;
	quadColliderSubmesh.IndexCount = (UINT)quadCollider.Indices32.size();
	quadColliderSubmesh.StartIndexLocation = quadColliderIndexOffset;
	quadColliderSubmesh.BaseVertexLocation = quadColliderVertexOffset;
	quadColliderSubmesh.OBBounds = InitializeBox((UINT)quadCollider.Vertices.size(), quadCollider.Vertices);

	SubmeshGeometry baseSubmesh;
	baseSubmesh.IndexCount = (UINT)base.Indices32.size();
	baseSubmesh.StartIndexLocation = baseIndexOffset;
	baseSubmesh.BaseVertexLocation = baseVertexOffset;
	baseSubmesh.OBBounds = InitializeBox((UINT)base.Vertices.size(), base.Vertices);

	SubmeshGeometry explo2Submesh;
	explo2Submesh.IndexCount = (UINT)explo2.Indices32.size();
	explo2Submesh.StartIndexLocation = explo2IndexOffset;
	explo2Submesh.BaseVertexLocation = explo2VertexOffset;
	explo2Submesh.OBBounds = InitializeBox((UINT)explo2.Vertices.size(), explo2.Vertices);

	SubmeshGeometry grassSubmesh;
	grassSubmesh.IndexCount = (UINT)grass.Indices32.size();
	grassSubmesh.StartIndexLocation = grassIndexOffset;
	grassSubmesh.BaseVertexLocation = grassVertexOffset;


	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		ballast.Vertices.size() +
		Scar.Vertices.size() +
		ScarTurret.Vertices.size() +
		bullet.Vertices.size() +
		UIrect.Vertices.size() +
		quad.Vertices.size() +
		collider.Vertices.size() +
		sphere.Vertices.size() +
		quadCollider.Vertices.size() +
		base.Vertices.size() +
		explo2.Vertices.size() +
		grass.Vertices.size();
	auto totalIndices32Count =
		box.Indices32.size() +
		grid.Indices32.size() +
		ballast.Indices32.size() +
		Scar.Indices32.size() +
		ScarTurret.Indices32.size() +
		bullet.Indices32.size() +
		UIrect.Indices32.size() +
		quad.Indices32.size() +
		collider.Indices32.size() +
		sphere.Indices32.size() +
		quadCollider.Indices32.size() +
		base.Indices32.size() +
		explo2.Indices32.size() +
		grass.Indices32.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for(size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < ballast.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = ballast.Vertices[i].Position;
		vertices[k].Normal = ballast.Vertices[i].Normal;
		vertices[k].TexC = ballast.Vertices[i].TexC;
		vertices[k].TangentU = ballast.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < Scar.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = Scar.Vertices[i].Position;
		vertices[k].Normal = Scar.Vertices[i].Normal;
		vertices[k].TexC = Scar.Vertices[i].TexC;
		vertices[k].TangentU = Scar.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < ScarTurret.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = ScarTurret.Vertices[i].Position;
		vertices[k].Normal = ScarTurret.Vertices[i].Normal;
		vertices[k].TexC = ScarTurret.Vertices[i].TexC;
		vertices[k].TangentU = ScarTurret.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < bullet.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = bullet.Vertices[i].Position;
		vertices[k].Normal = bullet.Vertices[i].Normal;
		vertices[k].TexC = bullet.Vertices[i].TexC;
		vertices[k].TangentU = bullet.Vertices[i].TangentU;
	}

	for(size_t i = 0; i < UIrect.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = UIrect.Vertices[i].Position;
		vertices[k].Normal = UIrect.Vertices[i].Normal;
		vertices[k].TexC = UIrect.Vertices[i].TexC;
		vertices[k].TangentU = UIrect.Vertices[i].TangentU;
	}
	for (int i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quad.Vertices[i].Position;
		vertices[k].Normal = quad.Vertices[i].Normal;
		vertices[k].TexC = quad.Vertices[i].TexC;
		vertices[k].TangentU = quad.Vertices[i].TangentU;
	}
	for (int i = 0; i < collider.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = collider.Vertices[i].Position;
		vertices[k].Normal = collider.Vertices[i].Normal;
		vertices[k].TexC = collider.Vertices[i].TexC;
		vertices[k].TangentU = collider.Vertices[i].TangentU;
	}
	for (int i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos.x = -sphere.Vertices[i].Position.x;
		vertices[k].Pos.y = sphere.Vertices[i].Position.y;
		vertices[k].Pos.z = sphere.Vertices[i].Position.z;
		vertices[k].Normal.x = sphere.Vertices[i].Normal.x;
		vertices[k].Normal.y = sphere.Vertices[i].Normal.y;
		vertices[k].Normal.z = sphere.Vertices[i].Normal.z;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}
	for (int i = 0; i < quadCollider.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quadCollider.Vertices[i].Position;
		vertices[k].Normal = quadCollider.Vertices[i].Normal;
		vertices[k].TexC = quadCollider.Vertices[i].TexC;
		vertices[k].TangentU = quadCollider.Vertices[i].TangentU;
	}
	for (int i = 0; i < base.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = base.Vertices[i].Position;
		vertices[k].Normal = base.Vertices[i].Normal;
		vertices[k].TexC = base.Vertices[i].TexC;
		vertices[k].TangentU = base.Vertices[i].TangentU;
	}
	for (int i = 0; i < explo2.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = explo2.Vertices[i].Position;
		vertices[k].Normal = explo2.Vertices[i].Normal;
		vertices[k].TexC = explo2.Vertices[i].TexC;
		vertices[k].TangentU = explo2.Vertices[i].TangentU;
	}

	for (int i = 0; i < grass.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grass.Vertices[i].Position;
		vertices[k].Normal = grass.Vertices[i].Normal;
		vertices[k].TexC = grass.Vertices[i].TexC;
		vertices[k].TangentU = grass.Vertices[i].TangentU;
	}

	std::vector<std::uint32_t> indices(totalIndices32Count);

	k = 0;
	for (size_t i = 0; i < box.Indices32.size(); ++i, ++k)
	{
		indices[k] = box.Indices32[i];
	}

	for (size_t i = 0; i < grid.Indices32.size(); ++i, ++k)
	{
		indices[k] = grid.Indices32[i];
	}
	for (size_t i = 0; i < ballast.Indices32.size(); ++i, ++k)
	{
		indices[k] = ballast.Indices32[i];
	}

	for (size_t i = 0; i < Scar.Indices32.size(); ++i, ++k)
	{
		indices[k] = Scar.Indices32[i];
	}

	for (size_t i = 0; i < ScarTurret.Indices32.size(); ++i, ++k)
	{
		indices[k] = ScarTurret.Indices32[i];
	}

	for (size_t i = 0; i < bullet.Indices32.size(); ++i, ++k)
	{
		indices[k] = bullet.Indices32[i];
	}

	for (size_t i = 0; i < UIrect.Indices32.size(); ++i, ++k)
	{
		indices[k] = UIrect.Indices32[i];
	}
	for (int i = 0; i < quad.Indices32.size(); ++i, ++k)
	{
		indices[k] = quad.Indices32[i];
	}
	for (int i = 0; i < collider.Indices32.size(); ++i, ++k)
	{
		indices[k] = collider.Indices32[i];
	}
	for (int i = 0; i < sphere.Indices32.size(); ++i, ++k)
	{
		indices[k] = sphere.Indices32[i];
	}
	for (int i = 0; i < quadCollider.Indices32.size(); ++i, ++k)
	{
		indices[k] = quadCollider.Indices32[i];
	}
	for (int i = 0; i < base.Indices32.size(); ++i, ++k)
	{
		indices[k] = base.Indices32[i];
	}
	for (int i = 0; i < explo2.Indices32.size(); ++i, ++k)
	{
		indices[k] = explo2.Indices32[i];
	}
	for (int i = 0; i < grass.Indices32.size(); ++i, ++k)
	{
		indices[k] = grass.Indices32[i];
	}

	/*
	std::vector<std::uint32_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices32()), std::end(box.GetIndices32()));
	indices.insert(indices.end(), std::begin(grid.GetIndices32()), std::end(grid.GetIndices32()));
	indices.insert(indices.end(), std::begin(ballast.GetIndices32()), std::end(ballast.GetIndices32()));
	indices.insert(indices.end(), std::begin(Scar.GetIndices32()), std::end(Scar.GetIndices32()));
	indices.insert(indices.end(), std::begin(UIrect.GetIndices32()), std::end(UIrect.GetIndices32()));
	indices.insert(indices.end(), std::begin(quad.GetIndices32()), std::end(quad.GetIndices32()));
	*/
 

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["ballast"] = ballastSubmesh;
	geo->DrawArgs["Scar"] = ScarSubmesh;
	geo->DrawArgs["ScarTurret"] = ScarTurretSubmesh;
	geo->DrawArgs["bullet"] = bulletSubmesh;
	geo->DrawArgs["UIrect"] = UIrectSubmesh;
	geo->DrawArgs["quad"] = quadSubmesh;
	geo->DrawArgs["collider"] = colliderSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["quadCollider"] = quadColliderSubmesh;
	geo->DrawArgs["base"] = baseSubmesh;
	geo->DrawArgs["explo2"] = explo2Submesh;
	geo->DrawArgs["grass"] = grassSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void TexColumnsApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;


	ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	basePsoDesc.pRootSignature = mRootSignature.Get();
	basePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	basePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);


	basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	basePsoDesc.SampleMask = UINT_MAX;
	basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	basePsoDesc.NumRenderTargets = 1;
	basePsoDesc.RTVFormats[0] = mBackBufferFormat;
	basePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	basePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	basePsoDesc.DSVFormat = mDepthStencilFormat;



	//
	// PSO for opaque objects.
	//

	//D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
	//opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	//opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&basePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = basePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = basePsoDesc;
	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	//alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	alphaTestedPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));


	 //PSO for debug layer.	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
	debugPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	debugPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	debugPsoDesc.pRootSignature = mRootSignature.Get();
	debugPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()),
		mShaders["debugVS"]->GetBufferSize()
	};
	debugPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()),
		mShaders["debugPS"]->GetBufferSize()
	};
	//debugPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	//debugPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));
	//md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"]));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = basePsoDesc;

	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	//skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	//skyPsoDesc.pRootSignature = mRootSignature.Get();
	/*skyPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};*/
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	skyPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alpha_1PsoDesc = basePsoDesc;

	alpha_1PsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	/*alpha_1PsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["alpha_1GS"]->GetBufferPointer()),
		mShaders["alpha_1GS"]->GetBufferSize()
	};*/
	alpha_1PsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["alpha_1PS"]->GetBufferPointer()),
		mShaders["alpha_1PS"]->GetBufferSize()
	};
	alpha_1PsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;

	alpha_1PsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	//alpha_1PsoDesc.BlendState.RenderTarget[1] = transparencyBlendDesc;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alpha_1PsoDesc, IID_PPV_ARGS(&mPSOs["alpha_1"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = basePsoDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));

	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC HPSpritePsoDesc = basePsoDesc;
	HPSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["HPSpriteVS"]->GetBufferPointer()),
		mShaders["HPSpriteVS"]->GetBufferSize()
	};
	HPSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["HPSpriteGS"]->GetBufferPointer()),
		mShaders["HPSpriteGS"]->GetBufferSize()
	};
	HPSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["HPSpritePS"]->GetBufferPointer()),
		mShaders["HPSpritePS"]->GetBufferSize()
	};
	HPSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	HPSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	HPSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&HPSpritePsoDesc, IID_PPV_ARGS(&mPSOs["HPSprites"])));

}

void TexColumnsApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void TexColumnsApp::BuildMaterials()
{
	int MatCount = 0;
	auto ScarMat = std::make_unique<Material>();
	ScarMat->Name = "ScarMat";
	ScarMat->MatCBIndex = ++MatCount;
	ScarMat->DiffuseSrvHeapIndex = 0;
	ScarMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	ScarMat->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	ScarMat->Roughness = 0.9f;

	auto ScarEnemyMat = std::make_unique<Material>();
	ScarEnemyMat->Name = "ScarEnemyMat";
	ScarEnemyMat->MatCBIndex = ++MatCount;
	ScarEnemyMat->DiffuseSrvHeapIndex = 1;
	ScarEnemyMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	ScarEnemyMat->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	ScarEnemyMat->Roughness = 0.9f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = ++MatCount;
	stone0->DiffuseSrvHeapIndex = 2;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    stone0->Roughness = 0.3f;
 
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = ++MatCount;
	tile0->DiffuseSrvHeapIndex = 3;
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    tile0->Roughness = 0.3f;

	auto tagDianzu0 = std::make_unique<Material>();
	tagDianzu0->Name = "dianzu0";
	tagDianzu0->MatCBIndex = ++MatCount;
	tagDianzu0->DiffuseSrvHeapIndex = 4;
	tagDianzu0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tagDianzu0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tagDianzu0->Roughness = 0.3f;

	auto UI0 = std::make_unique<Material>();
	UI0->Name = "UI";
	UI0->MatCBIndex = ++MatCount;
	UI0->DiffuseSrvHeapIndex = 5;
	UI0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	UI0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	UI0->Roughness = 0.3f;

	auto ballast0 = std::make_unique<Material>();
	ballast0->Name = "ballast";
	ballast0->MatCBIndex = ++MatCount;
	ballast0->DiffuseSrvHeapIndex = 6;
	ballast0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	ballast0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	ballast0->Roughness = 0.8f;

	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = ++MatCount;
	sky->DiffuseSrvHeapIndex = 7;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	sky->Roughness = 0.0f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = ++MatCount;
	treeSprites->DiffuseSrvHeapIndex = 8;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;

	auto ground = std::make_unique<Material>();
	ground->Name = "ground";
	ground->MatCBIndex = ++MatCount;
	ground->DiffuseSrvHeapIndex = 9;
	ground->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	ground->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	ground->Roughness = 0.0f;

	auto baseMat = std::make_unique<Material>();
	baseMat->Name = "baseMat";
	baseMat->MatCBIndex = ++MatCount;
	baseMat->DiffuseSrvHeapIndex = 10;
	baseMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	baseMat->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	baseMat->Roughness = 0.0f;

	auto baseMatEnemy = std::make_unique<Material>();
	baseMatEnemy->Name = "baseMatEnemy";
	baseMatEnemy->MatCBIndex = ++MatCount;
	baseMatEnemy->DiffuseSrvHeapIndex = 11;
	baseMatEnemy->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	baseMatEnemy->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	baseMatEnemy->Roughness = 0.0f;

	auto explo2 = std::make_unique<Material>();
	explo2->Name = "explo2";
	explo2->MatCBIndex = ++MatCount;
	explo2->DiffuseSrvHeapIndex = 12;
	explo2->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	explo2->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	explo2->Roughness = 0.0f;

	auto BlueWin = std::make_unique<Material>();
	BlueWin->Name = "BlueWin";
	BlueWin->MatCBIndex = ++MatCount;
	BlueWin->DiffuseSrvHeapIndex = 13;
	BlueWin->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	BlueWin->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	BlueWin->Roughness = 0.0f;

	auto RedWin = std::make_unique<Material>();
	RedWin->Name = "RedWin";
	RedWin->MatCBIndex = ++MatCount;
	RedWin->DiffuseSrvHeapIndex = 14;
	RedWin->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	RedWin->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	RedWin->Roughness = 0.0f;

	auto aim = std::make_unique<Material>();
	aim->Name = "aim";
	aim->MatCBIndex = ++MatCount;
	aim->DiffuseSrvHeapIndex = 15;
	aim->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	aim->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	aim->Roughness = 0.0f;

	auto CarDir = std::make_unique<Material>();
	CarDir->Name = "CarDir";
	CarDir->MatCBIndex = ++MatCount;
	CarDir->DiffuseSrvHeapIndex = 16;
	CarDir->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	CarDir->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	CarDir->Roughness = 0.0f;

	auto HPstripB = std::make_unique<Material>();
	HPstripB->Name = "HPstripB";
	HPstripB->MatCBIndex = ++MatCount;
	HPstripB->DiffuseSrvHeapIndex = 17;
	HPstripB->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	HPstripB->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	HPstripB->Roughness = 0.0f;

	auto HPstripR = std::make_unique<Material>();
	HPstripR->Name = "HPstripR";
	HPstripR->MatCBIndex = ++MatCount;
	HPstripR->DiffuseSrvHeapIndex = 18;
	HPstripR->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	HPstripR->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	HPstripR->Roughness = 0.0f;

	auto BlueBaseUITex = std::make_unique<Material>();
	BlueBaseUITex->Name = "BlueBaseUITex";
	BlueBaseUITex->MatCBIndex = ++MatCount;
	BlueBaseUITex->DiffuseSrvHeapIndex = 19;
	BlueBaseUITex->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	BlueBaseUITex->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	BlueBaseUITex->Roughness = 0.0f;

	auto RedBaseUITex = std::make_unique<Material>();
	RedBaseUITex->Name = "RedBaseUITex";
	RedBaseUITex->MatCBIndex = ++MatCount;
	RedBaseUITex->DiffuseSrvHeapIndex = 20;
	RedBaseUITex->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	RedBaseUITex->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	RedBaseUITex->Roughness = 0.0f;


	auto grayBK = std::make_unique<Material>();
	grayBK->Name = "grayBK";
	grayBK->MatCBIndex = ++MatCount;
	grayBK->DiffuseSrvHeapIndex = 21;
	grayBK->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grayBK->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	grayBK->Roughness = 0.0f;

	auto buttons = std::make_unique<Material>();
	buttons->Name = "buttons";
	buttons->MatCBIndex = ++MatCount;
	buttons->DiffuseSrvHeapIndex = 22;
	buttons->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	buttons->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	buttons->Roughness = 0.0f;

	auto gameHelper = std::make_unique<Material>();
	gameHelper->Name = "gameHelper";
	gameHelper->MatCBIndex = ++MatCount;
	gameHelper->DiffuseSrvHeapIndex = 23;
	gameHelper->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	gameHelper->FresnelR0 = XMFLOAT3(0.0f, 0.0f, 0.0f);
	gameHelper->Roughness = 0.0f;



	mMaterials["ScarMat"] = std::move(ScarMat);
	mMaterials["ScarEnemyMat"] = std::move(ScarEnemyMat);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["dianzu0"] = std::move(tagDianzu0);
	mMaterials["UI0"] = std::move(UI0);
	mMaterials["ballast0"] = std::move(ballast0);
	mMaterials["sky"] = std::move(sky);
	mMaterials["treeSprites"] = std::move(treeSprites);
	mMaterials["ground"] = std::move(ground);
	mMaterials["baseMat"] = std::move(baseMat);
	mMaterials["baseMatEnemy"] = std::move(baseMatEnemy);
	mMaterials["explo2"] = std::move(explo2);
	mMaterials["BlueWin"] = std::move(BlueWin);
	mMaterials["RedWin"] = std::move(RedWin);
	mMaterials["aim"] = std::move(aim);
	mMaterials["CarDir"] = std::move(CarDir);
	mMaterials["HPstripB"] = std::move(HPstripB);
	mMaterials["HPstripR"] = std::move(HPstripR);
	mMaterials["BlueBaseUITex"] = std::move(BlueBaseUITex);
	mMaterials["RedBaseUITex"] = std::move(RedBaseUITex);
	mMaterials["grayBK"] = std::move(grayBK);
	mMaterials["buttons"] = std::move(buttons);
	mMaterials["gameHelper"] = std::move(gameHelper);

}

void TexColumnsApp::BuildRenderItems()
{
	auto boxRitem = std::make_unique<RenderItem>();//样例盒
//XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixTranslation(0.0f, 0.0f, 0.0f)* XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = itemCount++;
	boxRitem->Mat = mMaterials["explo2"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->invisible = true;
	boxRitem->Bounds = boxRitem->Geo->DrawArgs["explo2"].OBBounds;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["explo2"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["explo2"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["explo2"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));


	auto groundRitem = std::make_unique<RenderItem>();//地板
	XMStoreFloat4x4(&groundRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&groundRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	groundRitem->ObjCBIndex = itemCount++;
	groundRitem->Mat = mMaterials["ground"].get();
	groundRitem->Geo = mGeometries["shapeGeo"].get();
	groundRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	groundRitem->Bounds = groundRitem->Geo->DrawArgs["quad"].OBBounds;
	groundRitem->IndexCount = groundRitem->Geo->DrawArgs["quad"].IndexCount;
	groundRitem->StartIndexLocation = groundRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	groundRitem->BaseVertexLocation = groundRitem->Geo->DrawArgs["quad"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(groundRitem.get());
	mAllRitems.push_back(std::move(groundRitem));

	addGrass(0.0f, -3.6f, 0.0f, 5.0f, 3);

	auto gridRitem = std::make_unique<RenderItem>();//场景
	XMStoreFloat4x4(&gridRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gridRitem->ObjCBIndex = itemCount++;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->Bounds = gridRitem->Geo->DrawArgs["grid"].OBBounds;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));


	auto gridleftRitem = std::make_unique<RenderItem>();//场景反转
	gridleftRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridleftRitem->World,
		XMMatrixScaling(-1.0f, 1.0f, -1.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&gridleftRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gridleftRitem->ObjCBIndex = itemCount++;
	gridleftRitem->Mat = mMaterials["tile0"].get();
	gridleftRitem->Geo = mGeometries["shapeGeo"].get();
	gridleftRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridleftRitem->Bounds = gridleftRitem->Geo->DrawArgs["grid"].OBBounds;
	gridleftRitem->IndexCount = gridleftRitem->Geo->DrawArgs["grid"].IndexCount;
	gridleftRitem->StartIndexLocation = gridleftRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridleftRitem->BaseVertexLocation = gridleftRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridleftRitem.get());
	mAllRitems.push_back(std::move(gridleftRitem));

	auto ballastRitem = std::make_unique<RenderItem>();//配重块
	XMStoreFloat4x4(&ballastRitem->World,
		XMMatrixScaling(0.3f, 0.3f, 0.3f)* XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&ballastRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	ballastRitem->ObjCBIndex = itemCount++;
	ballastRitem->Mat = mMaterials["ballast0"].get();
	ballastRitem->Geo = mGeometries["shapeGeo"].get();
	ballastRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//ballastRitem->invisible = true;
	ballastRitem->Bounds = ballastRitem->Geo->DrawArgs["ballast"].OBBounds;
	ballastRitem->IndexCount = ballastRitem->Geo->DrawArgs["ballast"].IndexCount;
	ballastRitem->StartIndexLocation = ballastRitem->Geo->DrawArgs["ballast"].StartIndexLocation;
	ballastRitem->BaseVertexLocation = ballastRitem->Geo->DrawArgs["ballast"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(ballastRitem.get());
	mAllRitems.push_back(std::move(ballastRitem));

	AddScar(-50.0f, -1.0f, -20.0f, XM_PI*0.0f, 0, 1);

	AddScar(-45.0f, -1.0f, -20.0f, XM_PI*0.25f, 1, 1);
	
	AddScar(-45.0f, -1.0f, -25.0f, XM_PI*0.5f , 2, 1);
	
	AddScar(50.0f, -1.0f, 20.0f, XM_PI*1.0f, 3, 2);
	
	AddScar(45.0f, -1.0f, 20.0f, XM_PI*1.25f, 4, 2);
	
	AddScar(45.0f, -1.0f, 25.0f, XM_PI*1.5f, 5, 2);

	AddBase(-53.5f, -2.2f, -27.5f, 0.0f, 1);

	AddBase(53.5f, -2.2f, 27.5f, XM_PI*1.0f, 2);

	addUI();


	auto groundColliderRitem = std::make_unique<RenderItem>();//地板碰撞
	XMStoreFloat4x4(&groundColliderRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&groundColliderRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	groundColliderRitem->ObjCBIndex = itemCount++;
	groundColliderRitem->Mat = mMaterials["tile0"].get();
	groundColliderRitem->Geo = mGeometries["shapeGeo"].get();
	groundColliderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	groundColliderRitem->Collider = true;
	groundColliderRitem->invisible = true;
	groundColliderRitem->Bounds = groundColliderRitem->Geo->DrawArgs["quadCollider"].OBBounds;
	groundColliderRitem->IndexCount = groundColliderRitem->Geo->DrawArgs["quadCollider"].IndexCount;
	groundColliderRitem->StartIndexLocation = groundColliderRitem->Geo->DrawArgs["quadCollider"].StartIndexLocation;
	groundColliderRitem->BaseVertexLocation = groundColliderRitem->Geo->DrawArgs["quadCollider"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(groundColliderRitem.get());
	mAllRitems.push_back(std::move(groundColliderRitem));

	auto colliderRitem = std::make_unique<RenderItem>();//场景碰撞
	XMStoreFloat4x4(&colliderRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&colliderRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	colliderRitem->ObjCBIndex = itemCount++;
	colliderRitem->Mat = mMaterials["tile0"].get();
	colliderRitem->Geo = mGeometries["shapeGeo"].get();
	colliderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	colliderRitem->invisible = true;
	colliderRitem->Collider = true;
	colliderRitem->Bounds = colliderRitem->Geo->DrawArgs["collider"].OBBounds;
	colliderRitem->IndexCount = colliderRitem->Geo->DrawArgs["collider"].IndexCount;
	colliderRitem->StartIndexLocation = colliderRitem->Geo->DrawArgs["collider"].StartIndexLocation;
	colliderRitem->BaseVertexLocation = colliderRitem->Geo->DrawArgs["collider"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(colliderRitem.get());
	mAllRitems.push_back(std::move(colliderRitem));

	
	auto colliderleftRitem = std::make_unique<RenderItem>();//场景碰撞反转
	colliderleftRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&colliderleftRitem->World,
		XMMatrixScaling(-1.0f, 1.0f, -1.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&colliderleftRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	colliderleftRitem->ObjCBIndex = itemCount++;
	colliderleftRitem->Mat = mMaterials["tile0"].get();
	colliderleftRitem->Geo = mGeometries["shapeGeo"].get();
	colliderleftRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	colliderleftRitem->invisible = true;
	colliderleftRitem->Collider = true;
	colliderleftRitem->Bounds = colliderleftRitem->Geo->DrawArgs["collider"].OBBounds;
	colliderleftRitem->IndexCount = colliderleftRitem->Geo->DrawArgs["collider"].IndexCount;
	colliderleftRitem->StartIndexLocation = colliderleftRitem->Geo->DrawArgs["collider"].StartIndexLocation;
	colliderleftRitem->BaseVertexLocation = colliderleftRitem->Geo->DrawArgs["collider"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(colliderleftRitem.get());
	mAllRitems.push_back(std::move(colliderleftRitem));

	auto box2Ritem = std::make_unique<RenderItem>();//样例盒
//XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	XMStoreFloat4x4(&box2Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	XMStoreFloat4x4(&box2Ritem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	box2Ritem->ObjCBIndex = itemCount++;
	box2Ritem->Mat = mMaterials["BlueWin"].get();
	box2Ritem->Geo = mGeometries["shapeGeo"].get();
	box2Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box2Ritem->invisible = true;
	box2Ritem->Bounds = box2Ritem->Geo->DrawArgs["UIrect"].OBBounds;
	box2Ritem->IndexCount = box2Ritem->Geo->DrawArgs["UIrect"].IndexCount;
	box2Ritem->StartIndexLocation = box2Ritem->Geo->DrawArgs["UIrect"].StartIndexLocation;
	box2Ritem->BaseVertexLocation = box2Ritem->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(box2Ritem.get());
	mAllRitems.push_back(std::move(box2Ritem));


	//天空盒
	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixRotationY(3.14f)*XMMatrixScaling(500.0f, 500.0f, 500.0f));
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = itemCount++;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));


	//AI队伍初始化


	for (int i = 0; i < itemCount; i++)
	{
		mAllRitems[i]->WorldBounds = UpdateBoundBox(XMLoadFloat4x4(&mAllRitems[i]->World), mAllRitems[i]->Bounds);
		mAllRitems[i]->BaseWorld = mAllRitems[i]->World;
		mAllRitems[i]->currentHP = mAllRitems[i]->maxHP;
		if (mAllRitems[i]->chara == 9)
		{
			for (int j = 0; j < 6; j++)
				if (mAllRitems[i]->team != GameAIs[j].team)
					GameAIs[j].EnemyBasePos = mAllRitems[i]->WorldBounds.Center;
		}
	}
}
bool TexColumnsApp::PlayBGM(LPWSTR FileName)
{
	if (PlayingBGM)
	{
		g_pDSBuffer8->Stop();
		PlayingBGM = false;
	}
	LPDIRECTSOUND8 g_pDsd = NULL;
	HRESULT hr;
	if (FAILED(hr = DirectSoundCreate8(NULL, &g_pDsd, NULL)))
		return false;
	if (FAILED(hr = g_pDsd->SetCooperativeLevel(mhMainWnd, DSSCL_PRIORITY)))		return false;
	CWaveFile *g_pWaveFile;
	g_pWaveFile = new CWaveFile;
	g_pWaveFile->Open(FileName, NULL, WAVEFILE_READ);
	DSBUFFERDESC dsc;
	ZeroMemory(&dsc, sizeof(DSBUFFERDESC));
	dsc.dwSize = sizeof(DSBUFFERDESC);
	dsc.dwFlags = 0;
	dsc.dwBufferBytes = g_pWaveFile->GetSize();
	dsc.lpwfxFormat = g_pWaveFile->GetFormat();
	LPDIRECTSOUNDBUFFER lpBuffer;
	if (FAILED(hr = g_pDsd->CreateSoundBuffer(&dsc, &lpBuffer, NULL)))
		return false;
	if (FAILED(hr = lpBuffer->QueryInterface(IID_IDirectSoundBuffer8, (LPVOID*)&g_pDSBuffer8)))
		return false;
	lpBuffer->Release();
	LPVOID lpLockBuffer;
	DWORD lpLen, dwRead;
	g_pDSBuffer8->Lock(0, 0, &lpLockBuffer, &lpLen, NULL, NULL, DSBLOCK_ENTIREBUFFER);
	g_pWaveFile->Read((BYTE*)lpLockBuffer, lpLen, &dwRead);
	g_pDSBuffer8->Unlock(lpLockBuffer, lpLen, NULL, 0);
	g_pDSBuffer8->SetCurrentPosition(0);
	g_pDSBuffer8->SetVolume(DSBVOLUME_MAX);
	g_pDSBuffer8->Play(0, 0, DSBPLAY_LOOPING);
	PlayingBGM = true;
	return true;
}

bool TexColumnsApp::Play3DWAV(XMFLOAT3 SPos, LPWSTR FileName)
{
	return true;
	/*DSBUFFERDESC dsc;
	LPDIRECTSOUND8 g_pDsd = NULL;
	HRESULT hr;
	LPDIRECTSOUND3DLISTENER8 g_p3DSListener = NULL;
	if (FAILED(hr = DirectSoundCreate8(NULL, &g_pDsd, NULL)))
		return false;
	if (FAILED(hr = g_pDsd->SetCooperativeLevel(mhMainWnd, DSSCL_PRIORITY)))
		return false;
	ZeroMemory(&dsc, sizeof(DSBUFFERDESC));
	dsc.dwSize = sizeof(DSBUFFERDESC);
	dsc.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_PRIMARYBUFFER;
	LPDIRECTSOUNDBUFFER lpBuffer=NULL;

	if (FAILED(hr = g_pDsd->CreateSoundBuffer(&dsc, &lpBuffer, NULL)))
		return false;
	if (FAILED(hr = lpBuffer->QueryInterface(IID_IDirectSound3DListener8, (LPVOID*)&g_p3DSListener)))
		return false;

	CWaveFile *g_pWaveFile;
	g_pWaveFile = new CWaveFile;
	g_pWaveFile->Open(FileName, NULL, WAVEFILE_READ);

	DSBUFFERDESC dsbd;
	LPDIRECTSOUND3DBUFFER8 g_p3DSBuffer;
	DS3DLISTENER g_dsListenerParams;

	ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
	dsbd.dwSize = sizeof(DSBUFFERDESC);
	dsbd.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_GLOBALFOCUS;
	dsbd.dwBufferBytes = g_pWaveFile->GetSize();
	dsbd.guid3DAlgorithm = DS3DALG_HRTF_LIGHT;
	dsbd.lpwfxFormat = g_pWaveFile->m_pwfx;
	if (FAILED(hr = g_pDsd->CreateSoundBuffer(&dsbd, &g_pDSBuffer, NULL)))
		return false;
	if (FAILED(hr = g_pDSBuffer->QueryInterface(IID_IDirectSound3DBuffer8, (VOID**)&g_p3DSBuffer)))
		return false;

	FLOAT fDopplerFactor = 0.0f;
	FLOAT fRolloffFactor = 0.0f;
	FLOAT fMinDistance = 10.0f;
	FLOAT fMaxDistance = 80.0f;
	DS3DBUFFER g_dsBufferParams;
	g_dsBufferParams.dwSize = sizeof(DS3DBUFFER);
	g_p3DSBuffer->GetAllParameters(&g_dsBufferParams);

	g_dsBufferParams.dwMode = DS3DMODE_HEADRELATIVE;
	g_dsBufferParams.vPosition = D3DVECTOR{ SPos.x,SPos.y,SPos.z };
	g_dsBufferParams.vVelocity = D3DVECTOR{ 0.0f,0.0f,0.0f };
	g_dsBufferParams.flMinDistance = fMinDistance;
	g_dsBufferParams.flMaxDistance = fMaxDistance;
	g_dsBufferParams.dwInsideConeAngle = 0.0f;
	g_dsBufferParams.dwOutsideConeAngle = XM_PI;
	g_dsBufferParams.lConeOutsideVolume = 10000;
	g_p3DSBuffer->SetAllParameters(&g_dsBufferParams, DS3D_IMMEDIATE);
	
	ZeroMemory(&g_dsListenerParams, sizeof(DS3DLISTENER));
	g_dsListenerParams.flDopplerFactor = fDopplerFactor;
	g_dsListenerParams.flRolloffFactor = fRolloffFactor;
	g_dsListenerParams.flDistanceFactor = 1.0f;
	g_dsListenerParams.vVelocity = D3DVECTOR{ 0.0f,0.0f,0.0f };
	g_dsListenerParams.vPosition = D3DVECTOR{ PlayerPos.x,PlayerPos.y,PlayerPos.z };
	g_dsListenerParams.vOrientFront = D3DVECTOR{ PlayerLook.x,PlayerLook.y,PlayerLook.z };
	g_dsListenerParams.vOrientTop = D3DVECTOR{ PlayerUP.x,PlayerUP.y,PlayerUP.z };
	g_dsListenerParams.dwSize = sizeof(DS3DLISTENER);
	g_p3DSListener->SetAllParameters(&g_dsListenerParams, DS3D_IMMEDIATE);

	DWORD res;
	LPVOID lplockbuf;
	DWORD len;
	DWORD dwWrite;
	g_pDSBuffer->Lock(0, 0, &lplockbuf, &len, NULL, NULL, DSBLOCK_ENTIREBUFFER);
	g_pWaveFile->Read((BYTE*)lplockbuf, len, &dwWrite);
	g_pDSBuffer->Unlock(lplockbuf, len, NULL, 0);
	g_pDSBuffer->SetCurrentPosition(0);
	g_pDSBuffer->SetVolume(DSBVOLUME_MAX);
	g_pDSBuffer->Play(0, 0, 0);
	return true;*/
}

bool TexColumnsApp::LoadWAV()
{
	LPWSTR FileName[4]{ {L"res/explo1.wav"} ,{L"res/GunFire.wav"},{L"res/CarCrashed.wav"},{L"res/BaseCrashed.wav"} };
	for (int i = 0; i < WAVCount; i++)
	{
		LPDIRECTSOUND8 g_pDsd = NULL;
		HRESULT hr;
		if (FAILED(hr = DirectSoundCreate8(NULL, &g_pDsd, NULL)))
			return false;
		if (FAILED(hr = g_pDsd->SetCooperativeLevel(mhMainWnd, DSSCL_PRIORITY)))			return false;
		CWaveFile *g_pWaveFile;
		g_pWaveFile = new CWaveFile;
		g_pWaveFile->Open(FileName[i], NULL, WAVEFILE_READ);
		DSBUFFERDESC dsc;
		ZeroMemory(&dsc, sizeof(DSBUFFERDESC));
		dsc.dwSize = sizeof(DSBUFFERDESC);
		dsc.dwFlags = DSBCAPS_CTRLVOLUME;
		dsc.dwBufferBytes = g_pWaveFile->GetSize();
		dsc.lpwfxFormat = g_pWaveFile->GetFormat();
		LPDIRECTSOUNDBUFFER lpBuffer;
		if (FAILED(hr = g_pDsd->CreateSoundBuffer(&dsc, &lpBuffer, NULL)))
			return false;
		if (FAILED(hr = lpBuffer->QueryInterface(IID_IDirectSoundBuffer8, (LPVOID*)&EVN_SBuffer[i])))
			return false;
		lpBuffer->Release();
		LPVOID lpLockBuffer;
		DWORD lpLen, dwRead;
		EVN_SBuffer[i]->Lock(0, 0, &lpLockBuffer, &lpLen, NULL, NULL, DSBLOCK_ENTIREBUFFER);
		g_pWaveFile->Read((BYTE*)lpLockBuffer, lpLen, &dwRead);
		EVN_SBuffer[i]->Unlock(lpLockBuffer, lpLen, NULL, 0);
	}
	return true;
}

bool TexColumnsApp::LoadWAVCar()
{
	for (int i = 0; i < 6; i++)
	{
		LPDIRECTSOUND8 g_pDsd = NULL;
		HRESULT hr;
		if (FAILED(hr = DirectSoundCreate8(NULL, &g_pDsd, NULL)))
			return false;
		if (FAILED(hr = g_pDsd->SetCooperativeLevel(mhMainWnd, DSSCL_PRIORITY)))			return false;
		CWaveFile *g_pWaveFile;
		g_pWaveFile = new CWaveFile;
		g_pWaveFile->Open(L"res/CarRun.wav", NULL, WAVEFILE_READ);
		DSBUFFERDESC dsc;
		ZeroMemory(&dsc, sizeof(DSBUFFERDESC));
		dsc.dwSize = sizeof(DSBUFFERDESC);
		dsc.dwFlags = DSBCAPS_CTRLVOLUME;
		dsc.dwBufferBytes = g_pWaveFile->GetSize();
		dsc.lpwfxFormat = g_pWaveFile->GetFormat();
		LPDIRECTSOUNDBUFFER lpBuffer;
		if (FAILED(hr = g_pDsd->CreateSoundBuffer(&dsc, &lpBuffer, NULL)))
			return false;
		if (FAILED(hr = lpBuffer->QueryInterface(IID_IDirectSoundBuffer8, (LPVOID*)&Car_SBuffer[i])))
			return false;
		lpBuffer->Release();
		LPVOID lpLockBuffer;
		DWORD lpLen, dwRead;
		Car_SBuffer[i]->Lock(0, 0, &lpLockBuffer, &lpLen, NULL, NULL, DSBLOCK_ENTIREBUFFER);
		g_pWaveFile->Read((BYTE*)lpLockBuffer, lpLen, &dwRead);
		Car_SBuffer[i]->Unlock(lpLockBuffer, lpLen, NULL, 0);
	}
	return false;
}

void TexColumnsApp::PlayEvnWav(XMFLOAT3 SPos,int id)
{
	float Slength = sqrt(pow(SPos.x - PlayerPos.x, 2) + pow(SPos.y - PlayerPos.y, 2) + pow(SPos.z - PlayerPos.z, 2));
	int VolumOffset;
	float minDist = 1.0f;
	float maxDist = 50.0f;
	if (id == 3)
		maxDist = 200.0f;
	if (Slength < minDist)
		VolumOffset = -500;
	else if (Slength > maxDist)
		return;
	else
		VolumOffset = -500 - 2500 * ((Slength - 1.0f) / (maxDist - minDist));
	EVN_SBuffer[id]->SetCurrentPosition(0);
	EVN_SBuffer[id]->SetVolume(VolumOffset);//-500~-3000
	EVN_SBuffer[id]->Play(0, 0, 0);

}

void TexColumnsApp::PlayCarWav(XMFLOAT3 SPos, int id)
{
	if (!CarRunning[id])
	{
		float Slength = sqrt(pow(SPos.x - PlayerPos.x, 2) + pow(SPos.y - PlayerPos.y, 2) + pow(SPos.z - PlayerPos.z, 2));
		int VolumOffset;
		if (Slength < 1.0f)
			VolumOffset = -500;
		else if (Slength > 50.0f)
			VolumOffset = -3000;
		else
			VolumOffset = -500 - 2500 * ((Slength - 1.0f) / 49.0f);
		Car_SBuffer[id]->SetCurrentPosition(0);
		Car_SBuffer[id]->SetVolume(VolumOffset);//-500~-3000
		Car_SBuffer[id]->Play(0, 0, DSBPLAY_LOOPING);
	}
	else if (CarRunning[id])
	{
		Car_SBuffer[id]->Stop();
	}
}

void TexColumnsApp::AddScar(float x, float y, float z, float angle, int AI, int team)
{
	GameAIs[AI].team = team;//AI队伍初始化
	GameAIs[AI].dx = angle;
	GameAIs[AI].CtrlItemId = itemCount;

	//#0
	auto ScarRitem = std::make_unique<RenderItem>();//车底盘
	XMStoreFloat4x4(&ScarRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixRotationY(angle)*XMMatrixTranslation(x, y, z));
	XMStoreFloat4x4(&ScarRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	ScarRitem->ObjCBIndex = itemCount++;
	if (team == 1)
		ScarRitem->Mat = mMaterials["ScarMat"].get();
	else if (team == 2)
		ScarRitem->Mat = mMaterials["ScarEnemyMat"].get();
	ScarRitem->Geo = mGeometries["shapeGeo"].get();
	ScarRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	ScarRitem->AIcontrol = AI;
	ScarRitem->team = team;
	ScarRitem->maxHP = 100;
	ScarRitem->Static = false;
	ScarRitem->Gravity = true;
	ScarRitem->Collider = true;
	//ScarRitem->canbePicked = true;
	ScarRitem->chara = 1;
	ScarRitem->Bounds = ScarRitem->Geo->DrawArgs["Scar"].OBBounds;
	ScarRitem->IndexCount = ScarRitem->Geo->DrawArgs["Scar"].IndexCount;
	ScarRitem->StartIndexLocation = ScarRitem->Geo->DrawArgs["Scar"].StartIndexLocation;
	ScarRitem->BaseVertexLocation = ScarRitem->Geo->DrawArgs["Scar"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(ScarRitem.get());
	mAllRitems.push_back(std::move(ScarRitem));

	//#1
	auto ScarTurretRitem = std::make_unique<RenderItem>();//炮塔
	XMStoreFloat4x4(&ScarTurretRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixRotationY(angle)* XMMatrixTranslation(x, y, z));
	XMStoreFloat4x4(&ScarTurretRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	ScarTurretRitem->ObjCBIndex = itemCount++;
	if (team == 1)
		ScarTurretRitem->Mat = mMaterials["ScarMat"].get();
	else if (team == 2)
		ScarTurretRitem->Mat = mMaterials["ScarEnemyMat"].get();
	ScarTurretRitem->Geo = mGeometries["shapeGeo"].get();
	ScarTurretRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	ScarTurretRitem->AIcontrol = AI;
	ScarTurretRitem->team = team;
	ScarTurretRitem->chara = 3;
	ScarTurretRitem->dx = angle;
	ScarTurretRitem->Static = false;
	ScarTurretRitem->Bounds = ScarTurretRitem->Geo->DrawArgs["ScarTurret"].OBBounds;
	ScarTurretRitem->IndexCount = ScarTurretRitem->Geo->DrawArgs["ScarTurret"].IndexCount;
	ScarTurretRitem->StartIndexLocation = ScarTurretRitem->Geo->DrawArgs["ScarTurret"].StartIndexLocation;
	ScarTurretRitem->BaseVertexLocation = ScarTurretRitem->Geo->DrawArgs["ScarTurret"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(ScarTurretRitem.get());
	mAllRitems.push_back(std::move(ScarTurretRitem));


	//#2
	auto bulletRitem = std::make_unique<RenderItem>();//弹头
	XMStoreFloat4x4(&bulletRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&bulletRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	bulletRitem->ObjCBIndex = itemCount++;
	bulletRitem->Mat = mMaterials["ScarMat"].get();
	bulletRitem->Geo = mGeometries["shapeGeo"].get();
	bulletRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	bulletRitem->AIcontrol = AI;
	bulletRitem->team = team;
	bulletRitem->Damage = 10;
	bulletRitem->chara = 7;
	bulletRitem->Static = false;
	//bulletRitem->Gravity = true;
	bulletRitem->Bounds = bulletRitem->Geo->DrawArgs["bullet"].OBBounds;
	bulletRitem->IndexCount = bulletRitem->Geo->DrawArgs["bullet"].IndexCount;
	bulletRitem->StartIndexLocation = bulletRitem->Geo->DrawArgs["bullet"].StartIndexLocation;
	bulletRitem->BaseVertexLocation = bulletRitem->Geo->DrawArgs["bullet"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Sky].push_back(bulletRitem.get());
	mAllRitems.push_back(std::move(bulletRitem));


	//#3
	auto treeSpritesRitem = std::make_unique<RenderItem>();	//爆炸（小）特效
	XMStoreFloat4x4(&treeSpritesRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&treeSpritesRitem->TexTransform, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	treeSpritesRitem->ObjCBIndex = itemCount++;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	treeSpritesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->chara = 51;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["explopoint"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["explopoint"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["explopoint"].BaseVertexLocation;


	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
	mAllRitems.push_back(std::move(treeSpritesRitem));


	//#4
	auto explo2Ritem = std::make_unique<RenderItem>(); 	//爆炸（大）特效
	XMStoreFloat4x4(&explo2Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&explo2Ritem->TexTransform, XMMatrixTranslation(0.0f, 0.0f, 0.0f)* XMMatrixScaling(1.0f, 1.0f, 1.0f));
	explo2Ritem->ObjCBIndex = itemCount++;
	explo2Ritem->Mat = mMaterials["explo2"].get();
	explo2Ritem->Geo = mGeometries["shapeGeo"].get();
	explo2Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	explo2Ritem->chara = 52;
	explo2Ritem->Bounds = explo2Ritem->Geo->DrawArgs["explo2"].OBBounds;
	explo2Ritem->IndexCount = explo2Ritem->Geo->DrawArgs["explo2"].IndexCount;
	explo2Ritem->StartIndexLocation = explo2Ritem->Geo->DrawArgs["explo2"].StartIndexLocation;
	explo2Ritem->BaseVertexLocation = explo2Ritem->Geo->DrawArgs["explo2"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(explo2Ritem.get());
	mAllRitems.push_back(std::move(explo2Ritem));


	//#5
	auto HPstripRitem = std::make_unique<RenderItem>();	//血条
	XMStoreFloat4x4(&HPstripRitem->World, XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&HPstripRitem->TexTransform, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	HPstripRitem->ObjCBIndex = itemCount++;
	HPstripRitem->chara = 105;
	if(team==1)
		HPstripRitem->Mat = mMaterials["HPstripB"].get();
	else if(team==2)
		HPstripRitem->Mat = mMaterials["HPstripR"].get();
	if (AI != playerID)
	{
		HPstripRitem->Geo = mGeometries["treeSpritesGeo"].get();
		HPstripRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
		HPstripRitem->IndexCount = HPstripRitem->Geo->DrawArgs["HPstrip"].IndexCount;
		HPstripRitem->StartIndexLocation = HPstripRitem->Geo->DrawArgs["HPstrip"].StartIndexLocation;
		HPstripRitem->BaseVertexLocation = HPstripRitem->Geo->DrawArgs["HPstrip"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::HPSprites].push_back(HPstripRitem.get());
	}
	else
	{
		HPstripRitem->Geo = mGeometries["shapeGeo"].get();
		HPstripRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		HPstripRitem->IndexCount = HPstripRitem->Geo->DrawArgs["UIrect"].IndexCount;
		HPstripRitem->StartIndexLocation = HPstripRitem->Geo->DrawArgs["UIrect"].StartIndexLocation;
		HPstripRitem->BaseVertexLocation = HPstripRitem->Geo->DrawArgs["UIrect"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::AlphaTested].push_back(HPstripRitem.get());
	}
	mAllRitems.push_back(std::move(HPstripRitem));



}
void TexColumnsApp::AddBase(float x, float y, float z, float angle, int team)
{
	//#0
	auto baseRitem = std::make_unique<RenderItem>();//基地
	XMStoreFloat4x4(&baseRitem->World,
		XMMatrixScaling(1.0f, 1.0f, 1.0f)*  XMMatrixRotationY(angle)*XMMatrixTranslation(x, y, z));
	XMStoreFloat4x4(&baseRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	baseRitem->ObjCBIndex = itemCount++;
	if(team==1)
		baseRitem->Mat = mMaterials["baseMat"].get();
	else if(team==2)
		baseRitem->Mat = mMaterials["baseMatEnemy"].get();
	baseRitem->Geo = mGeometries["shapeGeo"].get();
	baseRitem->chara = 9;
	baseRitem->team = team;
	baseRitem->Collider = true;
	baseRitem->maxHP = 1000;
	baseRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	baseRitem->Bounds = baseRitem->Geo->DrawArgs["base"].OBBounds;
	baseRitem->IndexCount = baseRitem->Geo->DrawArgs["base"].IndexCount;
	baseRitem->StartIndexLocation = baseRitem->Geo->DrawArgs["base"].StartIndexLocation;
	baseRitem->BaseVertexLocation = baseRitem->Geo->DrawArgs["base"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Sky].push_back(baseRitem.get());
	mAllRitems.push_back(std::move(baseRitem));


	//#1
	auto explo2Ritem = std::make_unique<RenderItem>();	//爆炸（大）特效
	XMStoreFloat4x4(&explo2Ritem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, -20.0f, 0.0f));
	XMStoreFloat4x4(&explo2Ritem->TexTransform, XMMatrixTranslation(0.0f, 0.0f, 0.0f)* XMMatrixScaling(1.0f, 1.0f, 1.0f));
	explo2Ritem->ObjCBIndex = itemCount++;
	explo2Ritem->Mat = mMaterials["explo2"].get();
	explo2Ritem->Geo = mGeometries["shapeGeo"].get();
	explo2Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	explo2Ritem->chara = 53;
	explo2Ritem->Bounds = explo2Ritem->Geo->DrawArgs["explo2"].OBBounds;
	explo2Ritem->IndexCount = explo2Ritem->Geo->DrawArgs["explo2"].IndexCount;
	explo2Ritem->StartIndexLocation = explo2Ritem->Geo->DrawArgs["explo2"].StartIndexLocation;
	explo2Ritem->BaseVertexLocation = explo2Ritem->Geo->DrawArgs["explo2"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(explo2Ritem.get());
	mAllRitems.push_back(std::move(explo2Ritem));

	//#2
	auto HPstripRitem = std::make_unique<RenderItem>();	//实时血条
	XMStoreFloat4x4(&HPstripRitem->World, XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&HPstripRitem->TexTransform, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	HPstripRitem->ObjCBIndex = itemCount++;
	HPstripRitem->chara = 106;
	if (team == 1)
		HPstripRitem->Mat = mMaterials["HPstripB"].get();
	else if (team == 2)
		HPstripRitem->Mat = mMaterials["HPstripR"].get();
	HPstripRitem->Geo = mGeometries["treeSpritesGeo"].get();
	HPstripRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	HPstripRitem->IndexCount = HPstripRitem->Geo->DrawArgs["HPstrip"].IndexCount;
	HPstripRitem->StartIndexLocation = HPstripRitem->Geo->DrawArgs["HPstrip"].StartIndexLocation;
	HPstripRitem->BaseVertexLocation = HPstripRitem->Geo->DrawArgs["HPstrip"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::HPSprites].push_back(HPstripRitem.get());
	mAllRitems.push_back(std::move(HPstripRitem));


	//#3
	auto HPstripUIRitem = std::make_unique<RenderItem>();	//UI血条
	XMStoreFloat4x4(&HPstripUIRitem->World, XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&HPstripUIRitem->TexTransform, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	HPstripUIRitem->ObjCBIndex = itemCount++;
	HPstripUIRitem->chara = 107;
	if (team == 1)
		HPstripUIRitem->Mat = mMaterials["HPstripB"].get();
	else if (team == 2)
		HPstripUIRitem->Mat = mMaterials["HPstripR"].get();
	HPstripUIRitem->Geo = mGeometries["shapeGeo"].get();
	HPstripUIRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	HPstripUIRitem->IndexCount = HPstripUIRitem->Geo->DrawArgs["UIrect"].IndexCount;
	HPstripUIRitem->StartIndexLocation = HPstripUIRitem->Geo->DrawArgs["UIrect"].StartIndexLocation;
	HPstripUIRitem->BaseVertexLocation = HPstripUIRitem->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(HPstripUIRitem.get());
	mAllRitems.push_back(std::move(HPstripUIRitem));
}
void TexColumnsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();
	//matCB
    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void TexColumnsApp::pick(int sx, int sy)
{	for (int id = 0; id < itemCount; id++)//第二个渲染项，对应boxRitem
	{
		XMFLOAT4X4 P = mCamera.GetProj4x4f();

		// Compute picking ray in view space.
		float vx = (+2.0f*sx / mClientWidth - 1.0f) / P(0, 0);
		float vy = (-2.0f*sy / mClientHeight + 1.0f) / P(1, 1);

		// Ray definition in view space.
		XMVECTOR rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		XMVECTOR rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);

		XMMATRIX V = mCamera.GetView();
		XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(V), V);//求逆矩阵

		// Assume nothing is picked to start, so the picked render-item is invisible.

		auto &ri = mAllRitems[id];
		auto geo = ri->Geo;

		// Skip invisible render-items.
		if (ri->canbePicked == false)
			continue;
		XMMATRIX W = XMLoadFloat4x4(&ri->World);
		XMMATRIX invWorld = XMMatrixInverse(&XMMatrixDeterminant(W), W);

		// Tranform ray to vi space of Mesh.
		XMMATRIX toLocal = XMMatrixMultiply(invView, invWorld);

		rayOrigin = XMVector3TransformCoord(rayOrigin, toLocal);
		rayDir = XMVector3TransformNormal(rayDir, toLocal);
		rayDir = XMVector3Normalize(rayDir);

		// Make the ray direction unit length for the intersection tests.

		// If we hit the bounding box of the Mesh, then we might have picked a Mesh triangle,
		// so do the ray/triangle tests.
		//
		// If we did not hit the bounding box, then it is impossible that we hit 
		// the Mesh, so do not waste effort doing ray/triangle tests.
		float tmin = 0.0f;
		if (ri->Bounds.Intersects(rayOrigin, rayDir, tmin))
		{
			// NOTE: For the demo, we know what to cast the vertex/index data to.  If we were mixing
			// formats, some metadata would be needed to figure out what to cast it to.
			auto vertices = (Vertex*)geo->VertexBufferCPU->GetBufferPointer() + ri->BaseVertexLocation;
			auto indices = (std::uint32_t*)geo->IndexBufferCPU->GetBufferPointer() + ri->StartIndexLocation;
			UINT triCount = ri->IndexCount / 3;

			// Find the nearest ray/triangle intersection.
			tmin = MathHelper::Infinity;
			for (UINT i = 0; i < triCount; ++i)
			{
				// Indices for this triangle.
				UINT i0 = indices[i * 3 + 0];//indices[]值错误
				UINT i1 = indices[i * 3 + 1];
				UINT i2 = indices[i * 3 + 2];

				// Vertices for this triangle.
				XMVECTOR v0 = XMLoadFloat3(&vertices[i0].Pos);
				XMVECTOR v1 = XMLoadFloat3(&vertices[i1].Pos);
				XMVECTOR v2 = XMLoadFloat3(&vertices[i2].Pos);

				// We have to iterate over all the triangles in order to find the nearest intersection.
				float t = 0.0f;
				if (TriangleTests::Intersects(rayOrigin, rayDir, v0, v1, v2, t)|| TriangleTests::Intersects(rayOrigin, rayDir, v0, v2, v1, t))
				{
					XMMATRIX world = XMLoadFloat4x4(&ri->World);
					XMStoreFloat4x4(&ri->World, world*XMMatrixTranslation(0.0f, 0.0f, 1.0f));
					ri->NumFramesDirty = gNumFrameResources; //3;
					break;
				}
			}
		}
	}
}
bool TexColumnsApp::CanBeShoot(int id1)
{
	float tr = 0.0;

	XMVECTOR rayOrigin = XMVectorSet(GameAIs[id1].SelfPos.x, GameAIs[id1].SelfPos.y, GameAIs[id1].SelfPos.z, 0.0f);
	XMVECTOR rayDir;

	if (GameAIs[id1].lockedId == 6)
		rayDir = XMVectorSet(GameAIs[id1].Car2BasePos.x, GameAIs[id1].Car2BasePos.y, GameAIs[id1].Car2BasePos.z, 0.0);
	else
		rayDir = XMVectorSet(GameAIs[id1].Car2CarPos[GameAIs[id1].lockedId].x, GameAIs[id1].Car2CarPos[GameAIs[id1].lockedId].y, GameAIs[id1].Car2CarPos[GameAIs[id1].lockedId].z, 0.0);

	rayDir = XMVector3Normalize(rayDir);



	for (int i = 0; i < itemCount; i++)
	{
		//仅与地形进行检测
		if (!mAllRitems[i]->Collider || i == GameAIs[id1].CtrlItemId)
			continue;

		auto &ri = mAllRitems[i];
		auto geo = ri->Geo;

		//定义炮弹射线

		//全局包围盒相交检测
		if (ri->WorldBounds.Intersects(rayOrigin, rayDir, tr))
		{
			if (ri->chara == 0)
			{
				//本地化炮弹射线
				XMMATRIX W = XMLoadFloat4x4(&ri->World);
				XMMATRIX toLocal = XMMatrixInverse(&XMMatrixDeterminant(W), W);
				XMVECTOR rayOriginL = XMVector3TransformCoord(rayOrigin, toLocal);
				XMVECTOR rayDirL = XMVector3TransformNormal(rayDir, toLocal);

				//获取三角面片
				auto vertices = (Vertex*)geo->VertexBufferCPU->GetBufferPointer() + ri->BaseVertexLocation;
				auto indices = (std::uint32_t*)geo->IndexBufferCPU->GetBufferPointer() + ri->StartIndexLocation;
				UINT triCount = ri->IndexCount / 3;

				//遍历三角面片
				for (UINT i = 0; i < triCount; ++i)
				{
					// Indices for this triangle.
					UINT i0 = indices[i * 3 + 0];
					UINT i1 = indices[i * 3 + 1];
					UINT i2 = indices[i * 3 + 2];

					// Vertices for this triangle.
					XMVECTOR v0 = XMLoadFloat3(&vertices[i0].Pos);
					XMVECTOR v1 = XMLoadFloat3(&vertices[i1].Pos);
					XMVECTOR v2 = XMLoadFloat3(&vertices[i2].Pos);

					// We have to iterate over all the triangles in order to find the nearest intersection.
					float t = 0.0f;
					//三角面检测
					if (TriangleTests::Intersects(rayOriginL, rayDirL, v0, v1, v2, t))
					{
						//刷新最小距离
						if (t < GameAIs[id1].tmin)
						{
							return false;
						}
					}
				}
			}
			else if (ri->chara == 1)
			{
				if (ri->team == GameAIs[id1].team)
				{
					XMFLOAT3 rayAly = { ri->WorldBounds.Center.x - GameAIs[id1].SelfPos.x,ri->WorldBounds.Center.y - GameAIs[id1].SelfPos.y ,ri->WorldBounds.Center.z - GameAIs[id1].SelfPos.z };
					float tc = sqrt(pow(rayAly.x, 2) + pow(rayAly.y, 2) + pow(rayAly.z, 2));
					if(tc<GameAIs[id1].tmin)
						return false;
				}
			}
		}
	}

	return true;
}
void TexColumnsApp::DrawUI()
{
	mCamera.UpdateViewMatrix();
	XMMATRIX V = mCamera.GetView();
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(V), V);

	float CarDirCos = 0.0f;
	if (playerID < 6)
	{
		CarDirCos -= atan(GameAIs[playerID].FrontDir.m128_f32[0] / GameAIs[playerID].FrontDir.m128_f32[2]);
		if (GameAIs[playerID].FrontDir.m128_f32[2] < 0.0f)
			CarDirCos += XM_PI;
		CarDirCos += mAllRitems[GameAIs[playerID].CtrlItemId + 1]->dx;
	}

	//暂停界面
	if (state == 3)
	{
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->chara == 110)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(2.2f, 1.3f, 0.1f)*XMMatrixTranslation(0.0f, 0.0f, 0.0f)*invView);
			}
			else if (mAllRitems[i]->chara == 115)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.04f, 0.01f, 0.05f)*XMMatrixTranslation(-0.0f, 0.005f, 0.0f)*invView);
			}
			else if (mAllRitems[i]->chara == 116)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(-0.0f, -0.0f, 0.0f)*invView);
				if (pickButton(dsx, dsy, i))
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
			}
			else if (mAllRitems[i]->chara == 117)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(-0.0f, -0.005f, 0.0f)*invView);
				if (pickButton(dsx, dsy, i))
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
			}
			mAllRitems[i]->NumFramesDirty = gNumFrameResources; //3
		}
	}
	//主菜单
	if (state == 0)
	{
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->chara == 100)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(2.2f, 1.3f, 1.0f)*XMMatrixTranslation(0.0f, 0.0f, 0.2f)*invView);
			}
			else if (mAllRitems[i]->chara == 110)
			{
			XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 111)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(-0.015f, -0.01f, 0.0f)*invView);
				if (pickButton(dsx, dsy, i))
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
			}
			else if (mAllRitems[i]->chara == 112)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(-0.0f, -0.01f, 0.0f)*invView);
				if (pickButton(dsx, dsy, i))
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
			}
			else if (mAllRitems[i]->chara == 113)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(0.015f, -0.01f, 0.0f)*invView);
				if (pickButton(dsx, dsy, i))
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
			}
			else if (mAllRitems[i]->chara == 114)
			{
				if(showHelper)
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, 0.05f, 0.0f)*XMMatrixScaling(0.03f, 0.03f, 0.05f)*invView);
				else if(!showHelper)
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 115)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 116)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 117)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 118)
			{
				if (teamChoosing)
				{
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(0.0f, 0.01f, 0.0f)*invView);
					if (pickButton(dsx, dsy, i))
						XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
				}
				else if (!teamChoosing)
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 119)
			{
				if (teamChoosing)
				{
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(-0.015f, 0.005f, 0.0f)*invView);
					if (pickButton(dsx, dsy, i))
						XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
				}
				else if (!teamChoosing)
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 120)
			{
				if (teamChoosing)
				{
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(0.015f, 0.005f, 0.0f)*invView);
					if (pickButton(dsx, dsy, i))
						XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
				}
				else if (!teamChoosing)
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 121)
			{
				if (teamChoosing)
				{
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(0.0f, 0.0f, 0.0f)*invView);
					if (pickButton(dsx, dsy, i))
						XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
				}
				else if (!teamChoosing)
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			mAllRitems[i]->NumFramesDirty = gNumFrameResources; //3
		}
	}
	//游戏内UI
	if (state == 1 || state == 2)
	{
		for (int i = 0; i < itemCount; i++)
		{
			if (mAllRitems[i]->chara == 100)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			if(mAllRitems[i]->chara == 103)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.02f, 0.02f)*XMMatrixTranslation(0.0f, 0.0f, 0.1f)*invView);
			}
			else if (mAllRitems[i]->chara == 104)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixRotationZ(CarDirCos)*XMMatrixScaling(0.1f, 0.1f, 0.1f)*XMMatrixTranslation(-0.1, -0.02f, 0.11f)*invView);
			}
			else if (mAllRitems[i]->chara == 105)
			{
				float HPOffset = 1.0f - (float)mAllRitems[i - 5]->currentHP / (float)mAllRitems[i - 5]->maxHP;
				if (HPOffset > 1.0f)
					HPOffset = 1.0f;
				if (mAllRitems[i - 5]->AIcontrol != playerID)
				{
					mAllRitems[i]->World = mAllRitems[i - 5]->World;
					mAllRitems[i]->World._42 += 1.6f;
					XMStoreFloat4x4(&mAllRitems[i]->TexTransform, XMMatrixTranslation(0.5f*HPOffset, 0.0f, 0.0f));
				}
				else if (mAllRitems[i - 5]->AIcontrol == playerID)
				{
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.1f, 0.01f, 0.1f)*XMMatrixTranslation(-0.1f, -0.06f, 0.1f)*invView);
					XMStoreFloat4x4(&mAllRitems[i]->TexTransform, XMMatrixScaling(0.5f,1.0f,1.0f)*XMMatrixTranslation(0.5f*HPOffset, 0.0f, 0.0f));
				}
			}
			else if (mAllRitems[i]->chara == 106)
			{
				float HPOffset = 1.0f - (float)mAllRitems[i - 2]->currentHP / (float)mAllRitems[i - 2]->maxHP;
				if (HPOffset > 1.0f)
					HPOffset = 1.0f;
				mAllRitems[i]->World = mAllRitems[i - 2]->World;
				mAllRitems[i]->World._42 += 3.5f;
				XMStoreFloat4x4(&mAllRitems[i]->TexTransform, XMMatrixTranslation(0.5f*HPOffset, 0.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 107)
			{
				float HPOffset = 1.0f - (float)mAllRitems[i - 3]->currentHP / (float)mAllRitems[i - 3]->maxHP;
				if (HPOffset > 1.0f)
					HPOffset = 1.0f;
				if (mAllRitems[i - 3]->team == 1)
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.15f, 0.01f, 0.1f)*XMMatrixTranslation(-0.058f, 0.07f, 0.1f)*invView);
				else if (mAllRitems[i - 3]->team == 2)
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(-0.15f, -0.01f, 0.1f)*XMMatrixTranslation(0.058f, 0.07f, 0.1f)*invView);
				XMStoreFloat4x4(&mAllRitems[i]->TexTransform, XMMatrixScaling(0.5f, 1.0f, 1.0f)*XMMatrixTranslation(0.5f*HPOffset, 0.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 108)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.05f, 0.05f, 0.05f)*XMMatrixTranslation(-0.095f, 0.05f, 0.1f)*invView);
			}
			else if (mAllRitems[i]->chara == 109)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.05f, 0.05f, 0.05f)*XMMatrixTranslation(0.095f, 0.05f, 0.1f)*invView);
			}
			else if (mAllRitems[i]->chara == 110 && state == 1)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			//结算界面
			else if (mAllRitems[i]->chara == 110 && state == 2)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(2.2f, 1.3f, 0.2f)*XMMatrixTranslation(0.0f, 0.0f, 0.0f)*invView);
			}
			else if (mAllRitems[i]->chara == 101 && winner == 1 && state == 2)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, 0.1f, 0.0f)*XMMatrixScaling(0.05f, 0.05f, 0.06f)*invView);
			}
			else if (mAllRitems[i]->chara == 102 && winner == 2 && state == 2)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, 0.1f, 0.0f)*XMMatrixScaling(0.05f, 0.05f, 0.06f)*invView);
			}
			else if (mAllRitems[i]->chara == 111)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 112)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 113)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 114)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 115)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 116)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 117 && state == 1)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 117 && state == 2)
			{
				XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(0.02f, 0.005f, 0.05f)*XMMatrixTranslation(-0.0f, -0.005f, 0.0f)*invView);
				if (pickButton(dsx, dsy, i))
					XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixScaling(1.1f, 1.1f, 1.0f)*XMLoadFloat4x4(&mAllRitems[i]->World));
			}
			else if (mAllRitems[i]->chara == 118)
			{
			XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 119)
			{
			XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 120)
			{
			XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			else if (mAllRitems[i]->chara == 121)
			{
			XMStoreFloat4x4(&mAllRitems[i]->World, XMMatrixTranslation(0.0f, -20.0f, 0.0f));
			}
			mAllRitems[i]->NumFramesDirty = gNumFrameResources; //3
		}
	}
	XMStoreFloat4x4(&mAllRitems[itemCount-1]->World, XMMatrixRotationY(3.14f)*XMMatrixScaling(500.0f, 500.0f, 500.0f)*XMMatrixTranslationFromVector(mCamera.GetPosition()));
	mAllRitems[itemCount-1]->NumFramesDirty = gNumFrameResources; //3
}
bool TexColumnsApp::pickButton(int sx, int sy,int id)
{
	XMFLOAT4X4 P = mCamera.GetProj4x4f();

	// Compute picking ray in view space.
	float vx = (+2.0f*sx / mClientWidth - 1.0f) / P(0, 0);
	float vy = (-2.0f*sy / mClientHeight + 1.0f) / P(1, 1);

	// Ray definition in view space.
	XMVECTOR rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);

	XMMATRIX V = mCamera.GetView();
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(V), V);//求逆矩阵

	// Assume nothing is picked to start, so the picked render-item is invisible.

	auto &ri = mAllRitems[id];
	auto geo = ri->Geo;

	// Skip invisible render-items.
	XMMATRIX W = XMLoadFloat4x4(&ri->World);
	XMMATRIX invWorld = XMMatrixInverse(&XMMatrixDeterminant(W), W);

	// Tranform ray to vi space of Mesh.
	XMMATRIX toLocal = XMMatrixMultiply(invView, invWorld);

	rayOrigin = XMVector3TransformCoord(rayOrigin, toLocal);
	rayDir = XMVector3TransformNormal(rayDir, toLocal);
	rayDir = XMVector3Normalize(rayDir);

	// Make the ray direction unit length for the intersection tests.

	// If we hit the bounding box of the Mesh, then we might have picked a Mesh triangle,
	// so do the ray/triangle tests.
	//
	// If we did not hit the bounding box, then it is impossible that we hit 
	// the Mesh, so do not waste effort doing ray/triangle tests.
	float tmin = 0.0f;
	if (ri->Bounds.Intersects(rayOrigin, rayDir, tmin))
	{
			return true;
	}
	return false;
}

BoundingOrientedBox TexColumnsApp::UpdateBoundBox(XMMATRIX world, BoundingOrientedBox bounds)
{
	bounds.Transform(bounds, world);
	return bounds;
}

BoundingOrientedBox TexColumnsApp::InitializeBox(UINT vcount, std::vector<GeometryGenerator::Vertex> Vertices)
{
	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	XMVECTOR vMin = XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

	for (UINT i = 0; i < vcount; ++i)
	{
		XMVECTOR P = XMLoadFloat3(&Vertices[i].Position);
		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}
	BoundingOrientedBox bounds;
	XMStoreFloat3(&bounds.Center, 0.5f*(vMin + vMax));
	XMStoreFloat3(&bounds.Extents, 0.5f*(vMax - vMin));
	return bounds;
}

void TexColumnsApp::Graph(int id1, int id2)
{
	auto &ri = mAllRitems[id2];
	auto geo = ri->Geo;
	XMFLOAT3 LM(0.0f, 0.0f, 0.0f);
	XMFLOAT3 RLM(0.0f, 0.0f, 0.0f);
	UINT faceCount = 0;
	UINT RfaceCount = 0;
	BoundingOrientedBox OBBounds = mAllRitems[id1]->WorldBounds;

	XMMATRIX toWorld = XMLoadFloat4x4(&ri->World);
	XMMATRIX toLocal = XMMatrixInverse(&XMMatrixDeterminant(toWorld), toWorld);
	OBBounds.Transform(OBBounds, toLocal);

	if (OBBounds.Contains(ri->Bounds))
	{
		if (ri->chara == 0)
		{
			auto vertices = (Vertex*)geo->VertexBufferCPU->GetBufferPointer() + ri->BaseVertexLocation;
			auto indices = (std::uint32_t*)geo->IndexBufferCPU->GetBufferPointer() + ri->StartIndexLocation;
			UINT triCount = ri->IndexCount / 3;
			// Find the nearest ray/triangle intersection.
			for (UINT i = 0; i < triCount; ++i)
			{
				// Indices for this triangle.
				UINT i0 = indices[i * 3 + 0];//indices[]值错误
				UINT i1 = indices[i * 3 + 1];
				UINT i2 = indices[i * 3 + 2];

				// Vertices for this triangle.
				XMVECTOR v0 = XMLoadFloat3(&vertices[i0].Pos);
				XMVECTOR v1 = XMLoadFloat3(&vertices[i1].Pos);
				XMVECTOR v2 = XMLoadFloat3(&vertices[i2].Pos);
				if (OBBounds.Contains(v0, v1, v2))
				{
					LM.x += vertices[i0].Normal.x;
					LM.y += vertices[i0].Normal.y;
					LM.z += vertices[i0].Normal.z;
					if (pow(vertices[i0].Normal.y, 2) > 1.0 / 3.0)
					{
						RLM.x += vertices[i0].Normal.x;
						RLM.y += vertices[i0].Normal.y;
						RLM.z += vertices[i0].Normal.z;
						RfaceCount++;
					}
					faceCount++;
				}
			}
		}
		else if(ri->chara==1)
		{
			//Y平面动量
			XMFLOAT3 CarHitNormalY;
			CarHitNormalY.x = mAllRitems[id1]->LastPos.Center.x - mAllRitems[id2]->LastPos.Center.x;
			CarHitNormalY.z = mAllRitems[id1]->LastPos.Center.z - mAllRitems[id2]->LastPos.Center.z;

			XMVECTOR CarHit = XMVectorSet(CarHitNormalY.x, 0.0f, CarHitNormalY.z, 0.0f);
			CarHit = XMVector4Normalize(CarHit);

			//Y方向动量
			XMVECTOR CarUp = XMVectorSet(0.0, 1.0, 0.0, 0.0);
			XMMATRIX toW = XMLoadFloat4x4(&mAllRitems[id1]->World);
			CarUp = XMVector3TransformNormal(CarUp, toW);
			float CarUptan = sqrt(pow(CarUp.m128_f32[0], 2) + pow(CarUp.m128_f32[2], 2)) / CarUp.m128_f32[1];

			/*if (mAllRitems[id1]->WorldBounds.Center.y + mAllRitems[id1]->GravityLM > mAllRitems[id1]->LastPos.Center.y)
				CarHit.m128_f32[1] = -CarUptan;
			else*/
				CarHit.m128_f32[1] = CarUptan;

				mAllRitems[id1]->CarLM += CarHit * 1.5f*playerspeed*cos(atan(CarUptan));
		}		
	}
	if (faceCount > 0)
	{
		/*XMMATRIX W = XMLoadFloat4x4(&mAllRitems[id1]->World);

		XMStoreFloat4x4(&mAllRitems[id1]->World,
			W*toLocal*XMMatrixTranslation(playerspeed*LM.x, playerspeed*LM.y, playerspeed*LM.z)*toWorld);
		mAllRitems[id1]->NumFramesDirty = gNumFrameResources; //3;

		mAllRitems[id1]->GravityLM = 0.0f;*/
		XMMATRIX toWorld = XMLoadFloat4x4(&mAllRitems[id2]->World);
		XMVECTOR LMDir = XMVectorSet(LM.x, LM.y, LM.z, 1.0f);
		//LMDir = LMDir / (float)faceCount;
		LMDir = XMVector3TransformNormal(LMDir, toWorld);
		mAllRitems[id1]->ColliderLM = mAllRitems[id1]->ColliderLM + LMDir;
		mAllRitems[id1]->ColliderRiCount +=faceCount;
		if (RfaceCount > 0)
		{
			XMVECTOR LMRot = XMVectorSet(RLM.x, RLM.y, RLM.z, 1.0f);
			LMRot = LMRot / (float)RfaceCount;
			LMRot = XMVector3TransformNormal(LMRot, toWorld);
			mAllRitems[id1]->RotateLM = mAllRitems[id1]->RotateLM + LMRot;
			mAllRitems[id1]->RotateFcaeCount += RfaceCount;
		}
	}

	
}
void TexColumnsApp::addUI()
{
	//菜单
	auto boxNewRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxNewRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&boxNewRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxNewRitem->ObjCBIndex = itemCount++;
	boxNewRitem->Mat = mMaterials["UI0"].get();
	boxNewRitem->Geo = mGeometries["shapeGeo"].get();
	boxNewRitem->chara = 100;
	boxNewRitem->canbePicked = true;
	boxNewRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxNewRitem->Bounds = boxNewRitem->Geo->DrawArgs["UIrect"].OBBounds;
	boxNewRitem->IndexCount = boxNewRitem->Geo->DrawArgs["UIrect"].IndexCount;
	boxNewRitem->StartIndexLocation = boxNewRitem->Geo->DrawArgs["UIrect"].StartIndexLocation;
	boxNewRitem->BaseVertexLocation = boxNewRitem->Geo->DrawArgs["UIrect"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxNewRitem.get());
	mAllRitems.push_back(std::move(boxNewRitem));

	auto aimRitem = std::make_unique<RenderItem>();//准星
	XMStoreFloat4x4(&aimRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&aimRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	aimRitem->ObjCBIndex = itemCount++;
	aimRitem->Mat = mMaterials["aim"].get();
	aimRitem->Geo = mGeometries["shapeGeo"].get();
	aimRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	aimRitem->chara = 103;
	aimRitem->Bounds = aimRitem->Geo->DrawArgs["UIrect"].OBBounds;
	aimRitem->IndexCount = aimRitem->Geo->DrawArgs["UIrect"].IndexCount;
	aimRitem->StartIndexLocation = aimRitem->Geo->DrawArgs["UIrect"].StartIndexLocation;
	aimRitem->BaseVertexLocation = aimRitem->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(aimRitem.get());
	mAllRitems.push_back(std::move(aimRitem));


	auto CarDirRitem = std::make_unique<RenderItem>();//车体方向
	XMStoreFloat4x4(&CarDirRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&CarDirRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	CarDirRitem->ObjCBIndex = itemCount++;
	CarDirRitem->Mat = mMaterials["CarDir"].get();
	CarDirRitem->Geo = mGeometries["shapeGeo"].get();
	CarDirRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	CarDirRitem->chara = 104;
	CarDirRitem->Bounds = CarDirRitem->Geo->DrawArgs["UIrect"].OBBounds;
	CarDirRitem->IndexCount = CarDirRitem->Geo->DrawArgs["UIrect"].IndexCount;
	CarDirRitem->StartIndexLocation = CarDirRitem->Geo->DrawArgs["UIrect"].StartIndexLocation;
	CarDirRitem->BaseVertexLocation = CarDirRitem->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(CarDirRitem.get());
	mAllRitems.push_back(std::move(CarDirRitem));

	auto BlueWinRitem = std::make_unique<RenderItem>();//蓝队获胜
	XMStoreFloat4x4(&BlueWinRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&BlueWinRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	BlueWinRitem->ObjCBIndex = itemCount++;
	BlueWinRitem->Mat = mMaterials["BlueWin"].get();
	BlueWinRitem->Geo = mGeometries["shapeGeo"].get();
	BlueWinRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	BlueWinRitem->chara = 101;
	BlueWinRitem->Bounds = BlueWinRitem->Geo->DrawArgs["UIrect"].OBBounds;
	BlueWinRitem->IndexCount = BlueWinRitem->Geo->DrawArgs["UIrect"].IndexCount;
	BlueWinRitem->StartIndexLocation = BlueWinRitem->Geo->DrawArgs["UIrect"].StartIndexLocation;
	BlueWinRitem->BaseVertexLocation = BlueWinRitem->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(BlueWinRitem.get());
	mAllRitems.push_back(std::move(BlueWinRitem));

	auto RedWinRitem = std::make_unique<RenderItem>();//红队获胜
	XMStoreFloat4x4(&RedWinRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&RedWinRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	RedWinRitem->ObjCBIndex = itemCount++;
	RedWinRitem->Mat = mMaterials["RedWin"].get();
	RedWinRitem->Geo = mGeometries["shapeGeo"].get();
	RedWinRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	RedWinRitem->chara = 102;
	RedWinRitem->Bounds = RedWinRitem->Geo->DrawArgs["UIrect"].OBBounds;
	RedWinRitem->IndexCount = RedWinRitem->Geo->DrawArgs["UIrect"].IndexCount;
	RedWinRitem->StartIndexLocation = RedWinRitem->Geo->DrawArgs["UIrect"].StartIndexLocation;
	RedWinRitem->BaseVertexLocation = RedWinRitem->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(RedWinRitem.get());
	mAllRitems.push_back(std::move(RedWinRitem));

	auto BlueBaseUITex = std::make_unique<RenderItem>();//蓝队基地图标
	XMStoreFloat4x4(&BlueBaseUITex->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&BlueBaseUITex->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	BlueBaseUITex->ObjCBIndex = itemCount++;
	BlueBaseUITex->Mat = mMaterials["BlueBaseUITex"].get();
	BlueBaseUITex->Geo = mGeometries["shapeGeo"].get();
	BlueBaseUITex->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	BlueBaseUITex->chara = 108;
	BlueBaseUITex->Bounds = BlueBaseUITex->Geo->DrawArgs["UIrect"].OBBounds;
	BlueBaseUITex->IndexCount = BlueBaseUITex->Geo->DrawArgs["UIrect"].IndexCount;
	BlueBaseUITex->StartIndexLocation = BlueBaseUITex->Geo->DrawArgs["UIrect"].StartIndexLocation;
	BlueBaseUITex->BaseVertexLocation = BlueBaseUITex->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(BlueBaseUITex.get());
	mAllRitems.push_back(std::move(BlueBaseUITex));

	auto RedBaseUITex = std::make_unique<RenderItem>();//红队基地图标
	XMStoreFloat4x4(&RedBaseUITex->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&RedBaseUITex->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	RedBaseUITex->ObjCBIndex = itemCount++;
	RedBaseUITex->Mat = mMaterials["RedBaseUITex"].get();
	RedBaseUITex->Geo = mGeometries["shapeGeo"].get();
	RedBaseUITex->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	RedBaseUITex->chara = 109;
	RedBaseUITex->Bounds = RedBaseUITex->Geo->DrawArgs["UIrect"].OBBounds;
	RedBaseUITex->IndexCount = RedBaseUITex->Geo->DrawArgs["UIrect"].IndexCount;
	RedBaseUITex->StartIndexLocation = RedBaseUITex->Geo->DrawArgs["UIrect"].StartIndexLocation;
	RedBaseUITex->BaseVertexLocation = RedBaseUITex->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(RedBaseUITex.get());
	mAllRitems.push_back(std::move(RedBaseUITex));

	
	auto grayBK = std::make_unique<RenderItem>();//半透明灰色背景
	XMStoreFloat4x4(&grayBK->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&grayBK->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	grayBK->ObjCBIndex = itemCount++;
	grayBK->Mat = mMaterials["grayBK"].get();
	grayBK->Geo = mGeometries["shapeGeo"].get();
	grayBK->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	grayBK->chara = 110;
	grayBK->Bounds = grayBK->Geo->DrawArgs["UIrect"].OBBounds;
	grayBK->IndexCount = grayBK->Geo->DrawArgs["UIrect"].IndexCount;
	grayBK->StartIndexLocation = grayBK->Geo->DrawArgs["UIrect"].StartIndexLocation;
	grayBK->BaseVertexLocation = grayBK->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(grayBK.get());
	mAllRitems.push_back(std::move(grayBK));


	auto startgame = std::make_unique<RenderItem>();//开始游戏
	XMStoreFloat4x4(&startgame->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&startgame->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	startgame->ObjCBIndex = itemCount++;
	startgame->Mat = mMaterials["buttons"].get();
	startgame->Geo = mGeometries["shapeGeo"].get();
	startgame->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	startgame->chara = 111;
	startgame->Bounds = startgame->Geo->DrawArgs["UIrect"].OBBounds;
	startgame->IndexCount = startgame->Geo->DrawArgs["UIrect"].IndexCount;
	startgame->StartIndexLocation = startgame->Geo->DrawArgs["UIrect"].StartIndexLocation;
	startgame->BaseVertexLocation = startgame->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(startgame.get());
	mAllRitems.push_back(std::move(startgame));

	auto gameTips = std::make_unique<RenderItem>();//帮助
	XMStoreFloat4x4(&gameTips->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&gameTips->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.0f, 0.4f, 0.0f));
	gameTips->ObjCBIndex = itemCount++;
	gameTips->Mat = mMaterials["buttons"].get();
	gameTips->Geo = mGeometries["shapeGeo"].get();
	gameTips->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	gameTips->chara = 112;
	gameTips->Bounds = gameTips->Geo->DrawArgs["UIrect"].OBBounds;
	gameTips->IndexCount = gameTips->Geo->DrawArgs["UIrect"].IndexCount;
	gameTips->StartIndexLocation = gameTips->Geo->DrawArgs["UIrect"].StartIndexLocation;
	gameTips->BaseVertexLocation = gameTips->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(gameTips.get());
	mAllRitems.push_back(std::move(gameTips));

	auto exitgame = std::make_unique<RenderItem>();//退出
	XMStoreFloat4x4(&exitgame->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&exitgame->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.0f, 0.2f, 0.0f));
	exitgame->ObjCBIndex = itemCount++;
	exitgame->Mat = mMaterials["buttons"].get();
	exitgame->Geo = mGeometries["shapeGeo"].get();
	exitgame->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	exitgame->chara = 113;
	exitgame->Bounds = exitgame->Geo->DrawArgs["UIrect"].OBBounds;
	exitgame->IndexCount = exitgame->Geo->DrawArgs["UIrect"].IndexCount;
	exitgame->StartIndexLocation = exitgame->Geo->DrawArgs["UIrect"].StartIndexLocation;
	exitgame->BaseVertexLocation = exitgame->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(exitgame.get());
	mAllRitems.push_back(std::move(exitgame));

	auto gameHelper = std::make_unique<RenderItem>();//操控说明
	XMStoreFloat4x4(&gameHelper->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&gameHelper->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	gameHelper->ObjCBIndex = itemCount++;
	gameHelper->Mat = mMaterials["stone0"].get();
	gameHelper->Geo = mGeometries["shapeGeo"].get();
	gameHelper->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	gameHelper->chara = 114;
	gameHelper->Bounds = gameHelper->Geo->DrawArgs["UIrect"].OBBounds;
	gameHelper->IndexCount = gameHelper->Geo->DrawArgs["UIrect"].IndexCount;
	gameHelper->StartIndexLocation = gameHelper->Geo->DrawArgs["UIrect"].StartIndexLocation;
	gameHelper->BaseVertexLocation = gameHelper->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(gameHelper.get());
	mAllRitems.push_back(std::move(gameHelper));


	auto pause = std::make_unique<RenderItem>();//暂停标识
	XMStoreFloat4x4(&pause->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&pause->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.5f, 0.0f, 0.0f));
	pause->ObjCBIndex = itemCount++;
	pause->Mat = mMaterials["buttons"].get();
	pause->Geo = mGeometries["shapeGeo"].get();
	pause->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	pause->chara = 115;
	pause->Bounds = pause->Geo->DrawArgs["UIrect"].OBBounds;
	pause->IndexCount = pause->Geo->DrawArgs["UIrect"].IndexCount;
	pause->StartIndexLocation = pause->Geo->DrawArgs["UIrect"].StartIndexLocation;
	pause->BaseVertexLocation = pause->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(pause.get());
	mAllRitems.push_back(std::move(pause));

	auto continueRitem = std::make_unique<RenderItem>();//继续
	XMStoreFloat4x4(&continueRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&continueRitem->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.0f, 0.6f, 0.0f));
	continueRitem->ObjCBIndex = itemCount++;
	continueRitem->Mat = mMaterials["buttons"].get();
	continueRitem->Geo = mGeometries["shapeGeo"].get();
	continueRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	continueRitem->chara = 116;
	continueRitem->Bounds = continueRitem->Geo->DrawArgs["UIrect"].OBBounds;
	continueRitem->IndexCount = continueRitem->Geo->DrawArgs["UIrect"].IndexCount;
	continueRitem->StartIndexLocation = continueRitem->Geo->DrawArgs["UIrect"].StartIndexLocation;
	continueRitem->BaseVertexLocation = continueRitem->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(continueRitem.get());
	mAllRitems.push_back(std::move(continueRitem));

	auto backToMain = std::make_unique<RenderItem>();//返回主菜单
	XMStoreFloat4x4(&backToMain->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&backToMain->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.0f, 0.8f, 0.0f));
	backToMain->ObjCBIndex = itemCount++;
	backToMain->Mat = mMaterials["buttons"].get();
	backToMain->Geo = mGeometries["shapeGeo"].get();
	backToMain->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	backToMain->chara = 117;
	backToMain->Bounds = backToMain->Geo->DrawArgs["UIrect"].OBBounds;
	backToMain->IndexCount = backToMain->Geo->DrawArgs["UIrect"].IndexCount;
	backToMain->StartIndexLocation = backToMain->Geo->DrawArgs["UIrect"].StartIndexLocation;
	backToMain->BaseVertexLocation = backToMain->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(backToMain.get());
	mAllRitems.push_back(std::move(backToMain));

	auto teamchoose = std::make_unique<RenderItem>();//选择阵营
	XMStoreFloat4x4(&teamchoose->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&teamchoose->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.5f, 0.2f, 0.0f));
	teamchoose->ObjCBIndex = itemCount++;
	teamchoose->Mat = mMaterials["buttons"].get();
	teamchoose->Geo = mGeometries["shapeGeo"].get();
	teamchoose->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	teamchoose->chara = 118;
	teamchoose->Bounds = teamchoose->Geo->DrawArgs["UIrect"].OBBounds;
	teamchoose->IndexCount = teamchoose->Geo->DrawArgs["UIrect"].IndexCount;
	teamchoose->StartIndexLocation = teamchoose->Geo->DrawArgs["UIrect"].StartIndexLocation;
	teamchoose->BaseVertexLocation = teamchoose->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(teamchoose.get());
	mAllRitems.push_back(std::move(teamchoose));

	auto chooseRed = std::make_unique<RenderItem>();//选择红方
	XMStoreFloat4x4(&chooseRed->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&chooseRed->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.5f, 0.6f, 0.0f));
	chooseRed->ObjCBIndex = itemCount++;
	chooseRed->Mat = mMaterials["buttons"].get();
	chooseRed->Geo = mGeometries["shapeGeo"].get();
	chooseRed->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	chooseRed->chara = 119;
	chooseRed->Bounds = chooseRed->Geo->DrawArgs["UIrect"].OBBounds;
	chooseRed->IndexCount = chooseRed->Geo->DrawArgs["UIrect"].IndexCount;
	chooseRed->StartIndexLocation = chooseRed->Geo->DrawArgs["UIrect"].StartIndexLocation;
	chooseRed->BaseVertexLocation = chooseRed->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(chooseRed.get());
	mAllRitems.push_back(std::move(chooseRed));

	auto chooseBlue = std::make_unique<RenderItem>();//选择蓝方
	XMStoreFloat4x4(&chooseBlue->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&chooseBlue->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.5f, 0.4f, 0.0f));
	chooseBlue->ObjCBIndex = itemCount++;
	chooseBlue->Mat = mMaterials["buttons"].get();
	chooseBlue->Geo = mGeometries["shapeGeo"].get();
	chooseBlue->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	chooseBlue->chara = 120;
	chooseBlue->Bounds = chooseBlue->Geo->DrawArgs["UIrect"].OBBounds;
	chooseBlue->IndexCount = chooseBlue->Geo->DrawArgs["UIrect"].IndexCount;
	chooseBlue->StartIndexLocation = chooseBlue->Geo->DrawArgs["UIrect"].StartIndexLocation;
	chooseBlue->BaseVertexLocation = chooseBlue->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(chooseBlue.get());
	mAllRitems.push_back(std::move(chooseBlue));

	auto chooseWatching = std::make_unique<RenderItem>();//选择观战
	XMStoreFloat4x4(&chooseWatching->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixTranslation(0.0f, -10.0f, 0.0f));
	XMStoreFloat4x4(&chooseWatching->TexTransform, XMMatrixScaling(0.5f, 0.2f, 1.0f)*XMMatrixTranslation(0.5f, 0.8f, 0.0f));
	chooseWatching->ObjCBIndex = itemCount++;
	chooseWatching->Mat = mMaterials["buttons"].get();
	chooseWatching->Geo = mGeometries["shapeGeo"].get();
	chooseWatching->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->invisible = true;
	chooseWatching->chara = 121;
	chooseWatching->Bounds = chooseWatching->Geo->DrawArgs["UIrect"].OBBounds;
	chooseWatching->IndexCount = chooseWatching->Geo->DrawArgs["UIrect"].IndexCount;
	chooseWatching->StartIndexLocation = chooseWatching->Geo->DrawArgs["UIrect"].StartIndexLocation;
	chooseWatching->BaseVertexLocation = chooseWatching->Geo->DrawArgs["UIrect"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::alpha_1].push_back(chooseWatching.get());
	mAllRitems.push_back(std::move(chooseWatching));


}
void TexColumnsApp::addGrass(float x, float y, float z, float l, int n)
{
	for (int i = 0; i < n; i++)
	{
		int il = l * 10;
		float randx = rand() % (2 * il + 1) - il;
		randx = randx / 10.0f;
		float randy = rand() % (2 * il + 1) - il;
		randy = randy / 10.f;
		float randa = rand() % 314;
		randa = randa / 100.0f;
		auto grassRitem = std::make_unique<RenderItem>();//草皮
		XMStoreFloat4x4(&grassRitem->World,
			 XMMatrixTranslation(x + randx, y, z + randy));
		XMStoreFloat4x4(&grassRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		grassRitem->ObjCBIndex = itemCount++;
		grassRitem->Mat = mMaterials["ground"].get();
		grassRitem->Geo = mGeometries["shapeGeo"].get();
		grassRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		grassRitem->Bounds = grassRitem->Geo->DrawArgs["grass"].OBBounds;
		grassRitem->IndexCount = grassRitem->Geo->DrawArgs["grass"].IndexCount;
		grassRitem->StartIndexLocation = grassRitem->Geo->DrawArgs["grass"].StartIndexLocation;
		grassRitem->BaseVertexLocation = grassRitem->Geo->DrawArgs["grass"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::alpha_1].push_back(grassRitem.get());
		mAllRitems.push_back(std::move(grassRitem));
	}
}
float TexColumnsApp::GraphBullet(int id)
{

	float tmin = MathHelper::Infinity;
	float tr = 0.0;
	for (int i = 0; i < itemCount; i++)
	{
		//仅与自身所属车体以外的参与碰撞的渲染项进行检测
		if ((!mAllRitems[i]->Collider) || i == id - 2)
			continue;

		auto &ri = mAllRitems[i];
		auto geo = ri->Geo;

		//定义炮弹射线
		XMFLOAT3 BC = mAllRitems[id]->WorldBounds.Center;
		XMVECTOR rayOrigin = XMVectorSet(BC.x, BC.y, BC.z, 0.0f);
		XMVECTOR rayDir = XMVectorSet(0.0, 0.0, 1.0, 0.0);
		XMMATRIX toWorld = XMLoadFloat4x4(&mAllRitems[id]->World);
		rayDir = XMVector3TransformNormal(rayDir, toWorld);

		//全局包围盒相交检测
		if (ri->WorldBounds.Intersects(rayOrigin, rayDir, tr))
		{
			//若为地形
			if (ri->chara == 0)
			{
				//本地化炮弹射线
				XMMATRIX W = XMLoadFloat4x4(&ri->World);
				XMMATRIX toLocal = XMMatrixInverse(&XMMatrixDeterminant(W), W);
				rayOrigin = XMVector3TransformCoord(rayOrigin, toLocal);
				rayDir = XMVector3TransformNormal(rayDir, toLocal);

				//获取三角面片
				auto vertices = (Vertex*)geo->VertexBufferCPU->GetBufferPointer() + ri->BaseVertexLocation;
				auto indices = (std::uint32_t*)geo->IndexBufferCPU->GetBufferPointer() + ri->StartIndexLocation;
				UINT triCount = ri->IndexCount / 3;

				//遍历三角面片
				for (UINT i = 0; i < triCount; ++i)
				{
					// Indices for this triangle.
					UINT i0 = indices[i * 3 + 0];
					UINT i1 = indices[i * 3 + 1];
					UINT i2 = indices[i * 3 + 2];

					// Vertices for this triangle.
					XMVECTOR v0 = XMLoadFloat3(&vertices[i0].Pos);
					XMVECTOR v1 = XMLoadFloat3(&vertices[i1].Pos);
					XMVECTOR v2 = XMLoadFloat3(&vertices[i2].Pos);

					// We have to iterate over all the triangles in order to find the nearest intersection.
					float t = 0.0f;
					//三角面检测
					if (TriangleTests::Intersects(rayOrigin, rayDir, v0, v1, v2, t))
					{
						//刷新最小距离
						if (t < tmin)
						{
							mAllRitems[id]->hitedRitemID = -1;
							tmin = t;
						}
					}
				}
			}

			//若为车体
			else if (ri->chara == 1)
			{
				//刷新最小距离
				if (tr < tmin)
				{
					mAllRitems[id]->hitedRitemID = i;
					tmin = tr;
				}
			}
			else if (ri->chara == 9)
			{
				//刷新最小距离
				if (tr < tmin)
				{
					mAllRitems[id]->hitedRitemID = i;
					tmin = tr;
				}
			}
		}
	}
	return tmin;
}
bool TexColumnsApp::cmpFloat4(XMFLOAT4 a, XMFLOAT4 b,float angle)
{
	if (abs(a.x - b.x)> angle)
		return false;
	if (abs(a.y - b.y) > angle)
		return false;
	if (abs(a.z - b.z) > angle)
		return false;
	if (abs(a.w - b.w) > angle)
		return false;
	return true;
}

float TexColumnsApp::GetHillsHeight(float x, float z)const
{
	return 0.3f*(z*sinf(0.1f*x) + x * cosf(0.1f*z));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TexColumnsApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}

