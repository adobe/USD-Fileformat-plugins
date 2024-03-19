/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#include <assetResolver/sbsarImage.h>
#include <sbsarDebug.h>
#include <sbsarEngine/sbsarInputImageCache.h>
#include <sbsarEngine/sbsarRender.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/ar/inMemoryAsset.h>
#include <substance/framework/framework.h>

using namespace SubstanceAir;
PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sbsar {

bool
applyParameterValue(InputInstanceBase* i, SubstanceIOType type, const JsValue& v)
{
    if (i == nullptr) {
        TF_RUNTIME_ERROR("SbsarRender: Input not existing on input");
        return false;
    }

    switch (type) {
        case Substance_IOType_Float: {
            InputInstanceFloat* f = dynamic_cast<InputInstanceFloat*>(i);
            if (f == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            float r;
            if (!getAsFloat(v, r)) {
                TF_RUNTIME_ERROR("SbsarRender: Type error: can't get value as float");
                return false;
            }
            f->setValue(r);
            break;
        }
        case Substance_IOType_Float2: {
            InputInstanceFloat2* f = dynamic_cast<InputInstanceFloat2*>(i);
            if (f == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            std::vector<double> a;
            getAsDoubleArray(v, a);
            if (a.size() != 2) {
                TF_RUNTIME_ERROR("SbsarRender: Incorrect data size");
                return false;
            }
            f->setValue(Vec2Float(static_cast<float>(a[0]), static_cast<float>(a[1])));
            break;
        }
        case Substance_IOType_Float3: {
            InputInstanceFloat3* f = dynamic_cast<InputInstanceFloat3*>(i);
            if (f == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            std::vector<double> a;
            getAsDoubleArray(v, a);
            if (a.size() != 3) {
                TF_RUNTIME_ERROR("SbsarRender: Incorrect data size");
                return false;
            }
            f->setValue(Vec3Float(
              static_cast<float>(a[0]), static_cast<float>(a[1]), static_cast<float>(a[2])));
            break;
        }
        case Substance_IOType_Float4: {
            InputInstanceFloat4* f = dynamic_cast<InputInstanceFloat4*>(i);
            if (f == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            std::vector<double> a;
            getAsDoubleArray(v, a);
            if (a.size() != 4) {
                TF_RUNTIME_ERROR("SbsarRender: Incorrect data size");
                return false;
            }
            f->setValue(Vec4Float(static_cast<float>(a[0]),
                                  static_cast<float>(a[1]),
                                  static_cast<float>(a[2]),
                                  static_cast<float>(a[3])));
            break;
        }
        case Substance_IOType_Integer: {
            InputInstanceInt* ii = dynamic_cast<InputInstanceInt*>(i);
            if (ii == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            int r;
            if (!getAsInt(v, r)) {
                TF_RUNTIME_ERROR("SbsarRender: Type error: can't get value as int");
                return false;
            }
            ii->setValue(r);
            break;
        }
        case Substance_IOType_Integer2: {
            InputInstanceInt2* ii = dynamic_cast<InputInstanceInt2*>(i);
            if (ii == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            std::vector<int> a;
            getAsIntArray(v, a);
            if (a.size() != 2) {
                TF_RUNTIME_ERROR("SbsarRender: Incorrect data size");
                return false;
            }
            ii->setValue(Vec2Int(a[0], a[1]));
            break;
        }
        case Substance_IOType_Integer3: {
            InputInstanceInt3* ii = dynamic_cast<InputInstanceInt3*>(i);
            if (ii == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            std::vector<int> a;
            getAsIntArray(v, a);
            if (a.size() != 3) {
                TF_RUNTIME_ERROR("SbsarRender: Incorrect data size");
                return false;
            }
            ii->setValue(Vec3Int(a[0], a[1], a[2]));
            break;
        }
        case Substance_IOType_Integer4: {
            InputInstanceInt4* ii = dynamic_cast<InputInstanceInt4*>(i);
            if (ii == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            std::vector<int> a;
            getAsIntArray(v, a);
            if (a.size() != 4) {
                TF_RUNTIME_ERROR("SbsarRender: Incorrect data size");
                return false;
            }
            ii->setValue(Vec4Int(a[0], a[1], a[2], a[3]));
            break;
        }
        case Substance_IOType_String: {
            InputInstanceString* s = dynamic_cast<InputInstanceString*>(i);
            if (s == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            const std::string& r = v.GetString();
            s->setString(r.c_str());
            break;
        }
        case Substance_IOType_Image: {
            InputInstanceImage* img = dynamic_cast<InputInstanceImage*>(i);
            if (img == nullptr) {
                TF_RUNTIME_ERROR("SbsarRender: Inconsistent type for input");
                return false;
            }
            const std::size_t& hash = v.GetUInt64();
            if (hash == 0) {
                img->reset();
            } else {
                InputImage::SPtr image = getImageFromInputImageCache(hash);
                if (image == nullptr) {
                    TF_WARN("SbsarRender: Image not found in cache");
                    img->reset();
                } else
                    img->setImage(image);
            }
            break;
        }
        default:
            TF_RUNTIME_ERROR("SbsarRender: Parameter set for incompatible type");
            return false;
    }
    return true;
}

//! With OpenGL version of the substance engine, the 8 bit output textures are in BGRA
//! and that is not supported by Hydra. So we swap Red and Blue channel of the ouput.
void
patchOutputFormat(const Renderer& renderer, OutputInstance& oi)
{
    int plaform = renderer.getCurrentVersion().platformImplEnum;
    auto rawPrecision = oi.mDesc.mFormat & Substance_PF_MASK_RAWPrecision;
    bool isOglEngine = plaform == Substance_EngineID_ogl3 || plaform == Substance_EngineID_ogl3m1;
    bool is8Bit = rawPrecision == Substance_PF_8I;
    if (isOglEngine && is8Bit) {
        OutputFormat outputFormat;
        outputFormat.perComponent[0].shuffleIndex = 2; // Fill R channel with B value
        outputFormat.perComponent[2].shuffleIndex = 0; // Fill B channel with R value
        oi.overrideFormat(outputFormat);
    }
}

bool
applyPathParameters(const GraphDesc& graph, GraphInstance& instance, const JsValue& parameters)
{
    // Ensure that every input is reset. This is necessary because we only set parameters contains
    // in the json object.
    for (auto i : instance.getInputs())
        i->reset();
    TF_AXIOM(parameters.IsObject());
    const JsObject& o = parameters.GetJsObject();
    for (auto i : graph.mInputs) {
        const auto& a = o.find(i->mIdentifier.c_str());
        if (a != o.end()) {
            bool applyRes = applyParameterValue(instance.findInput(i->mUid), i->mType, a->second);
            if (!applyRes) {
                TF_WARN("SbsarRender: Failed to apply value for %s", i->mIdentifier.c_str());
            }
        }
    }
    return true;
}

static inline SubstanceAir::OutputInstance::Result
getNewestOutputResult(SubstanceAir::OutputInstance* output)
{
    SubstanceAir::OutputInstance::Result result = nullptr;
    if (output != nullptr) {
        for (SubstanceAir::OutputInstance::Result nextResult = output->grabResult();
             nextResult != nullptr;
             nextResult = output->grabResult()) {
            result = std::move(nextResult);
        }
    }
    return result;
}

std::shared_ptr<ArAsset>
convertToArAsset(const RenderResultImage& img, const std::string& graphName)
{
    auto tex = img.getTexture();
    size_t bytePerPixel = SbsarImage::getBytePerPixel(tex.pixelFormat);
    size_t data_size = tex.level0Height * tex.level0Width * bytePerPixel;
    size_t buffer_size = sizeof(SbsarImage::ImageHeader) + data_size;
    auto buffer = std::shared_ptr<char>(new char[buffer_size], std::default_delete<char[]>());
    SbsarImage::ImageHeader* header = reinterpret_cast<SbsarImage::ImageHeader*>(buffer.get());
    char* data = buffer.get() + sizeof(SbsarImage::ImageHeader);
    header->level0Width = tex.level0Width;
    header->level0Height = tex.level0Height;
    header->pixelFormat = tex.pixelFormat;
    header->channelsOrder = Substance_ChanOrder_RGBA;
    header->mipmapCount = tex.mipmapCount;
    header->isSRGB = graphName == "baseColor";

    memcpy(data, tex.buffer, data_size);
    return PXR_NS::ArInMemoryAsset::FromBuffer(std::move(buffer), buffer_size);
}

VtValue
convertToVtValue(const RenderResultNumericalBase& res)
{
    if (res.isNumerical()) {
        if (res.mType == Substance_IOType_Float) {
            const RenderResultFloat* num = dynamic_cast<const RenderResultFloat*>(&res);
            return VtValue(num->mValue);
        }
        // XXX asm doesn't have integer value, so if an int is found it's necessarily a bool
        else if (res.mType == Substance_IOType_Integer) {
            const RenderResultInt* num = dynamic_cast<const RenderResultInt*>(&res);
            if (num->mValue == 0) {
                return VtValue(false);
            } else {
                return VtValue(true);
            }
        } else {
            TF_RUNTIME_ERROR("Failed to convert to VtValue, unsupported output value");
            return VtValue();
        }
    }
    TF_RUNTIME_ERROR("Failed to convert to VtValue, engine result is not numerical");
    return VtValue();
}

void
renderGraph(Renderer& renderer,
            GraphInstanceData& instanceData,
            const ParsePathResult& sbsarParameters,
            AssetCache& assetCache)
{
    SubstanceAir::GraphInstance& instance = instanceData.getGraphInstance();

    for (const auto& o : instance.mDesc.mOutputs) {
        OutputInstance* oi = instance.findOutput(o.mUid);
        TF_AXIOM(oi != nullptr);
        patchOutputFormat(renderer, *oi);
    }

    applyPathParameters(instance.mDesc, instance, sbsarParameters.parameters);

    renderer.push(instance);
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRender: Starting rendering\n");
    renderer.run();
    renderer.flush();
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRender: Done rendering\n");
    // Local copy of sbsarParameters to adapt with the channel.
    ParsePathResult lastSbsarParameters = sbsarParameters;
    lastSbsarParameters.inputParameters = instanceData.getLastInputParameters();
    RenderResultCache renderResult;
    for (auto o : instance.getOutputs()) {
        OutputInstance::Result res = getNewestOutputResult(o);
        if (!res) {
            // The output was not updated, take the previous result of the instance and share it.
            TF_DEBUG(SBSAR_RENDER)
              .Msg("SbsarRender: Result was not computed for %s, looking for previous result\n",
                   o->mDesc.mIdentifier.c_str());

            for (const SubstanceAir::string& usage : o->mDesc.mChannelsStr) {
                lastSbsarParameters.usage = usage;
                if (auto previousAsset = assetCache.getAsset(lastSbsarParameters))
                    renderResult.addAsset(usage.c_str(), previousAsset);
                else {
                    VtValue previousValue = assetCache.getNumericalValue(lastSbsarParameters);
                    if (!previousValue.IsEmpty())
                        renderResult.addNumericalValue(usage.c_str(), previousValue);
                    else
                        TF_RUNTIME_ERROR("SbsarRender: Previous result not found for %s",
                                         usage.c_str());
                }
            }
            continue;
        } else if (res->isNumerical()) {
            auto renderResultNumerical = dynamic_cast<RenderResultNumericalBase*>(res.get());
            TF_AXIOM(renderResultNumerical);
            for (const SubstanceAir::string& usage : o->mDesc.mChannelsStr)
                renderResult.addNumericalValue(usage.c_str(),
                                               convertToVtValue(*renderResultNumerical));
        } else if (res->isImage()) {
            auto renderResultImage = dynamic_cast<RenderResultImage*>(res.get());
            TF_AXIOM(renderResultImage);
            for (const SubstanceAir::string& usage : o->mDesc.mChannelsStr) {
                renderResult.addAsset(usage.c_str(),
                                      convertToArAsset(*renderResultImage, usage.c_str()));
            }
        }
    }
    assetCache.addRenderResult(sbsarParameters, std::move(renderResult));
    instanceData.setLastInputParameters(sbsarParameters.inputParameters);
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRender: Done update result\n");
}

}
