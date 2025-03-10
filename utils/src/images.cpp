/*
Copyright 2023 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <fileformatutils/common.h>
#include <fileformatutils/debugCodes.h>
#include <fileformatutils/images.h>
#include <filesystem>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/usd/ar/defaultResolver.h>

using namespace PXR_NS;

namespace adobe::usd {

Image::Image()
  : width(0U)
  , height(0U)
  , channels(0U)
{
}

Image::~Image() {}

bool
Image::allocate(int width, int height, int channels)
{
    this->width = width;
    this->height = height;
    this->channels = channels;
    pixels.resize(width * height * channels);
    return !pixels.empty();
}

bool
Image::read(const ImageAsset& imageAsset, int forceChannels)
{
    std::string extension = getFormatExtension(imageAsset.format);
    if (extension.empty()) {
        return false;
    }

    OIIO::Filesystem::IOMemReader memreader(
      const_cast<void*>(reinterpret_cast<const void*>(imageAsset.image.data())),
      imageAsset.image.size());
    void* ptr = &memreader;

    OIIO::ImageSpec config;

    // Set attribute to allow reading from memory
    config.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &ptr);

    // Set attribute to avoid conversion to pre-multiplied alpha when reading
    config.attribute("oiio:UnassociatedAlpha", 1);

    std::string filename = "dummy." + extension;
    std::unique_ptr<OIIO::ImageInput> input = OIIO::ImageInput::open(filename, &config);
    if (!input) {
        TF_WARN("Image::read() OpenImageIO failed to open ImageInput with URI=%s: %s\n",
                imageAsset.uri.c_str(),
                OIIO::geterror().c_str());
        return false;
    }
    const OIIO::ImageSpec& spec = input->spec();
    width = spec.width;
    height = spec.height;
    channels = spec.nchannels;
    if (forceChannels > 0) {
        // note we force to forceChannels, instead of the true spec.nchannels
        channels = forceChannels;
    }
    pixels.resize(width * height * channels);
    input->read_image(0, 0, 0, channels, OIIO::TypeDesc::FLOAT, pixels.data());
    input->close();
    return true;
}

bool
Image::write(ImageAsset& imageAsset) const
{
    // Check for invalid image dimensions or channels
    if (width < 1 || height < 1 || channels < 1) {
        TF_WARN("Trying to write invalid Image to ImageAsset %s with dimensions: "
                "width=%d, height=%d, channels=%d",
                imageAsset.uri.c_str(),
                width,
                height,
                channels);
        return false;
    }
    if (imageAsset.format == ImageFormatUnknown) {
        TF_CODING_ERROR("Trying to write Image to ImageAsset %s with unknown format",
                        imageAsset.uri.c_str());
        return false;
    }
    std::string extension = getFormatExtension(imageAsset.format);
    if (extension.empty()) {
        TF_CODING_ERROR("Trying to write Image to ImageAsset %s with empty extension",
                        imageAsset.uri.c_str());
        return false;
    }

    OIIO::ImageSpec spec(width, height, channels, OIIO::TypeDesc::FLOAT);
    // XXX this is needed for PNG images to have correct alpha that is independent of the RGB
    // channels. This is important when packing channels into an image file, like color and opacity.
    // This allows having color pixels, but an opacity of zero.
    spec.attribute("oiio:UnassociatedAlpha", 1);
    std::string dummyFilename = "dummy." + extension;
    OIIO::Filesystem::IOVecOutput memoryWriter(imageAsset.image); // I/O proxy object
    void* ptr = &memoryWriter;
    spec.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &ptr);

    std::unique_ptr<OIIO::ImageOutput> out;
    try {
        out = OIIO::ImageOutput::create(dummyFilename);
        if (!out) {
            TF_WARN("Failed to create ImageOutput for %s", dummyFilename.c_str());
            return false;
        }
        if (!out->open(dummyFilename, spec)) {
            TF_WARN("Failed to open ImageOutput for %s with the provided spec",
                    dummyFilename.c_str());
            return false;
        }

    } catch (const std::exception& e) {
        TF_WARN("Exception occurred: %s", e.what());
        return false;
    } catch (...) {
        TF_WARN("Unknown exception occurred during image opening");
        return false;
    }

    if (!out->write_image(OIIO::TypeDesc::FLOAT, pixels.data())) {
        TF_WARN("Failed to write image data to %s", dummyFilename.c_str());
        return false;
    }
    out->close();
    return true;
}

bool
Image::convertImageToPng(const ImageAsset& srcImageAsset, ImageAsset& dstImageAsset)
{
    if (srcImageAsset.format == ImageFormatUnknown) {
        TF_CODING_ERROR("Trying to write Image to ImageAsset %s with unknown format",
                        srcImageAsset.uri.c_str());
        return false;
    }

    {
        // If the extension is known, we can't handle the format
        std::string extension = getFormatExtension(srcImageAsset.format);
        if (extension.empty()) {
            return false;
        }
    }

    // input I/O proxy object
    OIIO::Filesystem::IOMemReader memreader(
      const_cast<void*>(reinterpret_cast<const void*>(srcImageAsset.image.data())),
      srcImageAsset.image.size());

    OIIO::ImageSpec config;
    void* ptr = &memreader;
    config.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &ptr);

    auto in = OIIO::ImageInput::open(srcImageAsset.uri, &config);
    if (!in) {
        return false;
    }

    OIIO::ImageSpec inspec = in->spec();
    std::string inputColorSpace = inspec.get_string_attribute("oiio:ColorSpace", "");

    std::vector<uint8_t> filebuffer;                  // output bytes will go here
    OIIO::Filesystem::IOVecOutput vecout(filebuffer); // output I/O proxy object

    std::string outname = srcImageAsset.name + ".png";
    auto out = OIIO::ImageOutput::create(outname);

    if (!out || !out->supports("ioproxy")) {
        return false;
    }
    // Set the color space to sRGB for the output
    OIIO::ImageSpec outspec = inspec;
    outspec.attribute("oiio:ColorSpace", "sRGB");
    ptr = &vecout;
    outspec.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &ptr);

    bool ok = out->open(outname, outspec);
    if (ok) {
        if (inputColorSpace == "sRGB") {
            ok = out->copy_image(in.get());
        } else {
            memreader.seek(0);
            OIIO::ImageBuf srcBuf(srcImageAsset.uri, 0, 0, nullptr, &config);
            OIIO::ImageBuf dstBuf;
            ok = OIIO::ImageBufAlgo::colorconvert(dstBuf, srcBuf, inputColorSpace, "sRGB");
            if (ok) {
                ok = dstBuf.write(out.get());
            }
        }
    }
    // close output before using filebuffer as extra bytes will be written on close
    in->close();
    out->close();

    if (ok) {
        dstImageAsset.name = srcImageAsset.name;
        dstImageAsset.uri = outname;
        dstImageAsset.format = ImageFormatPng;
        dstImageAsset.image = std::move(filebuffer);
    }

    return ok;
}

bool
Image::copyChannel(const Image& imageSrc, int channelSrc, int channelDst)
{
    return transformChannel(imageSrc, channelSrc, 1.0f, 0.0f, channelDst);
}

bool
Image::transformChannel(const Image& imageSrc,
                        int channelSrc,
                        float scale,
                        float bias,
                        int channelDst)
{
    if (width != imageSrc.width || height != imageSrc.height || channelSrc >= imageSrc.channels ||
        channelDst >= channels)
        return false;
    const uint32_t pixelCount = width * height;
    const float* src = imageSrc.pixels.data();
    const int numSrcChannels = imageSrc.channels;
    float* dst = pixels.data();
    const int numDstChannels = channels;
    // If the scale and bias are default, just copy source channel to dest channel
    if (scale == 1.0f && bias == 0.0f) {
        for (uint32_t i = 0; i < pixelCount; i++) {
            dst[i * numDstChannels + channelDst] = src[i * numSrcChannels + channelSrc];
        }
    } else {
        // Apply scale and bias to source channel and store in dest channel
        for (uint32_t i = 0; i < pixelCount; i++) {
            dst[i * numDstChannels + channelDst] =
              src[i * numSrcChannels + channelSrc] * scale + bias;
        }
    }
    return true;
}

void
Image::set(float r, float g, float b, float a)
{
    int pixelCount = width * height;
    float* dst = pixels.data();
    switch (channels) {
        case 1:
            for (int i = 0; i < pixelCount; i++) {
                dst[i] = r;
            }
            break;
        case 2:
            for (int i = 0; i < pixelCount; i++) {
                dst[2 * i] = r;
                dst[2 * i + 1] = g;
            }
            break;
        case 3:
            for (int i = 0; i < pixelCount; i++) {
                dst[3 * i] = r;
                dst[3 * i + 1] = g;
                dst[3 * i + 2] = b;
            }
            break;
        case 4:
            for (int i = 0; i < pixelCount; i++) {
                dst[4 * i] = r;
                dst[4 * i + 1] = g;
                dst[4 * i + 2] = b;
                dst[4 * i + 3] = a;
            }
            break;
    }
}

std::pair<GfVec4f, GfVec4f>
Image::computeRange() const
{
    float minr = FLT_MAX;
    float ming = FLT_MAX;
    float minb = FLT_MAX;
    float mina = FLT_MAX;
    float maxr = -FLT_MAX;
    float maxg = -FLT_MAX;
    float maxb = -FLT_MAX;
    float maxa = -FLT_MAX;

    int pixelCount = width * height;
    const float* src = pixels.data();
    switch (channels) {
        case 1:
            for (int i = 0; i < pixelCount; i++) {
                float r = src[i];
                minr = std::min(r, minr);
                maxr = std::max(r, maxr);
            }
            break;
        case 2:
            for (int i = 0; i < pixelCount; i++) {
                float r = src[2 * i + 0];
                float g = src[2 * i + 1];
                minr = std::min(r, minr);
                maxr = std::max(r, maxr);
                ming = std::min(g, ming);
                maxg = std::max(g, maxg);
            }
            break;
        case 3:
            for (int i = 0; i < pixelCount; i++) {
                float r = src[3 * i + 0];
                float g = src[3 * i + 1];
                float b = src[3 * i + 2];
                minr = std::min(r, minr);
                maxr = std::max(r, maxr);
                ming = std::min(g, ming);
                maxg = std::max(g, maxg);
                minb = std::min(b, minb);
                maxb = std::max(b, maxb);
            }
            break;
        case 4:
            for (int i = 0; i < pixelCount; i++) {
                float r = src[4 * i + 0];
                float g = src[4 * i + 1];
                float b = src[4 * i + 2];
                float a = src[4 * i + 3];
                minr = std::min(r, minr);
                maxr = std::max(r, maxr);
                ming = std::min(g, ming);
                maxg = std::max(g, maxg);
                minb = std::min(b, minb);
                maxb = std::max(b, maxb);
                mina = std::min(a, mina);
                maxa = std::max(a, maxa);
            }
            break;
    }

    return { GfVec4f(minr, ming, minb, mina), GfVec4f(maxr, maxg, maxb, maxa) };
}

bool
imageMult(const Image& in, const Image& factor, Image& out)
{
    out.allocate(in.width, in.height, in.channels);
    if (in.width != factor.width || in.height != factor.height) {
        // If factor is invalid or doesn't match the side of the in image, we just copy
        // in to out
        memcpy(out.pixels.data(), in.pixels.data(), in.width * in.height * in.channels);
        TF_WARN("imageMult: in image size (%d x %d) doesn't match factor size (%d x %d)",
                in.width,
                in.height,
                factor.width,
                factor.height);
        return false;
    }

    unsigned int pixelCount = in.width * in.height;
    const float* factorSrc = factor.pixels.data();
    int factorChannels = factor.channels;
    const float* src = in.pixels.data();
    int srcChannels = in.channels;
    float* dst = out.pixels.data();
    for (unsigned int i = 0; i < pixelCount; i++) {
        float f = factorSrc[i * factorChannels]; // takes value from first channel
        for (int j = 0; j < srcChannels; j++) {
            dst[i * srcChannels + j] = src[i * srcChannels + j] * f;
        }
    }

    return true;
}

bool
imageTransformAffine(const Image& in, float scale, float bias, Image& out)
{
    const int channels = in.channels;
    out.allocate(in.width, in.height, channels);
    size_t valueCount = in.width * in.height * channels;
    const float* src = in.pixels.data();
    float* dst = out.pixels.data();
    for (size_t i = 0; i < valueCount; i++) {
        *dst++ = scale * (*src++) + bias;
    }
    return true;
}

bool
imageExtractChannel(const Image& in, int channelSrc, float scale, float bias, Image& out)
{
    if (channelSrc < 0 || channelSrc >= in.channels) {
        TF_WARN("Invalid channel index (%d) for extraction from source image", channelSrc);
        return false;
    }

    // allocate space for single channel image and copy from source
    out.allocate(in.width, in.height, 1);
    return out.transformChannel(in, channelSrc, scale, bias, 0);
}

void
imageWrite(const adobe::usd::ImageAsset& image, const std::string& filename, bool overwrite)
{
    const std::string parentPath = TfGetPathName(filename);
    TfMakeDirs(parentPath, -1, true);
    std::ifstream ifile(filename);
    if (ifile.good() && !overwrite) {
        TF_WARN("File %s already exists, not overwriting", filename.c_str());
        ifile.close();
        return;
    }
    std::ofstream ofile(filename.c_str(), std::ios::out | std::ios::binary);
    if (!ofile.is_open()) {
        return;
    }
    ofile.write(reinterpret_cast<const char*>(image.image.data()), image.image.size());
    std::filesystem::path absPath = std::filesystem::absolute(filename);
    TF_STATUS("Wrote image to %s", absPath.c_str());
    ofile.close();
}

float
srgbToLinear(float s)
{
    if (s < 0.040448f)
        return s / 12.92f;
    return std::pow((s + 0.055f) / 1.055f, 2.4f);
}

float
linearToSRGB(float s)
{
    if (s < 0.0031308f)
        return s * 12.92f;
    return 1.055f * std::pow(s, (1.0f / 2.4f)) - 0.055f;
}

static std::string
_getAssetFileExtension(const std::string& resolvedAssetPath)
{
    return TfStringToLower(ArGetResolver().GetExtension(resolvedAssetPath));
}

bool
isImageFileSupported(const std::string& resolvedAssetPath)
{
    // Runtime cache for the supported file types. We don't expect the available plugins to change
    // at run-time. The query to the HioImage::IsSupportedImageFile is quite expensive, so we cache
    // it.
    static std::unordered_map<std::string, bool> supportedExtensions;
    // We want this to be multi-threading save, so we protect the cache
    static std::mutex supportedExtensionsMutex;

    std::lock_guard<std::mutex> lock(supportedExtensionsMutex);

    std::string ext = _getAssetFileExtension(resolvedAssetPath);
    auto [it, inserted] = supportedExtensions.emplace(ext, false);
    if (inserted) {
        it->second = HioImage::IsSupportedImageFile("filename." + ext);
        if (!it->second) {
            TF_WARN("Image file with extension '%s' at path '%s' is not supported",
                    ext.c_str(),
                    resolvedAssetPath.c_str());
        }
    }
    return it->second;
}

bool
isUriSbsarImage(const std::string& uri)
{
    size_t pos = uri.find_first_of('?');
    return uri.length() > 1 && pos != std::string::npos;
}

// Takes a SBSAR texture parameterization like
//   usage=ambientOcclusion#preset=Torn#packageHash=b427747e86441362#params={"$outputsize":\[4,4\]}
// and extracts the value for 'usage'
//   --> ambientOcclusion
// Returns an empty string if the parsing fails
std::string
getSbsarUsageFromParameters(const std::string& parametersStr)
{
    auto params = split(parametersStr, '#');
    for (const auto& param : params) {
        auto keyValue = split(param, '=');
        if (keyValue.size() != 2) {
            continue;
        }
        if (keyValue[0] == "usage") {
            return keyValue[1];
        }
    }

    return {};
}

std::string
getSbsarImageExtension(const std::string& resolvedAssetPath)
{
    if (!isImageFileSupported(resolvedAssetPath)) {
        TF_WARN("Asset %s is not a supported image type", resolvedAssetPath.c_str());
        return {};
    }

    HioImageSharedPtr inputImage = HioImage::OpenForReading(resolvedAssetPath);
    if (!inputImage) {
        TF_WARN("Couldn't open image %s for reading", resolvedAssetPath.c_str());
        return {};
    }

    HioFormat hioFormat = inputImage->GetFormat();

    // Floating point images are stored as exr files
    if (hioFormat == HioFormatFloat16 || hioFormat == HioFormatFloat16Vec2 ||
        hioFormat == HioFormatFloat16Vec3 || hioFormat == HioFormatFloat16Vec4 ||
        hioFormat == HioFormatFloat32 || hioFormat == HioFormatFloat32Vec2 ||
        hioFormat == HioFormatFloat32Vec3 || hioFormat == HioFormatFloat32Vec4 ||
        hioFormat == HioFormatDouble64 || hioFormat == HioFormatDouble64Vec2 ||
        hioFormat == HioFormatDouble64Vec3 || hioFormat == HioFormatDouble64Vec4) {
        return std::string("exr");
    }

    // All other images are most likely textures and we default to png
    return std::string("png");
}

// This function extracts a usable file path from an assetPath, which might be a bit funky:
// Examples:
//   Easy: some/path/to/texture.png -> some/path/to/texture.png
//   With?: some/path/to/texture.png?param1=val1#param2=val2 -> some/path/to/texture.png
//   SBSAR:
//     graphs/CardBoard/images?usage=ambientOcclusion#preset=Torn#packageHash=b427747e86441362#params={"$outputsize":\[4,4\]}.png
//       With some effort -> CardBoard_ambientOcclusion.png
std::string
extractFilePathFromAssetPath(const std::string& assetPath)
{
    // If there are no parameters after the file name we just take the whole path
    auto q = assetPath.find_first_of('?');
    if (q == std::string::npos) {
        return assetPath;
    }

    // If the path contains a '?', take the first part as the subpath
    std::string subpath = assetPath.substr(0, q);
    // If this subpath has a file extension then we just take that path
    if (std::filesystem::path(subpath).has_extension()) {
        return subpath;
    }

    // If the subpath does not have a file extension, check if the full asset path has an extension
    // Note, the extension returned here does not have a '.', so we get 'png'
    std::string ext = ArGetResolver().GetExtension(assetPath);
    if (ext.empty()) {
        TF_WARN("Could not find file extension for asset path %s", assetPath.c_str());
    }

    // Extract the parameters after the '?' and before the extension
    // Note, we want to skip the '.' of the extension, which is where the + 1 for the extension
    // length is coming from.
    std::string parameters = assetPath.substr(q + 1, assetPath.size() - (q + 1) - (ext.size() + 1));

    // Check if this is a SBSAR texture parameterization
    // If so, we can do a better job than naming the file `images.png`
    std::string usage = getSbsarUsageFromParameters(parameters);
    if (!usage.empty()) {
        // graphs/CardBoard/images -> CardBoard
        std::string graphName = std::filesystem::path(subpath).parent_path().filename().u8string();
        subpath = graphName + "_" + usage;
    }

    // Append the extension to create a complete path
    subpath = subpath + "." + ext;

    return subpath;
}

bool
transcodeImageAssetToMemory(const std::string& resolvedAssetPath,
                            const std::string& filename,
                            std::vector<uint8_t>& outputPixelData)
{
    if (!isImageFileSupported(resolvedAssetPath)) {
        TF_WARN("Asset %s is not a supported image type", resolvedAssetPath.c_str());
        return false;
    }

    // Define the temporary directory path and create it if it doesn't exist
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "transcoded_images";
    if (!std::filesystem::exists(tempDir)) {
        if (!createDirectory(tempDir)) {
            TF_WARN("Failed to create temporary directory: %s", tempDir.string().c_str());
            return false;
        }
    }

    std::filesystem::path filePath = tempDir / filename;
    if (!isImageFileSupported(filePath.string())) {
        TF_WARN("Output %s is not a supported image type", filePath.c_str());
        return false;
    }

    HioImageSharedPtr inputImage = HioImage::OpenForReading(resolvedAssetPath);
    if (!inputImage) {
        TF_WARN("Couldn't open image %s for reading", resolvedAssetPath.c_str());
        return false;
    }

    HioImage::StorageSpec storage;
    storage.width = inputImage->GetWidth();
    storage.height = inputImage->GetHeight();
    storage.format = inputImage->GetFormat();
    int bytesPerPixel = inputImage->GetBytesPerPixel();
    std::vector<uint8_t> pixelData(storage.width * storage.height * bytesPerPixel);
    storage.data = pixelData.data();

    if (!inputImage->Read(storage)) {
        TF_WARN("Reading of image %s failed", resolvedAssetPath.c_str());
        return false;
    }

    HioImageSharedPtr outputImage = HioImage::OpenForWriting(filePath.string());
    if (!outputImage) {
        TF_WARN("Couldn't open image %s for writing", filePath.string().c_str());
        return false;
    }

    // Unfortunately, with the HioImage API and there is no way to write to memory
    // and have it encoded to PNG or another format.  We could refactor this in the
    // future to use OIIO.
    if (!outputImage->Write(storage)) {
        TF_WARN("Writing of image %s failed", filePath.string().c_str());
        return false;
    }

    // Read the output image file as binary data into outputPixelData
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        TF_WARN("Couldn't open outputImage %s for reading", filePath.string().c_str());
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    outputPixelData.resize(size);
    if (!file.read(reinterpret_cast<char*>(outputPixelData.data()), size)) {
        TF_WARN("Reading of transcoded image %s failed", filePath.string().c_str());
        return false;
    }

    TF_STATUS("Transcoded image: %s -> %s and populated memory buffer",
              resolvedAssetPath.c_str(),
              filePath.string().c_str());
    return true;
}

}
