// This file contains the main funciton at the bottom
 
#include "BlitzenVulkan/vulkanRenderer.h"

#include "mainEngine.h"
#include "Platform/platform.h"



namespace BlitzenEngine
{
    // This will hold pointer to all the renderers (not really necessary but whatever)
    struct BlitzenRenderers
    {
        BlitzenVulkan::VulkanRenderer* pVulkan = nullptr;
    };

    // Static member variable needs to be declared
    Engine* Engine::pEngineInstance;

    Engine::Engine()
    {
        // There should not be a 2nd instance of Blitzen Engine
        if(GetEngineInstancePointer())
        {
            BLIT_ERROR("Blitzen is already active")
            return;
        }

        // Initialize the engine if no other instance seems to exist
        else
        {
            // The instance is the first thing that gets initalized
            pEngineInstance = this;
            m_systems.engine = 1;
            BLIT_INFO("%s Booting", BLITZEN_VERSION)

            if(BlitzenCore::InitLogging())
                BLIT_DBLOG("Test Logging")
            else
                BLIT_ERROR("Logging is not active")

            // Engine owns the event system
            if(BlitzenCore::EventsInit())
            {
                BLIT_INFO("Event system active")
                BlitzenCore::InputInit(&m_systems.inputState);
            }
            else
                BLIT_FATAL("Event system initialization failed!")

            if(!BlitzenEngine::LoadResourceSystem(m_resources))
            {
                BLIT_FATAL("Resource system initalization failed")
            }

            // Assert if platform specific code is initialized, the engine cannot continue without it
            BLIT_ASSERT(BlitzenPlatform::PlatformStartup(BLITZEN_VERSION, BLITZEN_WINDOW_STARTING_X, 
            BLITZEN_WINDOW_STARTING_Y, m_platformData.windowWidth, m_platformData.windowHeight))

            // Register some default events, like window closing on escape
            BlitzenCore::RegisterEvent(BlitzenCore::BlitEventType::EngineShutdown, nullptr, OnEvent);
            BlitzenCore::RegisterEvent(BlitzenCore::BlitEventType::KeyPressed, nullptr, OnKeyPress);
            BlitzenCore::RegisterEvent(BlitzenCore::BlitEventType::KeyReleased, nullptr, OnKeyPress);
            BlitzenCore::RegisterEvent(BlitzenCore::BlitEventType::WindowResize, nullptr, OnResize);
        }
    }



