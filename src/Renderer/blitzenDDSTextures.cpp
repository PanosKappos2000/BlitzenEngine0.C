#include "blitDDSTextures.h"
#include "Core/blitzenContainerLibrary.h"
#include "Platform/filesystem.h"

#include "Renderer/blitRenderer.h"
#include "BlitzenVulkan/vulkanRenderer.h"

namespace BlitzenEngine
{
    uint8_t LoadDDSImage(const char* filepath, DDS_HEADER& header, DDS_HEADER_DXT10& header10, 
	unsigned int& vulkanImageFormat, RendererToLoadDDS chosenRenderer, void* pData)
    {
		BlitzenPlatform::FileHandle handle;
		if(!handle.Open(filepath, BlitzenPlatform::FileModes::Read, 1))
			return 0;

		FILE* file = reinterpret_cast<FILE*>(handle.pHandle);

	    unsigned int magic = 0;

	    if (fread(&magic, sizeof(magic), 1, file) != 1 || magic != FourCC("DDS "))
		    return 0;

	    if (fread(&header, sizeof(header), 1, file) != 1)
		    return 0;

	    if (header.ddspf.dwFourCC == FourCC("DX10") && fread(&header10, sizeof(header10), 1, file) != 1)
		    return 0;

	    if (header.dwSize != sizeof(header) || header.ddspf.dwSize != sizeof(header.ddspf))
		    return 0;

	    if (header.dwCaps2 & (DDSCAPS2_CUBEMAP | DDSCAPS2_VOLUME))
		    return 0;

	    if (header.ddspf.dwFourCC == FourCC("DX10") && header10.resourceDimension != DDS_DIMENSION_TEXTURE2D)
		    return 0;

		switch (chosenRenderer)
		{
			case RendererToLoadDDS::Vulkan:
			{
				vulkanImageFormat = (unsigned int)BlitzenVulkan::GetDDSVulkanFormat(header, header10);

				if (vulkanImageFormat == VK_FORMAT_UNDEFINED)
					return 0;

				unsigned int blockSize =
					(vulkanImageFormat == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || vulkanImageFormat == VK_FORMAT_BC4_SNORM_BLOCK
						|| vulkanImageFormat == VK_FORMAT_BC4_UNORM_BLOCK) ? 8 : 16;
				size_t imageSize = GetDDSImageSizeBC(header.dwWidth, header.dwHeight, header.dwMipMapCount, blockSize);

				size_t readSize = fread(pData, 1, imageSize, file);

				if (!pData)
					return 0;

				if (readSize != imageSize)
					return 0;

				return 1;
			}

			case RendererToLoadDDS::Opengl:
			{
				size_t blockSize = GetDDSBlockSize(header, header10);
				size_t imageSize = GetDDSImageSizeBC(header.dwWidth, header.dwHeight, header.dwMipMapCount, 
				static_cast<unsigned int>(blockSize));

				size_t readSize = fread(pData, 1, imageSize, file);

				if (!pData)
					return 0;

				if (readSize != imageSize)
					return 0;

				return 1;
			}

			default:
				return 0;
		}
    }

    size_t GetDDSImageSizeBC(unsigned int width, unsigned int height, unsigned int levels, unsigned int blockSize)
    {
	    size_t result = 0;
	    for (unsigned int i = 0; i < levels; ++i)
	    {
		    result += ((width + 3) / 4) * ((height + 3) / 4) * blockSize;
		    width = width > 1 ? width / 2 : 1;
		    height = height > 1 ? height / 2 : 1;
	    }
	    return result;
    }

	size_t GetDDSBlockSize(DDS_HEADER& header, DDS_HEADER_DXT10& header10)
	{
		if (header.ddspf.dwFourCC == BlitzenEngine::FourCC("DXT1"))
	        return 8;
	    if (header.ddspf.dwFourCC == BlitzenEngine::FourCC("DXT3"))
	        return 16;
	    if (header.ddspf.dwFourCC == BlitzenEngine::FourCC("DXT5"))
	        return 16;

        if (header.ddspf.dwFourCC == BlitzenEngine::FourCC("DX10"))
	    {
	    	switch (header10.dxgiFormat)
	    	{
	    	    case BlitzenEngine::DXGI_FORMAT_BC1_UNORM:
	    	    case BlitzenEngine::DXGI_FORMAT_BC1_UNORM_SRGB:
				case BlitzenEngine::DXGI_FORMAT_BC4_UNORM:
				case BlitzenEngine::DXGI_FORMAT_BC4_SNORM:
	    	    	return 8;

	    	    case BlitzenEngine::DXGI_FORMAT_BC2_UNORM:
	    	    case BlitzenEngine::DXGI_FORMAT_BC2_UNORM_SRGB:
	    	    case BlitzenEngine::DXGI_FORMAT_BC3_UNORM:
	    	    case BlitzenEngine::DXGI_FORMAT_BC3_UNORM_SRGB:
	    	    case BlitzenEngine::DXGI_FORMAT_BC5_UNORM:
	    	    case BlitzenEngine::DXGI_FORMAT_BC5_SNORM:
	    	    case BlitzenEngine::DXGI_FORMAT_BC6H_UF16:
	    	    case BlitzenEngine::DXGI_FORMAT_BC6H_SF16:
	    	    case BlitzenEngine::DXGI_FORMAT_BC7_UNORM:
	    	    case BlitzenEngine::DXGI_FORMAT_BC7_UNORM_SRGB:
	    	    	return 16;
	    	}
	    }
        
	    return VK_FORMAT_UNDEFINED;
	}
}