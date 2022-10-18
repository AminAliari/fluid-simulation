#define MAX_PLANE 9U
#define THREAD_COUNT 256
#define DESCRIPTOR_SET_COUNT 3
#define MAX_PARTICLE_COUNT 100'000
#define COMPUTE_DESCRIPTOR_SET_COUNT 7
#define SPH_PARTITION_BUCKET_COUNT 1048576U

// Interfaces
#define USE_POSITIVE_PLANE_SIGN
#include "../../TheForge/Utilities/Interfaces/ILog.h"
#include "../../TheForge/Utilities/Interfaces/ITime.h"
#include "../../TheForge/Game/Interfaces/IScripting.h"
#include "../../TheForge/Application/Interfaces/IUI.h"
#include "../../TheForge/Application/Interfaces/IApp.h"
#include "../../TheForge/Application/Interfaces/IFont.h"
#include "../../TheForge/Application/Interfaces/IInput.h"
#include "../../TheForge/Application/Interfaces/IProfiler.h"
#include "../../TheForge/Utilities/Interfaces/IFileSystem.h"
#include "../../TheForge/Application/Interfaces/ICameraController.h"

// Renderer
#include "../../TheForge/Graphics/Interfaces/IGraphics.h"
#include "../../TheForge/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

// Math
#include "../../TheForge/Utilities/Math/MathTypes.h"
#include "../../TheForge/Utilities/Interfaces/IMemory.h"

/// Structs
struct ParticleData
{
    vec4 pos;
    vec4 vel;
    vec4 force; 
};

struct PlanesData
{
    mat4 toLocal[MAX_PLANE];
    mat4 toWorld[MAX_PLANE];
    vec4 pos[MAX_PLANE];
    vec4 normal[MAX_PLANE];
    vec4 color[MAX_PLANE];
};

struct PlaneObject
{
    vec3  mPos;
    vec3  mRot;
    vec3  mScale;

    vec4  mColor;
    vec3  mRotSpeed; // Rotation speed around self
};

struct RootConstants
{
    vec4 args;
};

struct FrameData
{
    mat4        vp;
    float3      lightDir            = float3(80.0f, -30.0f, 0.0f);
    uint32_t    _pad0;
    
    float3      gravity             = float3(0.0f, -9.8f, 0.0f);
    uint32_t    _pad1;
    
    float3      forcePos            = float3(1000.0f, 1000.0f, 1000.0f);
    uint32_t    _pad2;
    
    float       forceRange          = 10.0f;
    float       forcePower          = 20;

    float       dt                  = 0.03333334f; // 30 fps

    uint32_t    maxCount            = 100'000;
    float       mass                = 1.0f;
    float       drag                = 0.97f;
    float       particleSize        = 1.0f;

    float       SPH_h               = 1.0f;
    float       SPH_h_rcp;
    float       SPH_h2;
    float       SPH_h3;
    float       SPH_K               = 100.0f;
    float       SPH_e               = 0.1f;
    float       SPH_p0              = 2.0f;
    float       SPH_visc_constant;
    float       SPH_poly6_constant;
    float       SPH_spiky_constant;
    uint32_t    _pad3[3];
};

/// Globals
uint32_t       gFrameIndex = 0;
const uint32_t gImageCount = 3;
RootConstants  gRootConstants = {};
uint32_t       gRootConstantIndex = 0;
uint32_t       gComputeRootConstantIndex = 0;

// Vertex and index buffers
Buffer*   pPlaneVB = NULL;
Buffer*   pPlaneIB = NULL;
Buffer*   pSphereVB = NULL;

// Graphics shaders and pipelines
Shader*   pSphereShader = NULL;
Pipeline* pSpherePipeline = NULL;
Shader*   pPlaneShader = NULL;
Pipeline* pPlanePipeline = NULL;
Shader*   pPlaneInstanceShader = NULL;
Pipeline* pPlaneInstancePipline = NULL;

// Compute shaders and pipelines
Shader*   pPartitionShader = NULL;
Pipeline* pPartitionPipeline = NULL;
Shader*   pResetOffsetShader = NULL;
Pipeline* pResetOffsetPipeline = NULL;
Shader*   pPartitionOffsetShader = NULL;
Pipeline* pPartitionOffsetPipeline = NULL;
Shader*   pDensityShader = NULL;
Pipeline* pDensityPipeline = NULL;
Shader*   pForceShader = NULL;
Pipeline* pForcePipeline = NULL;
Shader*   pSimulateShader = NULL;
Pipeline* pSimulatePipeline = NULL;
Shader*   pSortShader = NULL;
Pipeline* pSortPipeline = NULL;
Shader*   pSortStepShader = NULL;
Pipeline* pSortStepPipeline = NULL;
Shader*   pSortInnerShader = NULL;
Pipeline* pSortInnerPipeline = NULL;

// Pipeline resources
RootSignature* pRootSignature = NULL;
RootSignature* pComputeRootSignature = NULL;
DescriptorSet* pFrameDS = { NULL };
DescriptorSet* pComputeFrameDS = { NULL };

// CPU data
FrameData   gFrameData;
PlanesData  gPlanesData;
PlaneObject gPlanes[MAX_PLANE];

// Uniform buffers
Buffer* pFrameUB[gImageCount] = { NULL };
Buffer* pPlanesUB[gImageCount] = { NULL };

// RWBuffers
Buffer* pIndexBuffer = NULL;
Buffer* pDensityBuffer = NULL;
Buffer* pParticleBuffer = NULL;
Buffer* pCellIndexBuffer = NULL;
Buffer* pCellOffsetBuffer = NULL;
  
// Rendering
Renderer*     pRenderer = NULL;
SwapChain*    pSwapChain = NULL;
RenderTarget* pDepthBuffer = NULL;
Queue*        pGraphicsQueue = NULL;
Cmd*          pCmds[gImageCount] = { NULL };
CmdPool*      pCmdPools[gImageCount] = { NULL };
Semaphore*    pImageAcquiredSemaphore = NULL;
Fence*        pRenderCompleteFences[gImageCount] = { NULL };
Semaphore*    pRenderCompleteSemaphores[gImageCount] = { NULL };

// Forge Misc
uint32_t           gFontID = 0; 
FontDrawDesc       gFrameTimeDraw; 
UIComponent*       pGuiWindow = NULL;
ICameraController* pCameraController = NULL;
ProfileToken       gGpuProfileToken = PROFILE_INVALID_TOKEN;

// Meshes data
int gNumberOfSpherePoints;
float* pSpherePoints = 0;
const int gSphereResolution = 5; // Increase for higher resolution spheres
const float gSphereDiameter = 0.5f;
float4 particleColor = float4(1.0f, 0.0f, 0.0f, 1.0f);

const float gPlanePoints[] =
{
    // pos (float3), normal (float3)
     1.0f, 0.0f,  1.0f,  0.0f, 1.0f, 0.0f,
     1.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f,
    -1.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f,
    -1.0f, 0.0f,  1.0f,  0.0f, 1.0f, 0.0f,
};
const uint16_t gPlaneIndices[] = { 0,1,2, 0,2,3 };
const uint32_t gPlaneIndexCount = sizeof(gPlaneIndices) / sizeof(uint16_t);

// Simulation controls
float2 mousePos = float2(0.0f, 0.0f);
float spawnRadius = 10.5f;
float3 spawnCenter = float3(-22.0f, 47.0f, 0.0f);

bool isPaused = false;
bool canProcessFrame = false;
void setProcessFrame() 
{
    canProcessFrame = true;
}

bool resetSimulation = false;
void setResetSimulation(void* pUserData)
{
    resetSimulation = true;
}