    /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        This function currently holds the majority of the functionality called during runtime.
        Its scope owns the memory used by each renderer(only Vulkan for the forseeable future.
        It calls every function needed to draw a frame and other functionality the engine has
    !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
    void Engine::Run()
    {
        #if BLITZEN_VULKAN
            BlitCL::SmartPointer<BlitzenVulkan::VulkanRenderer, BlitzenCore::AllocationType::Renderer> pVulkan;
        #endif
        {
            uint8_t hasRenderer = 0;

            #if BLITZEN_VULKAN
                if(pVulkan.Data())
                {
                    m_systems.vulkan = pVulkan.Data()->Init(m_platformData.windowWidth, m_platformData.windowHeight);
                    hasRenderer = m_systems.vulkan;
                    m_renderer = ActiveRenderer::Vulkan;
                }
            #endif

            BLIT_ASSERT_MESSAGE(hasRenderer, "Blitzen cannot continue without a renderer")
        }

        isRunning = 1;
        isSupended = 0;

        m_camera.projectionMatrix = BlitML::InfiniteZPerspective(BlitML::Radians(45.f), static_cast<float>(m_platformData.windowWidth) / 
        static_cast<float>(m_platformData.windowHeight), 0.1f);
        BlitML::vec3 initialCameraPosition(0.f, 0.f, 0.f);
        m_camera.viewMatrix = BlitML::Mat4Inverse(BlitML::Translate(initialCameraPosition));
        m_camera.projectionViewMatrix = m_camera.projectionMatrix * m_camera.viewMatrix;

        // The transpose of the projection matrix will be used for frustum culling
        /*glm::mat4 projectionTranspose = glm::transpose(glm::mat4(m_camera.projectionMatrix[0], m_camera.projectionMatrix[1], 
        m_camera.projectionMatrix[2], m_camera.projectionMatrix[3], m_camera.projectionMatrix[4], m_camera.projectionMatrix[5], 
        m_camera.projectionMatrix[6], m_camera.projectionMatrix[7], m_camera.projectionMatrix[8], m_camera.projectionMatrix[9], 
        m_camera.projectionMatrix[10], m_camera.projectionMatrix[11], m_camera.projectionMatrix[12], m_camera.projectionMatrix[13], 
        m_camera.projectionMatrix[14], m_camera.projectionMatrix[15])); Keeping this her in case my math library is porven inadequate*/

        // The transpose of the projection matrix will be used for frustum culling
        m_camera.projectionTranspose = BlitML::Transpose(m_camera.projectionViewMatrix);

        // Loads textures that were requested
        LoadTextures();
        LoadMaterials();
        LoadDefaultData(m_resources);



        /*----------------------------------
            Setup for Vulkan rendering
        ------------------------------------*/

        // This is declared outide the setup for rendering braces, as it will be passed to render context during the loop
        BlitzenVulkan::DrawObject* draws = reinterpret_cast<BlitzenVulkan::DrawObject*>(
        BlitzenCore::BlitAllocLinear(BlitzenCore::AllocationType::LinearAlloc, 100000 * sizeof(BlitzenVulkan::DrawObject)));
        uint32_t drawCount = 0;
        #if BLITZEN_VULKAN
        {
            // Combine all the surfaces from every mesh, into the draw objects (this will not be needed once I switch to indirect drawing)
            for(size_t i = 0; i < 100000; ++i)
            {
                PrimitiveSurface& currentSurface = m_resources.meshes[0].surfaces[0];
                BlitzenVulkan::DrawObject& currentDraw = draws[i];

                currentDraw.firstIndex = currentSurface.firstIndex;
                currentDraw.indexCount = currentSurface.indexCount;
                currentDraw.firstMeshlet = currentSurface.firstMeshlet;
                currentDraw.meshletCount = currentSurface.meshletCount;
                currentDraw.objectTag = drawCount;
                drawCount++;  
            }

            BlitzenVulkan::GPUData vulkanData(m_resources.vertices, m_resources.indices, m_resources.meshlets);
            vulkanData.pTextures = m_resources.textures;
            // The index where the resource system stopped when loading is the amount of mesh assets that were added to the array
            vulkanData.textureCount = m_resources.currentTextureIndex;
            vulkanData.pMaterials = m_resources.materials;
            vulkanData.materialCount = m_resources.currentMaterialIndex;
            vulkanData.pMeshes = m_resources.meshes;
            vulkanData.meshCount = m_resources.currentMeshIndex;

            pVulkan.Data()->UploadDataToGPUAndSetupForRendering(vulkanData);
        }// Vulkan renderer ready
        #endif



        // Should be called right before the main loop starts
        StartClock();
        double previousTime = m_clock.elapsedTime;
        // Main Loop starts
        while(isRunning)
        {
            if(!BlitzenPlatform::PlatformPumpMessages())
            {
                isRunning = 0;
            }

            if(!isSupended)
            {
                // Update the clock and deltaTime
                m_clock.elapsedTime = BlitzenPlatform::PlatformGetAbsoluteTime() - m_clock.startTime;
                double deltaTime = m_clock.elapsedTime - previousTime;
                previousTime = m_clock.elapsedTime;

                UpdateCamera(m_camera, (float)deltaTime);


                // Setting up draw frame for active renderer and calling it
                switch(m_renderer)
                {
                    #if BLITZEN_VULKAN
                    case ActiveRenderer::Vulkan:
                    {
                        if(m_systems.vulkan)
                        {
                            BlitzenVulkan::RenderContext renderContext;
                            renderContext.windowResize = m_platformData.windowResize;
                            renderContext.windowWidth = m_platformData.windowWidth;
                            renderContext.windowHeight = m_platformData.windowHeight;

                            renderContext.projectionMatrix = m_camera.projectionMatrix;
                            renderContext.viewMatrix = m_camera.viewMatrix;
                            renderContext.projectionView = m_camera.projectionViewMatrix;
                            renderContext.viewPosition = m_camera.position;
                            renderContext.projectionTranspose = m_camera.projectionTranspose;

                            renderContext.pDraws = draws;
                            renderContext.drawCount = drawCount;

                            // Hardcoding the sun for now
                            renderContext.sunlightDirection = BlitML::vec3(-0.57735f, -0.57735f, 0.57735f);
                            renderContext.sunlightColor = BlitML::vec4(0.8f, 0.8f, 0.8f, 1.0f);

                            pVulkan.Data()->DrawFrame(renderContext);
                        }
                        break;
                    }
                    #endif
                    default:
                    {
                        break;
                    }
                }




                // Make sure that the window resize is set to false after the renderer is notified
                m_platformData.windowResize = 0;

                BlitzenCore::UpdateInput(deltaTime);
            }
        }
        // Main loop ends
        StopClock();

        // The renderer is shutdown here as its memory scope it this Run function
        m_renderer = ActiveRenderer::MaxRenderers;
        #if BLITZEN_VULKAN
            m_systems.vulkan = 0;
            pVulkan.Data()->Shutdown();
        #endif

        // With the main loop done, Blitzen calls Shutdown on itself
        Shutdown();
    }







    void Engine::LoadTextures()
    {
        // Default texture at index 0
        uint32_t blitTexCol = glm::packUnorm4x8(glm::vec4(0.3, 0, 0.6, 1));
        uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	    uint32_t pixels[16 * 16]; 
	    for (int x = 0; x < 16; x++) 
        {
	    	for (int y = 0; y < 16; y++) 
            {
	    		pixels[y*16 + x] = ((x % 2) ^ (y % 2)) ? magenta : blitTexCol;
	    	}
	    }
        m_resources.textures[0].pTextureData = reinterpret_cast<uint8_t*>(pixels);
        m_resources.textures[0].textureHeight = 1;
        m_resources.textures[0].textureWidth = 1;
        m_resources.textures[0].textureChannels = 4;
        m_resources.textureTable.Set(BLIT_DEFAULT_TEXTURE_NAME, &(m_resources.textures[0]));
        // Default texture set
        

        // This is hardcoded now, but this is how all textures will be loaded
        LoadTextureFromFile(m_resources, "Assets/Textures/cobblestone.png", "loaded_texture");
        LoadTextureFromFile(m_resources, "Assets/Textures/texture.jpg", "loaded_texture2");
        LoadTextureFromFile(m_resources, "Assets/Textures/cobblestone_SPEC.jpg", "spec_texture");
    }

    void Engine::LoadMaterials()
    {
        // Manually load a default material at index 0
        m_resources.materials[0].diffuseColor = BlitML::vec4(1.f);
        m_resources.materials[0].diffuseMapName = BLIT_DEFAULT_TEXTURE_NAME;
        m_resources.materials[0].diffuseMapTag = m_resources.textureTable.Get(BLIT_DEFAULT_TEXTURE_NAME, &m_resources.textures[0])->textureTag;
        m_resources.materials[0].specularMapTag = m_resources.textureTable.Get(BLIT_DEFAULT_TEXTURE_NAME, &m_resources.textures[0])->textureTag;
        m_resources.materials[0].materialTag = 0;
        m_resources.materialTable.Set(BLIT_DEFAULT_MATERIAL_NAME, &(m_resources.materials[0]));

        // Test code
        BlitML::vec4 color1(0.1f);
        BlitML::vec4 color2(0.2f);
        DefineMaterial(m_resources, color1, 65.f, "loaded_texture", "spec_texture", "loaded_material");
        DefineMaterial(m_resources, color2, 65.f, "loaded_texture2", "unknown", "loaded_material2");
    }

    void Engine::StartClock()
    {
        m_clock.startTime = BlitzenPlatform::PlatformGetAbsoluteTime();
        m_clock.elapsedTime = 0;
    }

    void Engine::StopClock()
    {
        m_clock.elapsedTime = 0;
    }

    // This will move from here once I add a camera system
    void UpdateCamera(Camera& camera, float deltaTime)
    {
        if (camera.cameraDirty)
        {
            // I haven't overloaded the += operator
            camera.position = camera.position + camera.velocity * deltaTime * 40.f; 

            BlitML::mat4 translation = BlitML::Mat4Inverse(BlitML::Translate(camera.position));

            camera.viewMatrix = translation; // Normally, I would also add rotation here but the math library has a few problems at the moment
            camera.projectionViewMatrix = camera.projectionMatrix * camera.viewMatrix;
            camera.projectionTranspose = BlitML::Transpose(camera.projectionViewMatrix);
        }
    }



    void Engine::Shutdown()
    {
        if (m_systems.engine)
        {
            BLIT_WARN("Blitzen is shutting down")

            m_systems.logSystem = 0;
            BlitzenCore::ShutdownLogging();

            m_systems.eventSystem = 0;
            BlitzenCore::EventsShutdown();

            m_systems.inputSystem = 0;
            BlitzenCore::InputShutdown();

            BlitzenPlatform::PlatformShutdown();

            m_systems.engine = 0;
            pEngineInstance = nullptr;
        }

        // The destructor should not be calle more than once as it will crush the application
        else
        {
            BLIT_ERROR("Any uninitialized instances of Blitzen will not be explicitly cleaned up")
        }
    }




    uint8_t OnEvent(BlitzenCore::BlitEventType eventType, void* pSender, void* pListener, BlitzenCore::EventContext data)
    {
        if(eventType == BlitzenCore::BlitEventType::EngineShutdown)
        {
            BLIT_WARN("Engine shutdown event encountered!")
            BlitzenEngine::Engine::GetEngineInstancePointer()->RequestShutdown();
            return 1; 
        }

        return 0;
    }

    uint8_t OnKeyPress(BlitzenCore::BlitEventType eventType, void* pSender, void* pListener, BlitzenCore::EventContext data)
    {
        //Get the key pressed from the event context
        BlitzenCore::BlitKey key = static_cast<BlitzenCore::BlitKey>(data.data.ui16[0]);

        if(eventType == BlitzenCore::BlitEventType::KeyPressed)
        {
            switch(key)
            {
                case BlitzenCore::BlitKey::__ESCAPE:
                {
                    BlitzenCore::EventContext newContext = {};
                    BlitzenCore::FireEvent(BlitzenCore::BlitEventType::EngineShutdown, nullptr, newContext);
                    return 1;
                }
                case BlitzenCore::BlitKey::__W:
                {
                    Camera& camera = Engine::GetEngineInstancePointer()->GetCamera();
                    camera.cameraDirty = 1;
                    camera.velocity = BlitML::vec3(0.f, 0.f, 1.f);
                    break;
                }
                case BlitzenCore::BlitKey::__S:
                {
                    Camera& camera = Engine::GetEngineInstancePointer()->GetCamera();
                    camera.cameraDirty = 1;
                    camera.velocity = BlitML::vec3(0.f, 0.f, -1.f);
                    break;
                }
                case BlitzenCore::BlitKey::__A:
                {
                    Camera& camera = Engine::GetEngineInstancePointer()->GetCamera();
                    camera.cameraDirty = 1;
                    camera.velocity = BlitML::vec3(-1.f, 0.f, 0.f);
                    break;
                }
                case BlitzenCore::BlitKey::__D:
                {
                    Camera& camera = Engine::GetEngineInstancePointer()->GetCamera();
                    camera.cameraDirty = 1;
                    camera.velocity = BlitML::vec3(1.f, 0.f, 0.f);
                    break;
                }
                default:
                {
                    BLIT_DBLOG("Key pressed %i", key)
                    return 1;
                }
            }
        }
        else if (eventType == BlitzenCore::BlitEventType::KeyReleased)
        {
            switch (key)
            {
                case BlitzenCore::BlitKey::__W:
                case BlitzenCore::BlitKey::__S:
                {
                    Camera& camera = Engine::GetEngineInstancePointer()->GetCamera();
                    camera.velocity.z = 0.f;
                    if(camera.velocity.y == 0.f && camera.velocity.x == 0.f)
                    {
                        camera.cameraDirty = 0;
                    }
                    break;
                }
                case BlitzenCore::BlitKey::__A:
                case BlitzenCore::BlitKey::__D:
                {
                    Camera& camera = Engine::GetEngineInstancePointer()->GetCamera();
                    camera.velocity.x = 0.f;
                    if (camera.velocity.y == 0.f && camera.velocity.z == 0.f)
                    {
                        camera.cameraDirty = 0;
                    }
                    break;
                }
            }
        }
        return 0;
    }

    uint8_t OnResize(BlitzenCore::BlitEventType eventType, void* pSender, void* pListener, BlitzenCore::EventContext data)
    {
        uint32_t newWidth = data.data.ui32[0];
        uint32_t newHeight = data.data.ui32[1];

        Engine::GetEngineInstancePointer()->UpdateWindowSize(newWidth, newHeight);

        return 1;
    }

    void Engine::UpdateWindowSize(uint32_t newWidth, uint32_t newHeight) 
    {
        m_platformData.windowWidth = newWidth; 
        m_platformData.windowHeight = newHeight;
        m_platformData.windowResize = 1;
        if(newWidth == 0 || newHeight == 0)
        {
            isSupended = 1;
            return;
        }
        isSupended = 0;

        //m_camera.projectionMatrix = BlitML::Perspective(BlitML::Radians(45.f), (float)newWidth / (float)newHeight,
        //10000.f, 0.1f);
        m_camera.projectionMatrix = BlitML::InfiniteZPerspective(BlitML::Radians(45.f), static_cast<float>(m_platformData.windowWidth) / 
        static_cast<float>(m_platformData.windowHeight), 0.1f);
        m_camera.projectionViewMatrix = m_camera.projectionMatrix * m_camera.viewMatrix;
    }
}







int main()
{
    BlitzenCore::MemoryManagementInit();

    // Blitzen engine Allocated and freed inside this scope
    {
        // Blitzen engine allocated with smart pointer, it will cause stack overflow otherwise
        BlitCL::SmartPointer<BlitzenEngine::Engine, BlitzenCore::AllocationType::Engine> Blitzen;

        // This is basically the true main function
        Blitzen.Data()->Run();
    }

    BlitzenCore::MemoryManagementShutdown();
}
