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
#include <sbsarEngine/sbsarAssetCache.h>
#include <sbsarEngine/sbsarEngine.h>
#include <sbsarEngine/sbsarInputImageCache.h>
#include <sbsarEngine/sbsarPackageCache.h>
#include <sbsarEngine/sbsarRender.h>
#include <sbsarEngine/sbsarRenderThread.h>

#include <assetPath/assetPathParser.h>
#include <assetResolver/sbsarImage.h>
#include <sbsarDebug.h>
#include <usdGeneration/usdGenerationHelpers.h>

#include <substance/framework/framework.h>
#include <substance/linker/handle.h>

#include <utility>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/ar/asset.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <set>
#include <thread>

PXR_NAMESPACE_USING_DIRECTIVE
namespace adobe::usd::sbsar {

using namespace std::chrono_literals;

//! Key : package path + parse result
using RenderCacheKey = std::pair<std::string, std::string>;

struct RenderThreadState
{
    std::shared_ptr<std::thread> renderThread;
    std::mutex lock;
    std::condition_variable cv;
    bool shutDown = false;
    std::shared_ptr<SubstanceAir::Renderer> renderer;
    AssetCache assetCache;
    CacheStats cacheStats;
    CacheSize cacheSize;
    std::map<RenderCacheKey, ParsePathResult> readRequests;

