/*
MIT License
Copyright (c) 2019 AnastaZIuk
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "CImageLoaderOpenEXR.h"

#ifdef _IRR_COMPILE_WITH_OPENEXR_LOADER_

#include "irr/asset/COpenEXRImageMetadata.h"
#include "openexr/IlmBase/Imath/ImathBox.h"
#include "openexr/OpenEXR/IlmImf/ImfRgbaFile.h"
#include "openexr/OpenEXR/IlmImf/ImfInputFile.h"
#include "openexr/OpenEXR/IlmImf/ImfChannelList.h"
#include "openexr/OpenEXR/IlmImf/ImfChannelListAttribute.h"
#include "openexr/OpenEXR/IlmImf/ImfStringAttribute.h"
#include "openexr/OpenEXR/IlmImf/ImfMatrixAttribute.h"
#include "openexr/OpenEXR/IlmImf/ImfArray.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>

#include "openexr/OpenEXR/IlmImf/ImfNamespace.h"
namespace IMF = Imf;
namespace IMATH = Imath;

namespace irr
{
	namespace asset
	{
		using namespace IMF;
		using namespace IMATH;

		using suffixOfChannelBundle = std::string;
		using channelName = std::string;	     									// sytnax if as follows
		using mapOfChannels = std::unordered_map<channelName, Channel>;				// suffix.channel, where channel are "R", "G", "B", "A"

		class SContext;
		bool readVersionField(io::IReadFile* _file, SContext& ctx);
		bool readHeader(const char fileName[], SContext& ctx);
		template<typename rgbaFormat>
		void readRgba(InputFile& file, std::array<Array2D<rgbaFormat>, 4>& pixelRgbaMapArray, int& width, int& height, E_FORMAT& format, const suffixOfChannelBundle suffixOfChannels);
		E_FORMAT specifyIrrlichtEndFormat(const mapOfChannels& mapOfChannels, const suffixOfChannelBundle suffixName, const std::string fileName);

		//! A helpful struct for handling OpenEXR layout
		/*
			The latest OpenEXR file consists of the following components:
			- magic number
			- version field
			- header
			- line offset table
			- scan line blocks
		*/
		struct SContext
		{
			constexpr static uint32_t magicNumber = 20000630ul; // 0x76, 0x2f, 0x31, 0x01

			struct VersionField
			{
				uint32_t mainDataRegisterField = 0ul;      // treated as 2 seperate bit fields, contains some usefull data
				uint8_t fileFormatVersionNumber = 0;	   // contains current OpenEXR version. It has to be 0 upon initialization!
				bool doesFileContainLongNames;			   // if set, the maximum length of attribute names, attribute type names and channel names is 255 bytes. Otherwise 31 bytes
				bool doesItSupportDeepData;		           // if set, there is at least one part which is not a regular scan line image or regular tiled image, so it is a deep format

				struct Compoment
				{
					enum CompomentType
					{
						SINGLE_PART_FILE,
						MULTI_PART_FILE
					};

					enum SinglePartFileCompoments
					{
						NONE,
						SCAN_LINES,
						TILES,
						SCAN_LINES_OR_TILES
					};

					CompomentType type;
					SinglePartFileCompoments singlePartFileCompomentSubTypes;

				} Compoment;

			} versionField;

			struct Attributes
			{
				// The header of every OpenEXR file must contain at least the following attributes
				//according to https://www.openexr.com/documentation/openexrfilelayout.pdf (page 8)
				const Channel* channels = nullptr;
				const Compression* compression = nullptr;
				const Box2i* dataWindow = nullptr;
				const Box2i* displayWindow = nullptr;
				const LineOrder* lineOrder = nullptr;
				const float* pixelAspectRatio = nullptr;
				const V2f* screenWindowCenter = nullptr;
				const float* screenWindowWidth = nullptr;

				// These attributes are required in the header for all multi - part and /or deep data OpenEXR files
				const std::string* name = nullptr;
				const std::string* type = nullptr;
				const int* version = nullptr;
				const int* chunkCount = nullptr;

				// This attribute is required in the header for all files which contain deep data (deepscanline or deeptile)
				const int* maxSamplesPerPixel = nullptr;

				// This attribute is required in the header for all files which contain one or more tiles
				const TileDescription* tiles = nullptr;

				// This attribute can be used in the header for multi-part files
				const std::string* view = nullptr;

				// Others not required that can be used by metadata
				// - none at the moment

			} attributes;

			// core::smart_refctd_dynamic_array<uint32_t> offsetTable; 

			// scan line blocks
		};

		constexpr uint8_t availableChannels = 4;
		struct PerImageData
		{
			ICPUImage::SCreationParams params;
			std::array<Array2D<half>, availableChannels> halfPixelMapArray;
			std::array<Array2D<float>, availableChannels> fullFloatPixelMapArray;
			std::array<Array2D<uint32_t>, availableChannels> uint32_tPixelMapArray;
		};

		auto getChannels(const InputFile& file)
		{
			std::unordered_map<suffixOfChannelBundle, mapOfChannels> irrChannels;		    // example: G, albedo.R, color.space.B
			{
				auto channels = file.header().channels();
				for (auto mapItr = channels.begin(); mapItr != channels.end(); ++mapItr)
				{
					std::string fetchedChannelName = mapItr.name();
					const bool isThereAnySuffix = fetchedChannelName.size() > 1;

					if (isThereAnySuffix)
					{
						const auto endPositionOfChannelName = fetchedChannelName.find_last_of(".");
						auto suffix = fetchedChannelName.substr(0, endPositionOfChannelName);
						auto channel = fetchedChannelName.substr(endPositionOfChannelName + 1);
						if (channel == "R" || channel == "G" || channel == "B" || channel == "A")
							(irrChannels[suffix])[channel] = mapItr.channel();
					}
					else
						(irrChannels[""])[fetchedChannelName] = mapItr.channel();
				}
			}

			return irrChannels;
		}

		auto doesTheChannelExist(const std::string channelName, const mapOfChannels& mapOfChannels)
		{
			auto foundPosition = mapOfChannels.find(channelName);
			if (foundPosition != mapOfChannels.end())
				return true;
			else
				return false;
		}

		template<typename IlmF>
		void trackIrrFormatAndPerformDataAssignment(void* begginingOfImageDataBuffer, const uint64_t& endShiftToSpecifyADataPos, const IlmF& ilmPixelValueToAssignTo)
		{
			*(reinterpret_cast<IlmF*>(begginingOfImageDataBuffer) + endShiftToSpecifyADataPos) = ilmPixelValueToAssignTo;
		}

		asset::SAssetBundle CImageLoaderOpenEXR::loadAsset(io::IReadFile* _file, const asset::IAssetLoader::SAssetLoadParams& _params, asset::IAssetLoader::IAssetLoaderOverride* _override, uint32_t _hierarchyLevel)
		{
			if (!_file)
				return {};

			const auto& fileName = _file->getFileName().c_str();

			SContext ctx;
			InputFile file = fileName;

			if (!readVersionField(_file, ctx))
				return {};

			if (!readHeader(fileName, ctx))
				return {};

			core::vector<core::smart_refctd_ptr<ICPUImage>> images;
			const auto channelsData = getChannels(file);
			{
				for (const auto& data : channelsData)
				{
					const auto suffixOfChannels = data.first;
					const auto mapOfChannels = data.second;
					PerImageData perImageData;

					core::smart_refctd_dynamic_array<IImageMetadata::ImageInputSemantic> imageInputsMetadata = core::make_refctd_dynamic_array<decltype(imageInputsMetadata)>(1);
					auto input = imageInputsMetadata->begin();
					input->imageName = suffixOfChannels;
					input->colorSpace = ECS_SRGB_NONLINEAR_KHR;
					input->transferFunction.eotf = IImageMetadata::EOTF_sRGB;
					input->transferFunction.oetf = IImageMetadata::OETF_sRGB;

					int width;
					int height;

					auto params = perImageData.params;
					params.format = specifyIrrlichtEndFormat(mapOfChannels, suffixOfChannels, file.fileName());
					params.type = ICPUImage::ET_2D;;
					params.flags = static_cast<ICPUImage::E_CREATE_FLAGS>(0u);
					params.samples = ICPUImage::ESCF_1_BIT;
					params.extent.depth = 1u;
					params.mipLevels = 1u;
					params.arrayLayers = 1u;

					if (params.format == EF_UNKNOWN)
					{
						os::Printer::log("LOAD EXR: incorrect format specified for " + suffixOfChannels + " channels - returning empty or filled so far images bundle", file.fileName(), ELL_INFORMATION);
						return images;
					}

					if (params.format == EF_R16G16B16A16_SFLOAT)
						readRgba(file, perImageData.halfPixelMapArray, width, height, params.format, suffixOfChannels);
					else if (params.format == EF_R32G32B32A32_SFLOAT)
						readRgba(file, perImageData.fullFloatPixelMapArray, width, height, params.format, suffixOfChannels);
					else if (params.format == EF_R32G32B32A32_UINT)
						readRgba(file, perImageData.uint32_tPixelMapArray, width, height, params.format, suffixOfChannels);

					params.extent.width = width;
					params.extent.height = height;

					auto image = ICPUImage::create(std::move(params));

					const uint32_t texelFormatByteSize = getTexelOrBlockBytesize(image->getCreationParameters().format);
					auto texelBuffer = core::make_smart_refctd_ptr<ICPUBuffer>(image->getImageDataSizeInBytes());
					auto regions = core::make_refctd_dynamic_array<core::smart_refctd_dynamic_array<ICPUImage::SBufferCopy>>(1u);
					ICPUImage::SBufferCopy& region = regions->front();
					//region.imageSubresource.aspectMask = ...; // waits for Vulkan
					region.imageSubresource.mipLevel = 0u;
					region.imageSubresource.baseArrayLayer = 0u;
					region.imageSubresource.layerCount = 1u;
					region.bufferOffset = 0u;
					region.bufferRowLength = calcPitchInBlocks(width, texelFormatByteSize);
					region.bufferImageHeight = 0u;
					region.imageOffset = { 0u, 0u, 0u };
					region.imageExtent = image->getCreationParameters().extent;

					void* fetchedData = texelBuffer->getPointer();
					const auto pitch = region.bufferRowLength;

					for (uint64_t yPos = 0; yPos < height; ++yPos)
						for (uint64_t xPos = 0; xPos < width; ++xPos)
						{
							const uint64_t ptrStyleEndShiftToImageDataPixel = (yPos * pitch * availableChannels) + (xPos * availableChannels);

							for (uint8_t channelIndex = 0; channelIndex < availableChannels; ++channelIndex)
							{
								const auto& halfChannelElement = (perImageData.halfPixelMapArray[channelIndex])[yPos][xPos];
								const auto& fullFloatChannelElement = (perImageData.fullFloatPixelMapArray[channelIndex])[yPos][xPos];
								const auto& uint32_tChannelElement = (perImageData.uint32_tPixelMapArray[channelIndex])[yPos][xPos];

								if (params.format == EF_R16G16B16A16_SFLOAT)
									trackIrrFormatAndPerformDataAssignment<half>(fetchedData, ptrStyleEndShiftToImageDataPixel + channelIndex, halfChannelElement);
								else if (params.format == EF_R32G32B32A32_SFLOAT)
									trackIrrFormatAndPerformDataAssignment<float>(fetchedData, ptrStyleEndShiftToImageDataPixel + channelIndex, fullFloatChannelElement);
								else if (params.format == EF_R32G32B32A32_UINT)
									trackIrrFormatAndPerformDataAssignment<uint32_t>(fetchedData, ptrStyleEndShiftToImageDataPixel + channelIndex, uint32_tChannelElement);
							}
						}

					image->setBufferAndRegions(std::move(texelBuffer), regions);
					m_manager->setAssetMetadata(image.get(), core::make_smart_refctd_ptr<COpenEXRImageMetadata>(std::move(imageInputsMetadata)));

					images.push_back(std::move(image));
				}
			}	

			return images;
		}

		bool CImageLoaderOpenEXR::isALoadableFileFormat(io::IReadFile* _file) const
		{	
			const size_t begginingOfFile = _file->getPos();
            _file->seek(0ull);

			char magicNumberBuffer[sizeof(SContext::magicNumber)];
			_file->read(magicNumberBuffer, sizeof(SContext::magicNumber));
			_file->seek(begginingOfFile);

			return isImfMagic(magicNumberBuffer);
		}

		template<typename rgbaFormat>
		void readRgba(InputFile& file, std::array<Array2D<rgbaFormat>, 4>& pixelRgbaMapArray, int& width, int& height, E_FORMAT& format, const suffixOfChannelBundle suffixOfChannels)
		{
			Box2i dw = file.header().dataWindow();
			width = dw.max.x - dw.min.x + 1;
			height = dw.max.y - dw.min.y + 1;

			constexpr const char* rgbaSignatureAsText[] = {"R", "G", "B", "A"};
			for (auto& pixelChannelBuffer : pixelRgbaMapArray)
				pixelChannelBuffer.resizeErase(height, width);

			FrameBuffer frameBuffer;
			PixelType pixelType;

			if (format == EF_R16G16B16A16_SFLOAT)
				pixelType = PixelType::HALF;
			else if (format == EF_R32G32B32A32_SFLOAT)
				pixelType = PixelType::FLOAT;
			else if (format == EF_R32G32B32A32_UINT)
				pixelType = PixelType::UINT;

			for (uint8_t rgbaChannelIndex = 0; rgbaChannelIndex < availableChannels; ++rgbaChannelIndex)
			{
				std::string name = suffixOfChannels + "." + rgbaSignatureAsText[rgbaChannelIndex];
				frameBuffer.insert
				(
					name.c_str(),																					// name
					Slice(pixelType,																				// type
					(char*)(&(pixelRgbaMapArray[rgbaChannelIndex])[0][0] - dw.min.x - dw.min.y * width),			// base
						sizeof((pixelRgbaMapArray[rgbaChannelIndex])[0][0]) * 1,                                    // xStride
						sizeof((pixelRgbaMapArray[rgbaChannelIndex])[0][0]) * width,                                // yStride
						1, 1,                                                                                       // x/y sampling
						rgbaChannelIndex == 3 ? 1 : 0                                                               // default fillValue for channels that aren't present in file - 1 for alpha, otherwise 0
					));
			}

			file.setFrameBuffer(frameBuffer);
			file.readPixels(dw.min.y, dw.max.y);
		}

		E_FORMAT specifyIrrlichtEndFormat(const mapOfChannels& mapOfChannels, const suffixOfChannelBundle suffixName, const std::string fileName)
		{
			E_FORMAT retVal;

			const auto rChannel = doesTheChannelExist("R", mapOfChannels);
			const auto gChannel = doesTheChannelExist("G", mapOfChannels);
			const auto bChannel = doesTheChannelExist("B", mapOfChannels);
			const auto aChannel = doesTheChannelExist("A", mapOfChannels);

			if (rChannel && gChannel && bChannel && aChannel)
				os::Printer::log("LOAD EXR: loading " + suffixName + " RGBA file", fileName, ELL_INFORMATION);
			else if (rChannel && gChannel && bChannel)
				os::Printer::log("LOAD EXR: loading " + suffixName + " RGB file", fileName, ELL_INFORMATION);
			else if(rChannel && gChannel)
				os::Printer::log("LOAD EXR: loading " + suffixName + " RG file", fileName, ELL_INFORMATION);
			else if(rChannel)
				os::Printer::log("LOAD EXR: loading " + suffixName + " R file", fileName, ELL_INFORMATION);
			else 
				os::Printer::log("LOAD EXR: the file's channels are invalid to load", fileName, ELL_ERROR);

			auto doesMapOfChannelsFormatHaveTheSameFormatLikePassedToIt = [&](const PixelType ImfTypeToCompare)
			{
				for (auto& channel : mapOfChannels)
					if (channel.second.type != ImfTypeToCompare)
						return false;
				return true;
			};

			if (doesMapOfChannelsFormatHaveTheSameFormatLikePassedToIt(PixelType::HALF))
				retVal = EF_R16G16B16A16_SFLOAT;
			else if (doesMapOfChannelsFormatHaveTheSameFormatLikePassedToIt(PixelType::FLOAT))
				retVal = EF_R32G32B32A32_SFLOAT;
			else if (doesMapOfChannelsFormatHaveTheSameFormatLikePassedToIt(PixelType::UINT))
				retVal = EF_R32G32B32A32_UINT;
			else
				return EF_UNKNOWN;

			return retVal;
		}

		bool readVersionField(io::IReadFile* _file, SContext& ctx)
		{
			RgbaInputFile file(_file->getFileName().c_str());
			auto& versionField = ctx.versionField;
			
			versionField.mainDataRegisterField = file.version();

			auto isTheBitActive = [&](uint16_t bitToCheck)
			{
				return (versionField.mainDataRegisterField & (1 << bitToCheck - 1));
			};

			versionField.fileFormatVersionNumber |= isTheBitActive(1) | isTheBitActive(2) | isTheBitActive(3) | isTheBitActive(4) | isTheBitActive(5) | isTheBitActive(6) | isTheBitActive(7) | isTheBitActive(8);

			if (!isTheBitActive(11) && !isTheBitActive(12))
			{
				versionField.Compoment.type = SContext::VersionField::Compoment::SINGLE_PART_FILE;

				if (isTheBitActive(9))
				{
					versionField.Compoment.singlePartFileCompomentSubTypes = SContext::VersionField::Compoment::TILES;
					os::Printer::log("LOAD EXR: the file consist of not supported tiles", file.fileName(), ELL_ERROR);
					return false;
				}
				else
					versionField.Compoment.singlePartFileCompomentSubTypes = SContext::VersionField::Compoment::SCAN_LINES;
			}
			else if (!isTheBitActive(9) && !isTheBitActive(11) && isTheBitActive(12))
			{
				versionField.Compoment.type = SContext::VersionField::Compoment::MULTI_PART_FILE;
				versionField.Compoment.singlePartFileCompomentSubTypes = SContext::VersionField::Compoment::SCAN_LINES_OR_TILES;
				os::Printer::log("LOAD EXR: the file is a not supported multi part file", file.fileName(), ELL_ERROR);
				return false;
			}

			if (!isTheBitActive(9) && isTheBitActive(11) && isTheBitActive(12))
			{
				versionField.doesItSupportDeepData = true;
				os::Printer::log("LOAD EXR: the file consist of not supported deep data", file.fileName(), ELL_ERROR);
				return false;
			}
			else
				versionField.doesItSupportDeepData = false;

			if (isTheBitActive(10))
				versionField.doesFileContainLongNames = true;
			else
				versionField.doesFileContainLongNames = false;

			return true;
		}

		bool readHeader(const char fileName[], SContext& ctx)
		{
			RgbaInputFile file(fileName);
			auto& attribs = ctx.attributes;
			auto& versionField = ctx.versionField;

			/*

			// There is an OpenEXR library implementation error associated with dynamic_cast<> probably
			// Since OpenEXR loader only cares about RGB and RGBA, there is no need for bellow at the moment

			attribs.channels = file.header().findTypedAttribute<Channel>("channels");
			attribs.compression = file.header().findTypedAttribute<Compression>("compression");
			attribs.dataWindow = file.header().findTypedAttribute<Box2i>("dataWindow");
			attribs.displayWindow = file.header().findTypedAttribute<Box2i>("displayWindow");
			attribs.lineOrder = file.header().findTypedAttribute<LineOrder>("lineOrder");
			attribs.pixelAspectRatio = file.header().findTypedAttribute<float>("pixelAspectRatio");
			attribs.screenWindowCenter = file.header().findTypedAttribute<V2f>("screenWindowCenter");
			attribs.screenWindowWidth = file.header().findTypedAttribute<float>("screenWindowWidth");

			if (versionField.Compoment.singlePartFileCompomentSubTypes == SContext::VersionField::Compoment::TILES)
				attribs.tiles = file.header().findTypedAttribute<TileDescription>("tiles");

			if (versionField.Compoment.type == SContext::VersionField::Compoment::MULTI_PART_FILE)
				attribs.view = file.header().findTypedAttribute<std::string>("view");

			if (versionField.Compoment.type == SContext::VersionField::Compoment::MULTI_PART_FILE || versionField.doesItSupportDeepData)
			{
				attribs.name = file.header().findTypedAttribute<std::string>("name");
				attribs.type = file.header().findTypedAttribute<std::string>("type");
				attribs.version = file.header().findTypedAttribute<int>("version");
				attribs.chunkCount = file.header().findTypedAttribute<int>("chunkCount");
				attribs.maxSamplesPerPixel = file.header().findTypedAttribute<int>("maxSamplesPerPixel");
			}

			*/

			return true;
		}
	}
}

#endif // _IRR_COMPILE_WITH_OPENEXR_LOADER_