// Fluid Simluation App
class FluidSim: public IApp
{
public:
    bool Init()
    {
        // File system
        {
            fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_FONTS, "Fonts");
            fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_GPU_CONFIG, "GPUCfg");
            fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_SOURCES, "Shaders");
            fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_BINARIES, "CompiledShaders");
        }

        // Rendering
        {
            // window and renderer setup
            RendererDesc settings;
            memset(&settings, 0, sizeof(settings));
            settings.mGLESSupported = false;
            settings.mD3D11Supported = false;
            initRenderer(GetName(), &settings, &pRenderer);
            // Check for init success
            if (!pRenderer)
                return false;

            QueueDesc queueDesc = {};
            queueDesc.mType = QUEUE_TYPE_GRAPHICS;
            queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
            addQueue(pRenderer, &queueDesc, &pGraphicsQueue);

            for (uint32_t i = 0; i < gImageCount; ++i)
            {
                CmdPoolDesc cmdPoolDesc = {};
                cmdPoolDesc.pQueue = pGraphicsQueue;
                addCmdPool(pRenderer, &cmdPoolDesc, &pCmdPools[i]);
                CmdDesc cmdDesc = {};
                cmdDesc.pPool = pCmdPools[i];
                addCmd(pRenderer, &cmdDesc, &pCmds[i]);

                addFence(pRenderer, &pRenderCompleteFences[i]);
                addSemaphore(pRenderer, &pRenderCompleteSemaphores[i]);
            }
            addSemaphore(pRenderer, &pImageAcquiredSemaphore);

            initResourceLoaderInterface(pRenderer);
        }

        // Sphere buffer
        {
            // Generate sphere vertex buffer
            generateSpherePoints(&pSpherePoints, &gNumberOfSpherePoints, gSphereResolution, gSphereDiameter);

            uint64_t sphereDataSize = gNumberOfSpherePoints * sizeof(float);
            BufferLoadDesc sphereVbDesc = {};
            sphereVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
            sphereVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
            sphereVbDesc.mDesc.mSize = sphereDataSize;
            sphereVbDesc.pData = pSpherePoints;
            sphereVbDesc.ppBuffer = &pSphereVB;
            addResource(&sphereVbDesc, NULL);

            // free memory associated with sphere points.
            tf_free(pSpherePoints);
        }

        // Plane buffers
        {
            BufferLoadDesc planeVbDesc = {};
            planeVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
            planeVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
            planeVbDesc.mDesc.mSize = sizeof(gPlanePoints);
            planeVbDesc.pData = gPlanePoints;
            planeVbDesc.ppBuffer = &pPlaneVB;
            addResource(&planeVbDesc, NULL);
            
            BufferLoadDesc planeIbDesc = {};
            planeIbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_INDEX_BUFFER;
            planeIbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
            planeIbDesc.mDesc.mSize = sizeof(gPlaneIndices);
            planeIbDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_OWN_MEMORY_BIT;
            planeIbDesc.pData = gPlaneIndices;
            planeIbDesc.ppBuffer = &pPlaneIB;
            addResource(&planeIbDesc, NULL);
        }

        // Uniform buffers
        {
            BufferLoadDesc ubDesc = {};
            ubDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ubDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
            ubDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
            ubDesc.pData = NULL;
            for (uint32_t i = 0; i < gImageCount; ++i)
            {
                ubDesc.mDesc.mSize = sizeof(FrameData);
                ubDesc.ppBuffer = &pFrameUB[i];
                addResource(&ubDesc, NULL);
                
                ubDesc.mDesc.mSize = sizeof(PlanesData);
                ubDesc.ppBuffer = &pPlanesUB[i];
                addResource(&ubDesc, NULL);
            }
        }

        // Particle data and buffers
        {
            // Populate init data
            uint32_t maxCount = MAX_PARTICLE_COUNT;
            uint32_t* indexData = generateParticleIndexData();
            ParticleData* particleData = generateParticleData();

            // particleBuffer
            SyncToken particleToken = {};
            BufferLoadDesc bufferDesc = {};
            bufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
            bufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
            bufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;
            bufferDesc.mDesc.mFirstElement = 0;
            bufferDesc.mDesc.mElementCount = maxCount;
            bufferDesc.mDesc.mStructStride = sizeof(ParticleData);
            bufferDesc.mDesc.mSize = maxCount * bufferDesc.mDesc.mStructStride;
            bufferDesc.pData = particleData;
            bufferDesc.ppBuffer = &pParticleBuffer;
            addResource(&bufferDesc, &particleToken);

            // indexbuffer
            SyncToken indexToken = {};
            bufferDesc.mDesc.mElementCount = maxCount;
            bufferDesc.mDesc.mStructStride = sizeof(uint32_t);
            bufferDesc.mDesc.mSize = maxCount * bufferDesc.mDesc.mStructStride;
            bufferDesc.pData = indexData;
            bufferDesc.ppBuffer = &pIndexBuffer;
            addResource(&bufferDesc, &indexToken);

            // cellOffsetBuffer
            bufferDesc.mDesc.mElementCount = SPH_PARTITION_BUCKET_COUNT;
            bufferDesc.mDesc.mStructStride = sizeof(uint);
            bufferDesc.mDesc.mSize = SPH_PARTITION_BUCKET_COUNT * bufferDesc.mDesc.mStructStride;
            bufferDesc.pData = NULL;
            bufferDesc.ppBuffer = &pCellOffsetBuffer;
            addResource(&bufferDesc, NULL);

            // cellIndexBuffer
            bufferDesc.mDesc.mElementCount = maxCount;
            bufferDesc.mDesc.mStructStride = sizeof(float);
            bufferDesc.mDesc.mSize = maxCount * bufferDesc.mDesc.mStructStride;
            bufferDesc.pData = NULL;
            bufferDesc.ppBuffer = &pCellIndexBuffer;
            addResource(&bufferDesc, NULL);

            // densityBuffer
            bufferDesc.mDesc.mElementCount = maxCount;
            bufferDesc.mDesc.mStructStride = sizeof(float);
            bufferDesc.mDesc.mSize = maxCount * bufferDesc.mDesc.mStructStride;
            bufferDesc.pData = NULL;
            bufferDesc.ppBuffer = &pDensityBuffer;
            addResource(&bufferDesc, NULL);

            waitForToken(&indexToken);
            waitForToken(&particleToken);

            tf_free(indexData);
            tf_free(particleData);
        }

        // Forge UI and Profiler
        {
            FontDesc font = {};
            font.pFontPath = "TitilliumText/TitilliumText-Bold.otf";
            fntDefineFonts(&font, 1, &gFontID);

            FontSystemDesc fontRenderDesc = {};
            fontRenderDesc.pRenderer = pRenderer;
            if (!initFontSystem(&fontRenderDesc))
                return false; // report?

            // Initialize Forge User Interface Rendering
            UserInterfaceDesc uiRenderDesc = {};
            uiRenderDesc.pRenderer = pRenderer;
            initUserInterface(&uiRenderDesc);

            // Initialize micro profiler and its UI.
            ProfilerDesc profiler = {};
            profiler.pRenderer = pRenderer;
            profiler.mWidthUI = mSettings.mWidth;
            profiler.mHeightUI = mSettings.mHeight;
            initProfiler(&profiler);

            // Gpu profiler can only be added after initProfile.
            gGpuProfileToken = addGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
        }

        // App UI
        {
            UIComponentDesc guiDesc = {};
            guiDesc.mStartPosition = vec2(mSettings.mWidth * 0.8f, mSettings.mHeight * 0.01f);
            uiCreateComponent(GetName(), &guiDesc, &pGuiWindow);

            ButtonWidget resetUI;
            UIWidget* pResetUI = uiCreateComponentWidget(pGuiWindow, "Reset", &resetUI, WIDGET_TYPE_BUTTON);
            uiSetWidgetOnEditedCallback(pResetUI, nullptr, setResetSimulation);

            CheckboxWidget pauseUI;
            pauseUI.pData = &isPaused;
            UIWidget* pPauseUI = uiCreateComponentWidget(pGuiWindow, "Pause", &pauseUI, WIDGET_TYPE_CHECKBOX);

            SliderUintWidget maxCountUI;
            maxCountUI.pData = &gFrameData.maxCount;
            maxCountUI.mMin = 1;
            maxCountUI.mMax = 100'000;
            UIWidget* pMaxCountUI = uiCreateComponentWidget(pGuiWindow, "Particle Count", &maxCountUI, WIDGET_TYPE_SLIDER_UINT);
            uiSetWidgetOnEditedCallback(pMaxCountUI, nullptr, setResetSimulation);

            SliderFloatWidget dtUI;
            dtUI.pData = &gFrameData.dt;
            dtUI.mMin = 0.0f;
            dtUI.mMax = 1.0f;
            UIWidget* pUpdateStepUI = uiCreateComponentWidget(pGuiWindow, "Update Step", &dtUI, WIDGET_TYPE_SLIDER_FLOAT);

            SliderFloatWidget spawnRadiusUI;
            spawnRadiusUI.pData = &spawnRadius;
            spawnRadiusUI.mMin = 0.0f;
            spawnRadiusUI.mMax = 100.0f;
            UIWidget* pSpawnRadiusUI = uiCreateComponentWidget(pGuiWindow, "Spawn Radius", &spawnRadiusUI, WIDGET_TYPE_SLIDER_FLOAT);

            SliderFloat3Widget spawnCenterUI;
            spawnCenterUI.pData = &spawnCenter;
            spawnCenterUI.mMin = float3(-100.0f);
            spawnCenterUI.mMax = float3(100.0f);
            UIWidget* pSpawnCenterUI = uiCreateComponentWidget(pGuiWindow, "Spawn Center", &spawnCenterUI, WIDGET_TYPE_SLIDER_FLOAT3);

            SliderFloat3Widget gravityUI;
            gravityUI.pData = &gFrameData.gravity;
            gravityUI.mMin = float3(-100.0f);
            gravityUI.mMax = float3(100.0f);
            UIWidget* pGravityUI = uiCreateComponentWidget(pGuiWindow, "Gravity", &gravityUI, WIDGET_TYPE_SLIDER_FLOAT3);
            
            SliderFloatWidget forcePowerUI;
            forcePowerUI.pData = &gFrameData.forcePower;
            forcePowerUI.mMin = 0.0f;
            forcePowerUI.mMax = 100.0f;
            UIWidget* pForcePowerUI = uiCreateComponentWidget(pGuiWindow, "Force Power", &forcePowerUI, WIDGET_TYPE_SLIDER_FLOAT);
          
            SliderFloatWidget forceRange;
            forceRange.pData = &gFrameData.forceRange;
            forceRange.mMin = 0.0f;
            forceRange.mMax = 100.0f;
            UIWidget* pForceRange = uiCreateComponentWidget(pGuiWindow, "Force Range", &forceRange, WIDGET_TYPE_SLIDER_FLOAT);

            SliderFloatWidget massUI;
            massUI.pData = &gFrameData.mass;
            massUI.mMin = 0.0f;
            massUI.mMax = 10.0f;
            UIWidget* pMassUI = uiCreateComponentWidget(pGuiWindow, "Mass", &massUI, WIDGET_TYPE_SLIDER_FLOAT);

            SliderFloatWidget dragUI;
            dragUI.pData = &gFrameData.drag;
            dragUI.mMin = 0.0f;
            dragUI.mMax = 1.0f;
            UIWidget* pDragUI = uiCreateComponentWidget(pGuiWindow, "Drag", &dragUI, WIDGET_TYPE_SLIDER_FLOAT);

            SliderFloatWidget hUI;
            hUI.pData = &gFrameData.SPH_h;
            hUI.mMin = 0.0f;
            hUI.mMax = 10.0f;
            UIWidget* pHUI = uiCreateComponentWidget(pGuiWindow, "H", &hUI, WIDGET_TYPE_SLIDER_FLOAT);

            SliderFloatWidget kUI;
            kUI.pData = &gFrameData.SPH_K;
            kUI.mMin = 0.0f;
            kUI.mMax = 500.0f;
            UIWidget* pKUI = uiCreateComponentWidget(pGuiWindow, "K", &kUI, WIDGET_TYPE_SLIDER_FLOAT);

            SliderFloatWidget eUI;
            eUI.pData = &gFrameData.SPH_e;
            eUI.mMin = 0.0f;
            eUI.mMax = 10.0f;
            UIWidget* pEUI = uiCreateComponentWidget(pGuiWindow, "e", &eUI, WIDGET_TYPE_SLIDER_FLOAT);

            SliderFloatWidget p0UI;
            p0UI.pData = &gFrameData.SPH_p0;
            p0UI.mMin = 0.0f;
            p0UI.mMax = 500.0f;
            UIWidget* pP0UI = uiCreateComponentWidget(pGuiWindow, "p0", &p0UI, WIDGET_TYPE_SLIDER_FLOAT);
            
            SliderFloatWidget particleSizeUI;
            particleSizeUI.pData = &gFrameData.particleSize;
            particleSizeUI.mMin = 0.0f;
            particleSizeUI.mMax = 10.0f;
            UIWidget* pParticleSizeUI = uiCreateComponentWidget(pGuiWindow, "Particle Size", &particleSizeUI, WIDGET_TYPE_SLIDER_FLOAT);

            ColorSliderWidget particleColorUI;
            particleColorUI.pData = &particleColor;
            UIWidget* pParticleColorUI = uiCreateComponentWidget(pGuiWindow, "Particle Color", &particleColorUI, WIDGET_TYPE_COLOR_SLIDER);

            SliderFloat3Widget lightDirUI;
            lightDirUI.pData = &gFrameData.lightDir;
            lightDirUI.mMin = float3(-360.0f);
            lightDirUI.mMax = float3(360.0f);
            UIWidget* pLightDirUI = uiCreateComponentWidget(pGuiWindow, "Light Direction", &lightDirUI, WIDGET_TYPE_SLIDER_FLOAT3);
        }

        waitForAllResourceLoads();

        // Set initial plane data
        {
            const float kDegreeToRadian = PI / 180.f;
            
            // Floor plane
            gPlanes[0].mPos = vec3(0.2f, -7.6f, 0.0f);
            gPlanes[0].mRot = kDegreeToRadian * vec3(0.0f, 0.0f, -3.0f);
            gPlanes[0].mScale = vec3(50, 50, 50);
            gPlanes[0].mColor = vec4(1.0f);
            gPlanes[0].mRotSpeed = vec3(0.0f);

            // Right plane
            gPlanes[1].mPos = vec3(37.15f, 25.0f, 0.0f);
            gPlanes[1].mRot = kDegreeToRadian * vec3(-90.0f, 90.0f, 0.0f);
            gPlanes[1].mScale = vec3(50, 50, 50);
            gPlanes[1].mColor = vec4(1.0f);
            gPlanes[1].mRotSpeed = vec3(0.0f);

            // Left plane
            gPlanes[2].mPos = vec3(-37.15f, 25.0f, 0.0f);
            gPlanes[2].mRot = kDegreeToRadian * vec3(90.0f, 90.0f, 0.0f);
            gPlanes[2].mScale = vec3(50, 50, 50);
            gPlanes[2].mColor = vec4(1.0f);
            gPlanes[2].mRotSpeed = vec3(0.0f);

            // Back plane
            gPlanes[3].mPos = vec3(0.0f, 25.0f, 37.15f);
            gPlanes[3].mRot = kDegreeToRadian * vec3(-90.0f, 0.0f, 0.0f);
            gPlanes[3].mScale = vec3(50, 50, 50);
            gPlanes[3].mColor = vec4(1.0f);
            gPlanes[3].mRotSpeed = vec3(0.0f);

            // Front plane
            gPlanes[4].mPos = vec3(0.0f, 25.0f, -37.15f);
            gPlanes[4].mRot = kDegreeToRadian * vec3(90.0f, 0.0f, 0.0f);
            gPlanes[4].mScale = vec3(50, 50, 50);
            gPlanes[4].mColor = vec4(1.0f);
            gPlanes[4].mRotSpeed = vec3(0.0f);

            // Front pipe plane
            gPlanes[5].mPos = vec3(-33.0f, 44.0f, -12.0f);
            gPlanes[5].mRot = kDegreeToRadian * vec3(90.0f, 0.0f, 36.0f);
            gPlanes[5].mScale = vec3(12.0f, 12.0f, 28.0f);
            gPlanes[5].mColor = vec4(0.0, 1.0f, 0.0f, 1.0f);
            gPlanes[5].mRotSpeed = vec3(0.0f);

            // Back pipe plane
            gPlanes[6].mPos = vec3(-33.0f, 44.0f, 12.0f);
            gPlanes[6].mRot = kDegreeToRadian * vec3(90.0f, 0.0f, 36.0f);
            gPlanes[6].mScale = vec3(12.0f, 12.0f, 28.0f);
            gPlanes[6].mColor = vec4(0.0, 1.0f, 0.0f, 1.0f);
            gPlanes[6].mRotSpeed = vec3(0.0f);

            // Bottom pipe plane
            gPlanes[7].mPos = vec3(-29.3f, 25.0f, 0.0f);
            gPlanes[7].mRot = kDegreeToRadian * vec3(36.0f, 90.0f, 0.0f);
            gPlanes[7].mScale = vec3(12.0f, 12.0f, 17.5f);
            gPlanes[7].mColor = vec4(0.0, 1.0f, 0.0f, 1.0f);
            gPlanes[7].mRotSpeed = vec3(0.0f);

            // Rotator plane
            gPlanes[8].mPos = vec3(8, 2.5f, 4.0f);
            gPlanes[8].mRot = kDegreeToRadian * vec3(90.0f, 0.0f, 0.0f);
            gPlanes[8].mScale = vec3(15.0f, 15.0f, 15.0f);
            gPlanes[8].mColor = vec4(0.0f, 0.0f, 1.0f, 1.0f);
            gPlanes[8].mRotSpeed = kDegreeToRadian * vec3(0.0f, 40.0f, 0.0f);
        }

        // Camera
        {
            vec3 lookAt{vec3(0)};
            vec3 camPos{48.0f, 48.0f, 20.0f};
            CameraMotionParameters cmp{160.0f, 600.0f, 200.0f};

            pCameraController = initFpsCameraController(camPos, lookAt);
            pCameraController->setMotionParameters(cmp);
        }

        // Input system
        {
            InputSystemDesc inputDesc = {};
            inputDesc.pRenderer = pRenderer;
            inputDesc.pWindow = pWindow;
            if (!initInputSystem(&inputDesc))
                return false;
        }

        // App actions
        {
            InputActionDesc actionDesc = {DefaultInputActions::DUMP_PROFILE_DATA, [](InputActionContext* ctx) {  dumpProfileData(((Renderer*)ctx->pUserData)->pName); return true; }, pRenderer};
            addInputAction(&actionDesc);
            actionDesc = {DefaultInputActions::TOGGLE_FULLSCREEN, [](InputActionContext* ctx) { toggleFullscreen(((IApp*)ctx->pUserData)->pWindow); return true; }, this};
            addInputAction(&actionDesc);
            actionDesc = {DefaultInputActions::EXIT, [](InputActionContext* ctx) { requestShutdown(); return true; }};
            addInputAction(&actionDesc);
            InputActionCallback onAnyInput = [](InputActionContext* ctx)
            {
                if (ctx->mActionId > UISystemInputActions::UI_ACTION_START_ID_)
                {
                    uiOnInput(ctx->mActionId, ctx->mBool, ctx->pPosition, &ctx->mFloat2);
                }

                if (ctx->pPosition)
                    mousePos = *ctx->pPosition;

                return true;
            };

            typedef bool(*CameraInputHandler)(InputActionContext* ctx, uint32_t index);
            static CameraInputHandler onCameraInput = [](InputActionContext* ctx, uint32_t index)
            {
                if (*(ctx->pCaptured))
                {
                    float2 delta = uiIsFocused() ? float2(0.f, 0.f) : ctx->mFloat2;
                    index ? pCameraController->onRotate(delta) : pCameraController->onMove(delta);
                }
                return true;
            };
            actionDesc = {DefaultInputActions::CAPTURE_INPUT, [](InputActionContext* ctx) {setEnableCaptureInput(!uiIsFocused() && INPUT_ACTION_PHASE_CANCELED != ctx->mPhase);	return true;}, NULL};
            addInputAction(&actionDesc);
            actionDesc = {DefaultInputActions::ROTATE_CAMERA, [](InputActionContext* ctx) { return onCameraInput(ctx, 1); }, NULL};
            addInputAction(&actionDesc);
            actionDesc = {DefaultInputActions::TRANSLATE_CAMERA, [](InputActionContext* ctx) { return onCameraInput(ctx, 0); }, NULL};
            addInputAction(&actionDesc);
            actionDesc = {DefaultInputActions::RESET_CAMERA, [](InputActionContext* ctx) {  setProcessFrame(); return true; }};
            addInputAction(&actionDesc);		
            GlobalInputActionDesc globalInputActionDesc = {GlobalInputActionDesc::ANY_BUTTON_ACTION, onAnyInput, this };
            setGlobalInputAction(&globalInputActionDesc);
        }

        gFrameIndex = 0; 

        return true;
    }

    void Exit()
    {
        exitInputSystem();

        exitCameraController(pCameraController);

        exitUserInterface();

        exitFontSystem();

        // Exit profile
        exitProfiler();

        for (uint32_t i = 0; i < gImageCount; ++i)
        {
            removeResource(pFrameUB[i]);
            removeResource(pPlanesUB[i]);
        }
        
        removeResource(pPlaneVB);
        removeResource(pPlaneIB);
        removeResource(pSphereVB);


        removeResource(pIndexBuffer);
        removeResource(pDensityBuffer);
        removeResource(pParticleBuffer);
        removeResource(pCellIndexBuffer);
        removeResource(pCellOffsetBuffer);

        for (uint32_t i = 0; i < gImageCount; ++i)
        {
            removeFence(pRenderer, pRenderCompleteFences[i]);
            removeSemaphore(pRenderer, pRenderCompleteSemaphores[i]);

            removeCmd(pRenderer, pCmds[i]);
            removeCmdPool(pRenderer, pCmdPools[i]);
        }
        removeSemaphore(pRenderer, pImageAcquiredSemaphore);

        exitResourceLoaderInterface(pRenderer);

        removeQueue(pRenderer, pGraphicsQueue);

        exitRenderer(pRenderer);
        pRenderer = NULL; 
    }

    bool Load(ReloadDesc* pReloadDesc)
    {
        if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
        {
            addShaders();
            addRootSignatures();
            addDescriptorSets();
        }

        if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
        {
            if (!addSwapChain())
                return false;

            if (!addDepthBuffer())
                return false;
        }

        if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
        {
            addPipelines();
        }

        prepareDescriptorSets();

        UserInterfaceLoadDesc uiLoad = {};
        uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        uiLoad.mHeight = mSettings.mHeight;
        uiLoad.mWidth = mSettings.mWidth;
        uiLoad.mLoadType = pReloadDesc->mType;
        loadUserInterface(&uiLoad);

        FontSystemLoadDesc fontLoad = {};
        fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        fontLoad.mHeight = mSettings.mHeight;
        fontLoad.mWidth = mSettings.mWidth;
        fontLoad.mLoadType = pReloadDesc->mType;
        loadFontSystem(&fontLoad);

        return true;
    }

    void Unload(ReloadDesc* pReloadDesc)
    {
        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem(pReloadDesc->mType);
        unloadUserInterface(pReloadDesc->mType);

        if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
        {
            removePipelines();
        }

        if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
        {
            removeSwapChain(pRenderer, pSwapChain);
            removeRenderTarget(pRenderer, pDepthBuffer);
        }

        if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
        {
            removeDescriptorSets();
            removeRootSignatures();
            removeShaders();
        }
    }

    void Update(float deltaTime)
    {
        updateInputSystem(deltaTime, mSettings.mWidth, mSettings.mHeight);

        pCameraController->update(deltaTime);
        
        if (isPaused)
            deltaTime = 0.0f;

        // Update camera and projection
        mat4 viewMat = pCameraController->getViewMatrix();

        const float horizontal_fov = PI / 2.0f;
        const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
        CameraMatrix projMat = CameraMatrix::perspective(horizontal_fov, aspectInverse, 1000.0f, 0.1f);

        // Update frame data
        {
            gFrameData.vp = projMat.getPrimaryMatrix() * viewMat;

            // Calculate simulation params
            {
                gFrameData.SPH_h_rcp = 1.0f / gFrameData.SPH_h;
                gFrameData.SPH_h2 = powf(gFrameData.SPH_h, 2);
                gFrameData.SPH_h3 = powf(gFrameData.SPH_h, 3);

                float h6 = gFrameData.SPH_h2 * gFrameData.SPH_h2 * gFrameData.SPH_h2;
                float h9 = h6 * gFrameData.SPH_h3;
                gFrameData.SPH_visc_constant = (45.0f / (PI * h6));
                gFrameData.SPH_spiky_constant = (-45.0f / (PI * h6));
                gFrameData.SPH_poly6_constant = (315.0f / (64.0f * PI * h9));
            }
        }

        // Update planes data
        {
            for (uint32_t i = 0; i < MAX_PLANE; ++i)
            {
                PlaneObject& plane = gPlanes[i];

                mat4 rotX, rotY, rotZ;
                rotX = rotY = rotZ = mat4::identity();
                if (gPlanes[i].mRotSpeed.getX() > 0.0f)
                    rotX = mat4::rotationX(gPlanes[i].mRotSpeed.getX() * deltaTime);
                if (gPlanes[i].mRotSpeed.getY() > 0.0f)
                    plane.mRot.setY(plane.mRot.getY() + gPlanes[i].mRotSpeed.getY() * deltaTime);
                if (gPlanes[i].mRotSpeed.getZ() > 0.0f)
                    rotZ = mat4::rotationZ(gPlanes[i].mRotSpeed.getZ() * deltaTime);

                mat4 newRot = mat4::rotationZYX(plane.mRot);
                mat4 toWorld = mat4::translation(plane.mPos) * newRot * mat4::scale(plane.mScale);

                gPlanesData.toLocal[i] = inverse(toWorld);
                gPlanesData.toWorld[i] = toWorld;
                gPlanesData.pos[i] = vec4(plane.mPos, 0.0f);
                gPlanesData.normal[i] = normalize(toWorld * vec4(0.0f, 1.0f, 0.0f, 0.0f));
                gPlanesData.color[i] = plane.mColor;
            }
        }

        // Update force field based on mouse position.
        {
            Ray ray = screenPointToRay(mousePos, inverse(gFrameData.vp));
            Plane plane = Plane(vec3(0.0f, 1.0f, 0.0f), gPlanes[0].mPos.getY());

            vec3 worldPos;
            if (rayIntersectsPlane(ray, plane, &worldPos))
                gFrameData.forcePos = float3(worldPos.getX(), worldPos.getY(), worldPos.getZ());
            else
                gFrameData.forcePos = float3(1000.0f, 1000.0f, 1000.0f);
        }
    }

    void Draw()
    {
        if (pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

        RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
        Semaphore*    pRenderCompleteSemaphore = pRenderCompleteSemaphores[gFrameIndex];
        Fence*        pRenderCompleteFence = pRenderCompleteFences[gFrameIndex];

        // Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
        FenceStatus fenceStatus;
        getFenceStatus(pRenderer, pRenderCompleteFence, &fenceStatus);
        if (fenceStatus == FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &pRenderCompleteFence);

        // Update uniform buffers
        {
            BufferUpdateDesc frameCbv = { pFrameUB[gFrameIndex] };
            beginUpdateResource(&frameCbv);
            *(FrameData*)frameCbv.pMappedData = gFrameData;
            endUpdateResource(&frameCbv, NULL);

            BufferUpdateDesc planeCbv = { pPlanesUB[gFrameIndex] };
            beginUpdateResource(&planeCbv);
            *(PlanesData*)planeCbv.pMappedData = gPlanesData;
            endUpdateResource(&planeCbv, NULL);
        }

        if (resetSimulation)
        {
            // reset particles upon request
            resetSimulation = false;

            // Stall CPU and wait for all GPU frames to be presented before updating the GPU buffers.
            waitForFences(pRenderer, gImageCount, pRenderCompleteFences);
            
            // Update particle buffer
            {
                SyncToken token = {};
                BufferUpdateDesc particleBuffer = { pParticleBuffer };
                beginUpdateResource(&particleBuffer);
                generateParticleData((ParticleData*)particleBuffer.pMappedData);
                endUpdateResource(&particleBuffer, &token);
                waitForToken(&token);
            }

            // Update index buffer
            {
                SyncToken token = {};
                BufferUpdateDesc indexBuffer = { pIndexBuffer };
                beginUpdateResource(&indexBuffer);
                generateParticleIndexData((uint32_t*)indexBuffer.pMappedData);
                endUpdateResource(&indexBuffer, &token);
                waitForToken(&token);
            }
        }

        // Reset cmd pool for this frame
        resetCmdPool(pRenderer, pCmdPools[gFrameIndex]);

        Cmd* cmd = pCmds[gFrameIndex];
        beginCmd(cmd);

        cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);
        // Compute passes
        if (canProcessFrame || !isPaused)
        {
            canProcessFrame = false;

            uint32_t maxCount = gFrameData.maxCount;

            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Compute - Partition");
            {
                BufferBarrier bufferBarriers[] =
                {
                    { pIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pParticleBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pCellIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                };
                cmdResourceBarrier(cmd, 3, bufferBarriers, 0, NULL, 0, NULL);

                cmdBindPipeline(cmd, pPartitionPipeline);
                cmdBindDescriptorSet(cmd, gFrameIndex, pComputeFrameDS);

                uint32_t* threadGroupSize = pPartitionShader->pReflection->mStageReflections[0].mNumThreadsPerGroup;
                uint32_t groupCountX = (maxCount  + threadGroupSize[0] - 1) / threadGroupSize[0];
                cmdDispatch(cmd, groupCountX, 1, 1);
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Compute - Sort");
            {
                int presorted = 512;
                uint32_t numThreadGroups = ((uint32_t(maxCount) - 1) >> 9) + 1;
                bool isDone = !(numThreadGroups > 1);
                
                // Sort
                {
                    BufferBarrier bufferBarriers[] =
                    {
                        { pIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                        { pCellIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    };
                    cmdResourceBarrier(cmd, 2, bufferBarriers, 0, NULL, 0, NULL);

                    cmdBindPipeline(cmd, pSortPipeline);
                    cmdBindDescriptorSet(cmd, gFrameIndex, pComputeFrameDS);

                    uint32_t* threadGroupSize = pSortShader->pReflection->mStageReflections[0].mNumThreadsPerGroup;
                    uint32_t groupCountX = (maxCount  + threadGroupSize[0] - 1) / threadGroupSize[0];
                    cmdDispatch(cmd, groupCountX, 1, 1);
                }

                while (!isDone)
                {
                    isDone = true;
                    numThreadGroups = 0;

                    if (maxCount > uint32_t(presorted))
                    {
                        if (maxCount > uint32_t(presorted) * 2)
                            isDone = false;

                        int pow2 = presorted;
                        while (uint32_t(pow2) < maxCount)
                            pow2 *= 2;
                        numThreadGroups = uint32_t(pow2 >> 9);
                    }

                    uint32_t nMergeSize = uint32_t(presorted * 2);
                    for (uint32_t nMergeSubSize = nMergeSize >> 1; nMergeSubSize > 256; nMergeSubSize = nMergeSubSize >> 1)
                    {
                        gRootConstants.args[0] = float(nMergeSubSize);
                        if (nMergeSubSize == nMergeSize >> 1)
                        {
                            gRootConstants.args[1] = float(2 * nMergeSubSize - 1);
                            gRootConstants.args[2] = -1;
                        }
                        else
                        {
                            gRootConstants.args[1] = float(nMergeSubSize);
                            gRootConstants.args[2] = 1;
                        }

                        // Sort step
                        {
                            BufferBarrier bufferBarriers[] =
                            {
                                { pIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                                { pCellIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                            };
                            cmdResourceBarrier(cmd, 2, bufferBarriers, 0, NULL, 0, NULL);

                            cmdBindPipeline(cmd, pSortStepPipeline);
                            cmdBindDescriptorSet(cmd, gFrameIndex, pComputeFrameDS);
                            cmdBindPushConstants(cmd, pComputeRootSignature, gComputeRootConstantIndex, &gRootConstants);

                            cmdDispatch(cmd, int(numThreadGroups), 1, 1);
                        }
                    }

                    // Sort inner
                    {
                        BufferBarrier bufferBarriers[] =
                        {
                            { pIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                            { pCellIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(cmd, 2, bufferBarriers, 0, NULL, 0, NULL);

                        cmdBindPipeline(cmd, pSortInnerPipeline);
                        cmdBindDescriptorSet(cmd, gFrameIndex, pComputeFrameDS);
                        cmdBindPushConstants(cmd, pComputeRootSignature, gComputeRootConstantIndex, &gRootConstants);

                        cmdDispatch(cmd, int(numThreadGroups), 1, 1);
                    }

                    presorted *= 2;
                }
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Compute - Reset Offset");
            {
                BufferBarrier bufferBarriers[] =
                {
                    { pCellOffsetBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                };
                cmdResourceBarrier(cmd, 1, bufferBarriers, 0, NULL, 0, NULL);

                cmdBindPipeline(cmd, pResetOffsetPipeline);
                cmdBindDescriptorSet(cmd, gFrameIndex, pFrameDS);

                uint32_t* threadGroupSize = pResetOffsetShader->pReflection->mStageReflections[0].mNumThreadsPerGroup;
                uint32_t groupCountX = (SPH_PARTITION_BUCKET_COUNT + threadGroupSize[0] - 1) / threadGroupSize[0];
                cmdDispatch(cmd, groupCountX, 1, 1);
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Compute - Partition Offset");
            {
                BufferBarrier bufferBarriers[] =
                {
                    { pIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pCellIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pCellOffsetBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                };
                cmdResourceBarrier(cmd, 3, bufferBarriers, 0, NULL, 0, NULL);

                cmdBindPipeline(cmd, pPartitionOffsetPipeline);
                cmdBindDescriptorSet(cmd, gFrameIndex, pFrameDS);

                uint32_t* threadGroupSize = pPartitionOffsetShader->pReflection->mStageReflections[0].mNumThreadsPerGroup;
                uint32_t groupCountX = (maxCount + threadGroupSize[0] - 1) / threadGroupSize[0];
                cmdDispatch(cmd, groupCountX, 1, 1);
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Compute - Density");
            {
                BufferBarrier bufferBarriers[] =
                {
                    { pIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pDensityBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pParticleBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pCellIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pCellOffsetBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                };
                cmdResourceBarrier(cmd, 5, bufferBarriers, 0, NULL, 0, NULL);

                cmdBindPipeline(cmd, pDensityPipeline);
                cmdBindDescriptorSet(cmd, gFrameIndex, pFrameDS);

                uint32_t* threadGroupSize = pDensityShader->pReflection->mStageReflections[0].mNumThreadsPerGroup;
                uint32_t groupCountX = (maxCount + threadGroupSize[0] - 1) / threadGroupSize[0];
                cmdDispatch(cmd, groupCountX, 1, 1);
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Compute - Force");
            {
                BufferBarrier bufferBarriers[] =
                {
                    { pIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pDensityBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pParticleBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pCellIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pCellOffsetBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                };
                cmdResourceBarrier(cmd, 5, bufferBarriers, 0, NULL, 0, NULL);

                cmdBindPipeline(cmd, pForcePipeline);
                cmdBindDescriptorSet(cmd, gFrameIndex, pFrameDS);

                uint32_t* threadGroupSize = pForceShader->pReflection->mStageReflections[0].mNumThreadsPerGroup;
                uint32_t groupCountX = (maxCount + threadGroupSize[0] - 1) / threadGroupSize[0];
                cmdDispatch(cmd, groupCountX, 1, 1);
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Compute - Simulate");
            {
                BufferBarrier bufferBarriers[] =
                {
                    { pIndexBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                    { pParticleBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
                };
                cmdResourceBarrier(cmd, 2, bufferBarriers, 0, NULL, 0, NULL);

                cmdBindPipeline(cmd, pSimulatePipeline);
                cmdBindDescriptorSet(cmd, gFrameIndex, pFrameDS);

                uint32_t* threadGroupSize = pSimulateShader->pReflection->mStageReflections[0].mNumThreadsPerGroup;
                uint32_t groupCountX = (maxCount + threadGroupSize[0] - 1) / threadGroupSize[0];
                cmdDispatch(cmd, groupCountX, 1, 1);
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Graphics passes
        {
            RenderTargetBarrier barriers[] = 
            {
                { pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET },
            };
            BufferBarrier bufferBarriers[] =
            {
                { pParticleBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS },
            };
            cmdResourceBarrier(cmd, 1, bufferBarriers, 0, NULL, 1, barriers);

            // simply record the screen cleaning command
            LoadActionsDesc loadActions = {};
            loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
            loadActions.mLoadActionDepth = LOAD_ACTION_CLEAR;
            loadActions.mClearDepth.depth = 0.0f;
            cmdBindRenderTargets(cmd, 1, &pRenderTarget, pDepthBuffer, &loadActions, NULL, NULL, -1, -1);
            cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
            cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Graphics - Planes");
            {
                // Draw instanced planes (culling back faces)
                const uint32_t planeVbStride = sizeof(float) * 6;
                cmdBindPipeline(cmd, pPlaneInstancePipline);
                cmdBindDescriptorSet(cmd, gFrameIndex, pFrameDS);
                cmdBindIndexBuffer(cmd, pPlaneIB, INDEX_TYPE_UINT16, 0);
                cmdBindVertexBuffer(cmd, 1, &pPlaneVB, &planeVbStride, NULL);
                cmdDrawIndexedInstanced(cmd, gPlaneIndexCount, 0, MAX_PLANE - 1, 0, 0);

                // Draw the rotator (without culling back faces)
                cmdBindPipeline(cmd, pPlanePipeline);
                gRootConstants.args.setX(MAX_PLANE - 1);
                cmdBindPushConstants(cmd, pRootSignature, gRootConstantIndex, &gRootConstants);
                cmdDrawIndexed(cmd, gPlaneIndexCount, 0, 0);
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Graphics - Spheres");
            {
                // Draw particles as instanced spheres
                const uint32_t sphereVbStride = sizeof(float) * 6;
                gRootConstants.args = particleColor.toVec4();
                
                cmdBindPipeline(cmd, pSpherePipeline);
                cmdBindPushConstants(cmd, pRootSignature, gRootConstantIndex, &gRootConstants);
                cmdBindDescriptorSet(cmd, gFrameIndex, pFrameDS);
                cmdBindVertexBuffer(cmd, 1, &pSphereVB, &sphereVbStride, NULL);
                cmdDrawInstanced(cmd, gNumberOfSpherePoints / 6, 0, gFrameData.maxCount, 0);
            }
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // UI and submit
        {
            LoadActionsDesc loadActions = {};
            loadActions.mLoadActionsColor[0] = LOAD_ACTION_LOAD;
            cmdBindRenderTargets(cmd, 1, &pRenderTarget, nullptr, &loadActions, NULL, NULL, -1, -1);
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw UI");

            gFrameTimeDraw.mFontColor = 0xff00ffff;
            gFrameTimeDraw.mFontSize = 18.0f;
            gFrameTimeDraw.mFontID = gFontID;
            float2 txtSizePx = cmdDrawCpuProfile(cmd, float2(8.f, 15.f), &gFrameTimeDraw);
            cmdDrawGpuProfile(cmd, float2(8.f, txtSizePx.y + 75.f), gGpuProfileToken, &gFrameTimeDraw);

            cmdDrawUserInterface(cmd);

            cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

            RenderTargetBarrier barriers[] =
            {
                pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT
            };
            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

            cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
            endCmd(cmd);

            QueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.mSignalSemaphoreCount = 1;
            submitDesc.mWaitSemaphoreCount = 1;
            submitDesc.ppCmds = &cmd;
            submitDesc.ppSignalSemaphores = &pRenderCompleteSemaphore;
            submitDesc.ppWaitSemaphores = &pImageAcquiredSemaphore;
            submitDesc.pSignalFence = pRenderCompleteFence;
            queueSubmit(pGraphicsQueue, &submitDesc);
            QueuePresentDesc presentDesc = {};
            presentDesc.mIndex = swapchainImageIndex;
            presentDesc.mWaitSemaphoreCount = 1;
            presentDesc.pSwapChain = pSwapChain;
            presentDesc.ppWaitSemaphores = &pRenderCompleteSemaphore;
            presentDesc.mSubmitDone = true;
            
            queuePresent(pGraphicsQueue, &presentDesc);
            flipProfiler();
        }

        gFrameIndex = (gFrameIndex + 1) % gImageCount;
    }

    const char* GetName() { return "Fluid Simluation"; }

    bool addSwapChain()
    {
        SwapChainDesc swapChainDesc = {};
        swapChainDesc.mWindowHandle = pWindow->handle;
        swapChainDesc.mPresentQueueCount = 1;
        swapChainDesc.ppPresentQueues = &pGraphicsQueue;
        swapChainDesc.mWidth = mSettings.mWidth;
        swapChainDesc.mHeight = mSettings.mHeight;
        swapChainDesc.mImageCount = gImageCount;
        swapChainDesc.mColorFormat = getRecommendedSwapchainFormat(true, true);
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mFlags = SWAP_CHAIN_CREATION_FLAG_ENABLE_FOVEATED_RENDERING_VR;
        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    bool addDepthBuffer()
    {
        // Add depth buffer
        RenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue.depth = 0.0f;
        depthRT.mClearValue.stencil = 0;
        depthRT.mDepth = 1;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = RESOURCE_STATE_DEPTH_WRITE;
        depthRT.mHeight = mSettings.mHeight;
        depthRT.mSampleCount = SAMPLE_COUNT_1;
        depthRT.mSampleQuality = 0;
        depthRT.mWidth = mSettings.mWidth;
        depthRT.mFlags = TEXTURE_CREATION_FLAG_ON_TILE | TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

        return pDepthBuffer != NULL;
    }

    void addDescriptorSets()
    {
        DescriptorSetDesc desc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount * DESCRIPTOR_SET_COUNT };
        addDescriptorSet(pRenderer, &desc, &pFrameDS);

        desc = { pComputeRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount * COMPUTE_DESCRIPTOR_SET_COUNT };
        addDescriptorSet(pRenderer, &desc, &pComputeFrameDS);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pFrameDS);
        removeDescriptorSet(pRenderer, pComputeFrameDS);
    }

    void addRootSignatures()
    {
        // Graphics
        {
            Shader* shaders[3];
            uint32_t shadersCount = 3;
            shaders[0] = pPlaneShader;
            shaders[1] = pPlaneInstanceShader;
            shaders[2] = pSphereShader;

            RootSignatureDesc rootDesc = {};
            rootDesc.mStaticSamplerCount = 0;
            rootDesc.ppStaticSamplerNames = NULL;
            rootDesc.ppStaticSamplers = NULL;
            rootDesc.mShaderCount = shadersCount;
            rootDesc.ppShaders = shaders;
            addRootSignature(pRenderer, &rootDesc, &pRootSignature);
            gRootConstantIndex = getDescriptorIndexFromName(pRootSignature, "rootConstants");
        }

        // Compute
        {
            Shader* shaders[9];
            uint32_t shadersCount = 9;
            shaders[0] = pPartitionShader;
            shaders[1] = pResetOffsetShader;
            shaders[2] = pPartitionOffsetShader;
            shaders[3] = pDensityShader;
            shaders[4] = pForceShader;
            shaders[5] = pSimulateShader;
            shaders[6] = pSortShader;
            shaders[7] = pSortStepShader;
            shaders[8] = pSortInnerShader;

            RootSignatureDesc rootDesc = {};
            rootDesc.mStaticSamplerCount = 0;
            rootDesc.ppStaticSamplerNames = NULL;
            rootDesc.ppStaticSamplers = NULL;
            rootDesc.mShaderCount = shadersCount;
            rootDesc.ppShaders = shaders;
            addRootSignature(pRenderer, &rootDesc, &pComputeRootSignature);
            gComputeRootConstantIndex = getDescriptorIndexFromName(pComputeRootSignature, "rootConstants");
        }
    }

    void removeRootSignatures()
    {
        removeRootSignature(pRenderer, pRootSignature);
        removeRootSignature(pRenderer, pComputeRootSignature);
    }

    void addShaders()
    {
        ShaderLoadDesc shader = {};
        shader.mStages[0] = { "basic.vert", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        shader.mStages[1] = { "basic.frag", NULL, 0 };
        addShader(pRenderer, &shader, &pPlaneShader);

        shader.mStages[0] = { "instanced.vert", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        shader.mStages[1] = { "basic.frag", NULL, 0 };
        addShader(pRenderer, &shader, &pPlaneInstanceShader);

        shader.mStages[0] = { "sphereInstanced.vert", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        shader.mStages[1] = { "basic.frag", NULL, 0 };
        addShader(pRenderer, &shader, &pSphereShader);

        ShaderLoadDesc compShader = {};
        compShader.mStages[0] = { "partition.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pPartitionShader);

        compShader.mStages[0] = { "resetOffset.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pResetOffsetShader);

        compShader.mStages[0] = { "partitionOffset.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pPartitionOffsetShader);

        compShader.mStages[0] = { "density.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pDensityShader);

        compShader.mStages[0] = { "force.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pForceShader);

        compShader.mStages[0] = { "simulate.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pSimulateShader);

        compShader.mStages[0] = { "sort.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pSortShader);

        compShader.mStages[0] = { "sortStep.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pSortStepShader);

        compShader.mStages[0] = { "sortInner.comp", NULL, 0, NULL, SHADER_STAGE_LOAD_FLAG_NONE };
        addShader(pRenderer, &compShader, &pSortInnerShader);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pPartitionShader);
        removeShader(pRenderer, pResetOffsetShader);
        removeShader(pRenderer, pPartitionOffsetShader);
        removeShader(pRenderer, pDensityShader);
        removeShader(pRenderer, pForceShader);
        removeShader(pRenderer, pSimulateShader);
        removeShader(pRenderer, pSortShader);
        removeShader(pRenderer, pSortStepShader);
        removeShader(pRenderer, pSortInnerShader);

        removeShader(pRenderer, pSphereShader);
        removeShader(pRenderer, pPlaneShader);
        removeShader(pRenderer, pPlaneInstanceShader);
    }

    void addPipelines()
    {
        // Graphics pipelines
        {
            VertexLayout vertexLayout = {};
            vertexLayout.mAttribCount = 2;
            vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
            vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
            vertexLayout.mAttribs[0].mBinding = 0;
            vertexLayout.mAttribs[0].mLocation = 0;
            vertexLayout.mAttribs[0].mOffset = 0;
            vertexLayout.mAttribs[1].mSemantic = SEMANTIC_NORMAL;
            vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
            vertexLayout.mAttribs[1].mBinding = 0;
            vertexLayout.mAttribs[1].mLocation = 1;
            vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

            RasterizerStateDesc planeRsDesc = {};
            planeRsDesc.mCullMode = CULL_MODE_FRONT;

            RasterizerStateDesc noCullRsDesc = {};
            noCullRsDesc.mCullMode = CULL_MODE_NONE;

            DepthStateDesc depthStateDesc = {};
            depthStateDesc.mDepthTest = true;
            depthStateDesc.mDepthWrite = true;
            depthStateDesc.mDepthFunc = CMP_GEQUAL;

            PipelineDesc desc = {};
            desc.pName = "Plane";
            desc.mType = PIPELINE_TYPE_GRAPHICS;
            GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
            pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
            pipelineSettings.mRenderTargetCount = 1;
            pipelineSettings.pDepthState = &depthStateDesc;
            pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
            pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
            pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
            pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
            pipelineSettings.pRootSignature = pRootSignature;
            pipelineSettings.pShaderProgram = pPlaneInstanceShader;
            pipelineSettings.pVertexLayout = &vertexLayout;
            pipelineSettings.pRasterizerState = &planeRsDesc;
            pipelineSettings.mVRFoveatedRendering = false;
            addPipeline(pRenderer, &desc, &pPlaneInstancePipline);

            desc.pName = "Sphere";
            pipelineSettings.pShaderProgram = pSphereShader;
            addPipeline(pRenderer, &desc, &pSpherePipeline);

            desc.pName = "Plane - No Cull";
            pipelineSettings.pShaderProgram = pPlaneShader;
            pipelineSettings.pRasterizerState = &noCullRsDesc;
            addPipeline(pRenderer, &desc, &pPlanePipeline);
        }

        // Compute pipelines
        {
            PipelineDesc desc = {};
            desc.pName = "Partition";
            desc.mType = PIPELINE_TYPE_COMPUTE;
            ComputePipelineDesc& computeDesc = desc.mComputeDesc;
            computeDesc.pRootSignature = pComputeRootSignature;
            computeDesc.pShaderProgram = pPartitionShader;
            addPipeline(pRenderer, &desc, &pPartitionPipeline);

            desc.pName = "Reset Offset";
            computeDesc.pShaderProgram = pResetOffsetShader;
            addPipeline(pRenderer, &desc, &pResetOffsetPipeline);

            desc.pName = "Partition Offset";
            computeDesc.pShaderProgram = pPartitionOffsetShader;
            addPipeline(pRenderer, &desc, &pPartitionOffsetPipeline);

            desc.pName = "Density";
            computeDesc.pShaderProgram = pDensityShader;
            addPipeline(pRenderer, &desc, &pDensityPipeline);

            desc.pName = "Force";
            computeDesc.pShaderProgram = pForceShader;
            addPipeline(pRenderer, &desc, &pForcePipeline);

            desc.pName = "Simulate";
            computeDesc.pShaderProgram = pSimulateShader;
            addPipeline(pRenderer, &desc, &pSimulatePipeline);

            // AMD sort shaders
            {
                desc.pName = "Sort";
                computeDesc.pShaderProgram = pSortShader;
                addPipeline(pRenderer, &desc, &pSortPipeline);

                desc.pName = "Sort Step";
                computeDesc.pShaderProgram = pSortStepShader;
                addPipeline(pRenderer, &desc, &pSortStepPipeline);

                desc.pName = "Sort Inner";
                computeDesc.pShaderProgram = pSortInnerShader;
                addPipeline(pRenderer, &desc, &pSortInnerPipeline);
            }
        }
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pSortPipeline);
        removePipeline(pRenderer, pForcePipeline);
        removePipeline(pRenderer, pDensityPipeline);
        removePipeline(pRenderer, pSortStepPipeline);
        removePipeline(pRenderer, pSimulatePipeline);
        removePipeline(pRenderer, pPartitionPipeline);
        removePipeline(pRenderer, pSortInnerPipeline);
        removePipeline(pRenderer, pResetOffsetPipeline);
        removePipeline(pRenderer, pPartitionOffsetPipeline);

        removePipeline(pRenderer, pPlanePipeline);
        removePipeline(pRenderer, pSpherePipeline);
        removePipeline(pRenderer, pPlaneInstancePipline);
    }

    void prepareDescriptorSets()
    {
        for (uint32_t i = 0; i < gImageCount; ++i)
        {
            DescriptorData params[COMPUTE_DESCRIPTOR_SET_COUNT] = {};
            params[0].pName = "frameUB";
            params[0].ppBuffers = &pFrameUB[i];

            params[1].pName = "planesUB";
            params[1].ppBuffers = &pPlanesUB[i];

            params[2].pName = "particleBuffer";
            params[2].ppBuffers = &pParticleBuffer;

            params[3].pName = "indexBuffer";
            params[3].ppBuffers = &pIndexBuffer;

            params[4].pName = "densityBuffer";
            params[4].ppBuffers = &pDensityBuffer;

            params[5].pName = "cellIndexBuffer";
            params[5].ppBuffers = &pCellIndexBuffer;

            params[6].pName = "cellOffsetBuffer";
            params[6].ppBuffers = &pCellOffsetBuffer; 

            updateDescriptorSet(pRenderer, i, pFrameDS, DESCRIPTOR_SET_COUNT, params);
            updateDescriptorSet(pRenderer, i, pComputeFrameDS, COMPUTE_DESCRIPTOR_SET_COUNT, params);
        }
    }

    ParticleData* generateParticleData(ParticleData* preAllocated = NULL) const
    {
        ParticleData* particleData = preAllocated;
        uint32_t maxCount = gFrameData.maxCount;

        if (!particleData)
            particleData = (ParticleData*)tf_malloc(sizeof(ParticleData) * maxCount);

        // Spawn particles in area of a sphere
        for (uint32_t i = 0; i < maxCount; ++i)
        {
            vec3 pos = vec3(spawnCenter.x, spawnCenter.y, spawnCenter.z) + (randomPointOnUnitSphere() * spawnRadius);
            particleData[i] = ParticleData
            {
                vec4(pos, 1.0f),
                vec4(0.0f),
                vec4(0.0f),
            };
        }
        return particleData;
    }

    uint32_t* generateParticleIndexData(uint32_t* preAllocated = NULL) const
    {
        uint32_t* indexData = preAllocated;
        uint32_t maxCount = gFrameData.maxCount;

        if (!indexData)
            indexData = (uint32_t*)tf_malloc(sizeof(uint32_t) * maxCount);

        for (uint32_t i = 0; i < maxCount; ++i)
            indexData[i] = i;

        return indexData;
    }

    vec3 randomPointOnUnitSphere() const
    {
        float u = randomFloat01();
        float v = randomFloat01();
        float theta = 2 * PI * u;
        float phi = acosf(2 * v - 1);
        float x = sinf(phi) * cosf(theta);
        float y = sinf(phi) * sinf(theta);
        float z = cosf(phi);
        return vec3(x, y, z);
    }

    Ray screenPointToRay(float2 screenPos, const mat4& invVP) const
    {
        vec3 originPoint = vec3(screenPos.x, screenPos.y, 0.0f);
        vec3 targetPoint = vec3(screenPos.x, screenPos.y, 1.0f);

        originPoint = screenPointToWorld(originPoint, invVP);
        targetPoint = screenPointToWorld(targetPoint, invVP);

        return Ray(originPoint, targetPoint - originPoint);
    }

    vec3 screenPointToWorld(const vec3& vec, const mat4& invVP) const
    {
        vec4 screenPos(vec.getX() / float(mSettings.mWidth), vec.getY() / float(mSettings.mHeight), vec.getZ(), 1.0f);
        screenPos.setX(screenPos.getX() * 2.0f - 1.0f);
        screenPos.setY(1.0f - screenPos.getY() * 2.0f);
        screenPos.setZ(screenPos.getZ() * 2.0f - 1.0f);

        screenPos = invVP * screenPos;
        if (screenPos.getW() != 0.0f)
        {
            screenPos.setX(screenPos.getX() / screenPos.getW());
            screenPos.setY(screenPos.getY() / screenPos.getW());
            screenPos.setZ(screenPos.getZ() / screenPos.getW());
        }

        return vec3(screenPos.getX(), screenPos.getY(), screenPos.getZ());
    }
};

DEFINE_APPLICATION_MAIN(FluidSim)