    RenderThreadState();
    ~RenderThreadState();
};

std::mutex renderInitMutex;
std::unique_ptr<RenderThreadState> g_state;

RenderThreadState*
getRenderThreadState()
{
    if (!g_state) {
        std::lock_guard<std::mutex> _l(renderInitMutex);
        if (!g_state) {
            // Jumping through some hoops because of some kind of overridden
            // deleter in SubstanceAir?
            g_state = std::unique_ptr<RenderThreadState>(new RenderThreadState(),
                                                         std::default_delete<RenderThreadState>());
        }
    }
    return g_state.get();
}

//! \brief Render thread function
//! This function is the main loop of the render thread. It will wait for request from
//! requestAsset(), render the asset and store the result in the AssetCache.
void
renderThreadFn()
{
    try {
        RenderThreadState* state = getRenderThreadState();
        TF_AXIOM(!state->renderer);
        // Make sure the renderer is initialized inside the render thread
        // to avoid any issues with creating GL contexts from the wrong thread
        state->renderer = std::shared_ptr<SubstanceAir::Renderer>(
          new SubstanceAir::Renderer(SubstanceAir::RenderOptions(), getPreferredEngineDll()),
          [](SubstanceAir::Renderer*) {});

        while (!state->shutDown) {
            std::unique_lock<std::mutex> guard(state->lock);
            while (!state->readRequests.empty()) {
                auto req = state->readRequests.begin();
                const ParsePathResult& parsePathResult = req->second;
                const std::string& packagePath = req->first.first;
                // Checking cache. Even if the cache check in
                // renderSbsarAsset failed, the texture might have been
                // prefetched at this point so we can skip rendering
                if (!state->assetCache.hasRenderResult(parsePathResult)) {
                    ++state->cacheStats.renderingCall;
                    TF_DEBUG(SBSAR_RENDER)
                      .Msg("SbsarRenderThread: Didn't find %s, "
                           "%s in cache. Texture "
                           "was "
                           "not prefetched yet\n",
                           packagePath.c_str(),
                           req->first.second.c_str());
                    std::shared_ptr<GraphInstanceData> instance =
                      getGraphInstanceFromPackageCache(packagePath, parsePathResult);
                    renderGraph(*state->renderer, *instance, parsePathResult, state->assetCache);
                } else {
                    ++state->cacheStats.resultFoundInCache;
                    TF_DEBUG(SBSAR_RENDER)
                      .Msg("SbsarRenderThread: Skipping rendering: found %s, %s in "
                           "cache. Texture was "
                           "prefetched\n",
                           packagePath.c_str(),
                           req->first.second.c_str());
                }

                TF_AXIOM(state->assetCache.hasRenderResult(parsePathResult));
                state->readRequests.erase(req);
                // Give threads reading a chance to consume
                // data before processing next request
                // TODO: Can we be more granualar here
                state->cv.notify_all();
                state->cv.wait_for(guard, 0s);
            }
            TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: waiting for jobs\n");
            if (!state->shutDown) {
                state->cv.wait_for(guard, 30s);
            }
            TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Renderthread waking up\n");
        }
        TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Renderthread finishing\n");
    } catch (std::exception& e) {
        TF_RUNTIME_ERROR("SbsarRenderThread: Exception : %s", e.what());
    } catch (...) {
        TF_RUNTIME_ERROR("SbsarRenderThread: Exception");
    }
}

//! Check if the result is valid.
template<typename T>
bool
resultIsValid(const T& elem)
{
    if constexpr (std::is_same_v<T, std::shared_ptr<ArAsset>>)
        return elem != nullptr;
    else if constexpr (std::is_same_v<T, VtValue>)
        return !elem.IsEmpty();
}

//! Look for a asset or a numerical value in the cache and return it if found.
template<typename T>
T
findResultInCache(const ParsePathResult& parseOutput, RenderThreadState* state)
{
    if constexpr (std::is_same_v<T, std::shared_ptr<ArAsset>>)
        return state->assetCache.getAsset(parseOutput);
    if constexpr (std::is_same_v<T, VtValue>)
        return state->assetCache.getNumericalValue(parseOutput);
}

//! Check if the result was stored in the other cache.
template<typename T>
bool
resultExistInTheOtherCache(const ParsePathResult& parseOutput, RenderThreadState* state)
{
    if constexpr (std::is_same_v<T, std::shared_ptr<ArAsset>>)
        return resultIsValid<VtValue>(state->assetCache.getNumericalValue(parseOutput));
    if constexpr (std::is_same_v<T, VtValue>)
        return resultIsValid<std::shared_ptr<ArAsset>>(state->assetCache.getAsset(parseOutput));
}

//! Ask to the cache if the asset or a value is already exist for the given paths, if not request a
//! render. The render is carried out on another thread.
template<typename ResultType>
ResultType
requestRender(const std::string& packagePath, const std::string& packagedPath)
{
    ParsePathResult parseOutput;
    ParsePathResult::ParseError parseResult = parsePath(packagedPath, parseOutput);
    if (parseResult != ParsePathResult::PE_SUCCESS) {
        TF_WARN("SbsarRenderThread: Error parsing path %s", packagedPath.c_str());
        return ResultType{};
    }

    RenderThreadState* state = getRenderThreadState();
    auto requestKey = std::make_pair(packagePath, packagedPath);
    {
        std::unique_lock<std::mutex> guard(state->lock);
        // Checking for cached result
        auto result = findResultInCache<ResultType>(parseOutput, state);
        if (resultIsValid(result)) {
            TF_DEBUG(SBSAR_RENDER)
              .Msg("SbsarRenderThread: Found result in cache %s, %s\n",
                   packagePath.c_str(),
                   packagedPath.c_str());
            ++state->cacheStats.resultFoundInCache;
            return result;
        }
        ++state->cacheStats.requestSend;
        TF_DEBUG(SBSAR_RENDER)
          .Msg("SbsarRenderThread: Result not found in cache %s, %s, submitting to render "
               "thread\n",
               packagePath.c_str(),
               packagedPath.c_str());

        // Check if a read requests for this texture has already
        // been submitted
        if (state->readRequests.find(requestKey) == state->readRequests.end()) {
            // No request submitted before, submit
            state->readRequests[requestKey] = parseOutput;
        }
        state->cv.notify_all();
        while (true) {
            state->cv.wait(guard);
            result = findResultInCache<ResultType>(parseOutput, state);
            if (resultIsValid(result)) {
                TF_DEBUG(SBSAR_RENDER)
                  .Msg("SbsarRenderThread: Result send to hydra %s, %s\n",
                       packagePath.c_str(),
                       packagedPath.c_str());
                return result;
            } else if (resultExistInTheOtherCache<ResultType>(parseOutput, state)) {
                TF_WARN("SbsarRenderThread: the requested result is not of the right type (VtValue "
                        "or ArAsset): %s, %s\n",
                        packagePath.c_str(),
                        packagedPath.c_str());
                return ResultType{};
            }
        }
    }
}

std::shared_ptr<ArAsset>
renderSbsarAsset(const std::string& packagePath, const std::string& packagedPath)
{
    return requestRender<std::shared_ptr<ArAsset>>(packagePath, packagedPath);
}

VtValue
renderSbsarValue(const std::string& packagePath, const std::string& packagedPath)
{
    return requestRender<VtValue>(packagePath, packagedPath);
}

void
clearCache()
{
    RenderThreadState* state = getRenderThreadState();
    {
        std::unique_lock<std::mutex> guard(state->lock);
        state->cacheStats = CacheStats();
        state->assetCache.clearCache();
        clearInputImageCache();
        clearPackageCache();
    }
}

CacheStats&
getCacheStats()
{
    RenderThreadState* state = getRenderThreadState();
    return state->cacheStats;
}

CacheSize&
getCacheSize()
{
    RenderThreadState* state = getRenderThreadState();
    return state->cacheSize;
}

RenderThreadState::RenderThreadState()
{
#ifdef _WIN32
    // Remove destructor call
    // since threads are killed before static data
    // is released on windows
    renderThread =
      std::shared_ptr<std::thread>(new std::thread(renderThreadFn), [](std::thread*) {});
#else  // _WIN32
    renderThread = std::make_unique<std::thread>(renderThreadFn);
#endif // _WIN32
    // Remove destruction to "work around" lock at end
    // Leaving Renderer uninitialized to make sure the renderer is created
    // by the render thread to avoid GL context issues
}
RenderThreadState::~RenderThreadState()
{
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Releasing\n");
    RenderThreadState* s = getRenderThreadState();
    std::unique_lock<std::mutex> guard(lock);
    shutDown = true;
    guard.unlock();
    cv.notify_all();
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Waiting for render thread to stop\n");
    renderThread->join();
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Cleaning up renderer\n");
    renderer.reset();
}
CacheSize::CacheSize()
{
    setMaxAssetCacheSize();
    setMaxInputImageCacheSize();
    setMaxPackageCacheSize();
}

std::size_t
CacheSize::getMaxAssetCacheSize() const
{
    return m_maxAssetCacheSize;
}

std::size_t
CacheSize::getMaxInputImageCacheSize() const
{
    return m_maxInputImageCacheSize;
}

std::size_t
CacheSize::getMaxPackageCacheSize() const
{
    return m_maxPackageCacheSize;
}
void
CacheSize::setMaxAssetCacheSize(std::size_t size)
{
    m_maxAssetCacheSize = size;
}

void
CacheSize::setMaxInputImageCacheSize(std::size_t size)
{
    m_maxInputImageCacheSize = size;
}
void
CacheSize::setMaxPackageCacheSize(std::size_t size)
{
    if (size == 0) {
        TF_RUNTIME_ERROR("Package cache size cannot be 0");
        return;
    }
    m_maxPackageCacheSize = size;
}

} // namespace adobe::usd::sbsar
