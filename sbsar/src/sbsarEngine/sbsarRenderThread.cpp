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

#include <config/sbsarConfig.h>

#include <sbsarDebug.h>

#include <substance/framework/framework.h>

#include <utility>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/ar/asset.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

PXR_NAMESPACE_USING_DIRECTIVE
namespace adobe::usd::sbsar {

using namespace std::chrono_literals;

//! Key : package path + parse result
using RenderCacheKey = std::pair<std::string, std::string>;

// ---------------------------------------------------------------------------
// Render thread lifetime — three invariants govern this file
//
//  1. Active drain on shutdown. The render thread parks in cv.wait_for(30s)
//     when idle; teardown that just drops a pointer would block process
//     exit for up to 30s. shutdown() sets stopRequested AND notify_all() so
//     the thread reacts immediately.
//
//  2. Join before sibling statics tear down. The thread touches USD,
//     SubstanceAir and the asset resolver — if their file-scope state has
//     already been destroyed, the thread will use-after-free. The join is
//     anchored to a function-local static (ShutdownAtExit) whose destructor
//     runs LIFO before g_state and earlier-registered peers; see
//     getRenderThreadState for the ordering proof.
//
//  3. The render thread is never the last strong holder of RenderThreadState.
//     If it were, dropping the last ref on the render thread would run
//     ~RenderThreadState there and try to join() itself. ShutdownAtExit
//     pins a strong ref across join(); the thread itself carries only a
//     weak_ptr that it upgrades per outer iteration.
//
// Two shortcuts have been considered and rejected: giving the render thread
// a shared_ptr (creates the self-join cycle in #3) and dropping g_state to
// let refcounting tear down (violates #1 and #2). Modify via shutdown() and
// the shared/weak split instead.
//
// Windows: a previous version wrapped renderThread in a no-op deleter so it
// was never joined — a guaranteed use-after-free masked by ExitProcess
// killing the thread. Joining at atexit fixes that, but runs under CRT
// shutdown, which can hold the loader lock. If a future change makes the
// render thread block on anything that itself needs the loader lock (DLL
// load, COM init, cross-thread GDI), the join will deadlock. There is no
// off-the-shelf alternative — USD does not provide a plugin-unload hook
// — so the constraint to honor when extending render-thread work is:
// nothing on the shutdown path may require the loader lock. Relatedly,
// ExitProcess can terminate the render thread mid-lock, leaving the
// underlying SRWLOCK orphaned; shutdown() uses try_lock to avoid
// deadlocking on it.
// ---------------------------------------------------------------------------

struct RenderThreadState
{
    std::thread renderThread;
    std::mutex lock;
    std::condition_variable cv;
    std::atomic<bool> stopRequested{ false };
    std::atomic<bool> shutdownStarted{ false };
    std::shared_ptr<SubstanceAir::Renderer> renderer;
    AssetCache assetCache;
    CacheStats cacheStats;
    SbsarConfigRefPtr config;
    std::map<RenderCacheKey, ParsePathResult> readRequests;

    static std::shared_ptr<RenderThreadState> create();

    // Idempotent: sets stopRequested, wakes the render thread, and joins it.
    // Safe to call from any thread other than the render thread itself.
    void shutdown();

    ~RenderThreadState();

private:
    RenderThreadState() = default;
};

void
renderThreadFn(std::weak_ptr<RenderThreadState> weakState);

namespace {

std::mutex g_renderInitMutex;
// Sole strong reference owning the singleton. The render thread holds only a
// weak_ptr; consumers (requestRender/clearCache/getCacheStats) take a copy of
// this shared_ptr for the duration of their call so the state cannot be
// destroyed out from under them.
std::shared_ptr<RenderThreadState> g_state;
// Once set, getRenderThreadState() returns nullptr. Prevents a USD worker
// thread from re-creating g_state during atexit teardown.
bool g_shuttingDown = false;

// Drains the render thread at atexit. Registered as a function-local static
// in getRenderThreadState() so its destructor runs LIFO before g_state's.
//
// Ordering proof: g_state above is namespace-scope and its initializer is
// shared_ptr's default constructor, which is constexpr in C++17 — so g_state
// is constant-initialized during the static-init phase, before any dynamic
// initialization. ShutdownAtExit is a function-local static, so it registers
// with __cxa_atexit on first call to getRenderThreadState(), strictly later.
// LIFO destruction therefore guarantees this hook runs first, while
// USD/Substance/asset resolver are still alive — the safe window to join.
struct ShutdownAtExit
{
    ~ShutdownAtExit()
    {
        std::shared_ptr<RenderThreadState> state;
        {
            std::lock_guard<std::mutex> _l(g_renderInitMutex);
            g_shuttingDown = true;
            state = std::move(g_state);
        }
        if (state) {
            // Drain the render thread synchronously. We still hold a strong
            // reference here, so the render thread cannot be the last holder.
            state->shutdown();
        }
        // 'state' drops here. If no consumer thread still holds a reference,
        // ~RenderThreadState runs now on the main thread; shutdown() is
        // idempotent, so the destructor's call to it is a no-op.
    }
};

} // namespace

std::shared_ptr<RenderThreadState>
getRenderThreadState()
{
    std::lock_guard<std::mutex> _l(g_renderInitMutex);
    if (g_shuttingDown) {
        return nullptr;
    }
    if (!g_state) {
        g_state = RenderThreadState::create();
        // First-use registration of the atexit drain — see ShutdownAtExit.
        static const ShutdownAtExit shutdownHook;
        (void)shutdownHook;
    }
    return g_state;
}

std::shared_ptr<RenderThreadState>
RenderThreadState::create()
{
    auto state = std::shared_ptr<RenderThreadState>(new RenderThreadState());
    // Materialize the config before the render thread starts so it cannot
    // race with first-use construction inside the loop.
    state->config = getSbsarConfig();
    // weak_ptr (not shared_ptr) — see invariant #3 in the file header: a
    // shared_ptr capture would let the render thread become the last holder
    // and self-join inside ~RenderThreadState.
    std::weak_ptr<RenderThreadState> weak = state;
    state->renderThread = std::thread(renderThreadFn, weak);
    return state;
}

void
RenderThreadState::shutdown()
{
    if (shutdownStarted.exchange(true)) {
        return;
    }
    stopRequested = true;
    {
        // try_lock, not lock: normally serializes with a render-thread
        // waiter mid-predicate-check to close the missed-wakeup window.
        // On Windows, ExitProcess can kill the render thread mid-lock and
        // orphan the SRWLOCK; a blocking acquire would deadlock. The worst
        // case if we proceed without the lock is one missed wakeup,
        // bounded by cv.wait_for's 30 s timeout.
        std::unique_lock<std::mutex> guard(lock, std::try_to_lock);
        if (!guard.owns_lock()) {
            TF_RUNTIME_ERROR("SbsarRenderThread: shutdown could not acquire state lock — "
                             "render thread likely terminated mid-lock by ExitProcess");
        }
    }
    // Wake the render thread (and any requestRender() waiters) so they observe
    // stopRequested and exit promptly, instead of sitting in cv.wait_for() for
    // up to 30 s before join() can complete.
    cv.notify_all();
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Waiting for render thread to stop\n");
    if (renderThread.joinable()) {
        renderThread.join();
    } else {
        // Should be joinable from construction until we join it here.
        TF_RUNTIME_ERROR("SbsarRenderThread: render thread is not joinable at shutdown");
    }
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Cleaning up renderer\n");
    // At this point no other thread of ours can touch state members. Clear the
    // renderer explicitly so its (intentionally no-op-deleted) shared_ptr is
    // released here rather than during arbitrary member-destruction order.
    renderer.reset();
}

RenderThreadState::~RenderThreadState()
{
    TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Releasing\n");
    shutdown();
}

//! \brief Render thread function
//! This function is the main loop of the render thread. It will wait for request from
//! requestAsset(), render the asset and store the result in the AssetCache.
//! The lock is released during the expensive substance rendering to allow requesting
//! threads to consume results and avoid cache eviction of unconsumed entries.
void
renderThreadFn(std::weak_ptr<RenderThreadState> weakState)
{
    bool firstIteration = true;
    while (true) {
        // Hold a strong reference only for the duration of one outer
        // iteration. ShutdownAtExit pins its own reference across join(), so
        // this thread is never the last holder — see RenderThreadState::shutdown.
        std::shared_ptr<RenderThreadState> state = weakState.lock();
        if (!state || state->stopRequested) {
            return;
        }
        try {
            if (firstIteration) {
                TF_AXIOM(!state->renderer);
                firstIteration = false;
            }
            std::unique_lock<std::mutex> guard(state->lock);
            // INVARIANT: The lock (guard) must be held at the start and end of each
            // iteration of this inner loop. The lock is temporarily released during
            // executeGraph() for the expensive rendering, but must be re-acquired
            // before any continue/break. Any new exit path must maintain this.
            while (!state->readRequests.empty() && !state->stopRequested) {
                auto req = state->readRequests.begin();
                ParsePathResult parsePathResult = req->second;
                std::string packagePath = req->first.first;
                RenderCacheKey requestKey = req->first;

                // Checking cache. Even if the cache check in
                // renderSbsarAsset failed, the texture might have been
                // prefetched at this point so we can skip rendering
                if (!state->assetCache.hasRenderResult(parsePathResult)) {
                    // We initialize the renderer just before it is needed to render a request
                    if (!state->renderer) {
                        TF_DEBUG(SBSAR_RENDER)
                          .Msg("SbsarRenderThread: Initialize Substance engine\n");
                        // Make sure the renderer is initialized inside the render thread
                        // to avoid any issues with creating GL contexts from the wrong thread
                        state->renderer = std::shared_ptr<SubstanceAir::Renderer>(
                          new SubstanceAir::Renderer(SubstanceAir::RenderOptions(),
                                                     getPreferredEngineDll()),
                          [](SubstanceAir::Renderer*) {});
                    }

                    ++state->cacheStats.renderingCall;
                    TF_DEBUG(SBSAR_RENDER)
                      .Msg("SbsarRenderThread: Didn't find %s, "
                           "%s in cache. Texture "
                           "was "
                           "not prefetched yet\n",
                           packagePath.c_str(),
                           requestKey.second.c_str());
                    std::shared_ptr<GraphInstanceData> instance =
                      getGraphInstanceFromPackageCache(packagePath, parsePathResult);

                    if (!instance) {
                        TF_RUNTIME_ERROR(
                          "SbsarRenderThread: Failed to get graph instance for %s, %s",
                          packagePath.c_str(),
                          requestKey.second.c_str());
                        state->readRequests.erase(requestKey);
                        state->cv.notify_all();
                        continue;
                    }

                    // Prepare the graph instance (fast, under lock)
                    prepareGraph(*state->renderer, *instance, parsePathResult);

                    // Release lock for the expensive rendering
                    guard.unlock();

                    try {
                        executeGraph(*state->renderer, instance->getGraphInstance());
                    } catch (std::exception& e) {
                        TF_RUNTIME_ERROR("SbsarRenderThread: Render failed for %s: %s",
                                         packagePath.c_str(),
                                         e.what());
                        guard.lock();
                        state->readRequests.erase(requestKey);
                        state->cv.notify_all();
                        continue;
                    }

                    // Re-acquire lock to store results and update shared state
                    guard.lock();

                    // Collect results and store in cache (under lock)
                    collectAndStoreResults(*instance, parsePathResult, state->assetCache);
                } else {
                    ++state->cacheStats.resultFoundInCache;
                    TF_DEBUG(SBSAR_RENDER)
                      .Msg("SbsarRenderThread: Skipping rendering: found %s, %s in "
                           "cache. Texture was "
                           "prefetched\n",
                           packagePath.c_str(),
                           requestKey.second.c_str());
                }

                // Erase request AFTER result is in cache, so requesting threads
                // can see their request is still pending during rendering
                state->readRequests.erase(requestKey);
                state->cv.notify_all();
            }
            TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: waiting for jobs\n");
            // Notify before sleeping so any stuck threads get a wakeup
            state->cv.notify_all();
            if (!state->stopRequested) {
                state->cv.wait_for(guard, 30s);
            }
            TF_DEBUG(SBSAR_RENDER).Msg("SbsarRenderThread: Renderthread waking up\n");
        } catch (std::exception& e) {
            TF_RUNTIME_ERROR("SbsarRenderThread: Exception : %s", e.what());
            {
                std::lock_guard<std::mutex> guard(state->lock);
                state->stopRequested = true;
            }
            state->cv.notify_all();
            return;
        } catch (...) {
            TF_RUNTIME_ERROR("SbsarRenderThread: Exception");
            {
                std::lock_guard<std::mutex> guard(state->lock);
                state->stopRequested = true;
            }
            state->cv.notify_all();
            return;
        }
        // 'state' shared_ptr drops here, bringing the refcount back down so
        // that the owning ShutdownAtExit / consumer threads control destruction.
    }
}

//! Check if the result is valid.
template<typename T>
bool
resultIsValid(const T& elem)
{
    if constexpr (std::is_same_v<T, std::shared_ptr<SubstanceAir::RenderResultImage>>)
        return elem != nullptr;
    else if constexpr (std::is_same_v<T, VtValue>)
        return !elem.IsEmpty();
}

//! Look for a asset or a numerical value in the cache and return it if found.
template<typename T>
T
findResultInCache(const ParsePathResult& parseOutput, RenderThreadState* state)
{
    if constexpr (std::is_same_v<T, std::shared_ptr<SubstanceAir::RenderResultImage>>)
        return state->assetCache.getRenderResultImage(parseOutput);
    if constexpr (std::is_same_v<T, VtValue>)
        return state->assetCache.getNumericalValue(parseOutput);
}

//! Check if the result was stored in the other cache.
template<typename T>
bool
resultExistInTheOtherCache(const ParsePathResult& parseOutput, RenderThreadState* state)
{
    if constexpr (std::is_same_v<T, std::shared_ptr<SubstanceAir::RenderResultImage>>)
        return resultIsValid<VtValue>(state->assetCache.getNumericalValue(parseOutput));
    if constexpr (std::is_same_v<T, VtValue>)
        return resultIsValid<std::shared_ptr<SubstanceAir::RenderResultImage>>(
          state->assetCache.getRenderResultImage(parseOutput));
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

    std::shared_ptr<RenderThreadState> state = getRenderThreadState();
    if (!state) {
        return ResultType{};
    }
    auto requestKey = std::make_pair(packagePath, packagedPath);
    {
        std::unique_lock<std::mutex> guard(state->lock);
        // Checking for cached result
        auto result = findResultInCache<ResultType>(parseOutput, state.get());
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

        constexpr auto kWaitTimeout = 5s;
        constexpr int kMaxRetries = 12; // 1 minute total at 5s per timeout
        int retries = 0;

        while (true) {
            state->cv.wait_for(guard, kWaitTimeout);

            // Check for shutdown
            if (state->stopRequested) {
                TF_WARN("SbsarRenderThread: Shutdown while waiting for %s, %s",
                        packagePath.c_str(),
                        packagedPath.c_str());
                return ResultType{};
            }

            result = findResultInCache<ResultType>(parseOutput, state.get());
            if (resultIsValid(result)) {
                TF_DEBUG(SBSAR_RENDER)
                  .Msg("SbsarRenderThread: Result send to hydra %s, %s\n",
                       packagePath.c_str(),
                       packagedPath.c_str());
                return result;
            } else if (resultExistInTheOtherCache<ResultType>(parseOutput, state.get())) {
                TF_WARN("SbsarRenderThread: the requested result is not of the right type (VtValue "
                        "or ArAsset): %s, %s\n",
                        packagePath.c_str(),
                        packagedPath.c_str());
                return ResultType{};
            }

            // If the request is still pending, the render thread hasn't finished yet
            if (state->readRequests.find(requestKey) != state->readRequests.end()) {
                continue;
            }

            // Request was processed but result not in cache (evicted).
            // Re-submit the request.
            ++retries;
            if (retries > kMaxRetries) {
                TF_RUNTIME_ERROR("SbsarRenderThread: Exceeded max retries waiting for %s, %s",
                                 packagePath.c_str(),
                                 packagedPath.c_str());
                return ResultType{};
            }
            TF_WARN("SbsarRenderThread: Result evicted before consumption, "
                    "re-submitting request (retry %d) for %s, %s",
                    retries,
                    packagePath.c_str(),
                    packagedPath.c_str());
            state->readRequests[requestKey] = parseOutput;
            state->cv.notify_all();
        }
    }
}

std::shared_ptr<SubstanceAir::RenderResultImage>
renderSbsarAsset(const std::string& packagePath, const std::string& packagedPath)
{
    return requestRender<std::shared_ptr<SubstanceAir::RenderResultImage>>(packagePath,
                                                                           packagedPath);
}

VtValue
renderSbsarValue(const std::string& packagePath, const std::string& packagedPath)
{
    return requestRender<VtValue>(packagePath, packagedPath);
}

void
clearCache()
{
    std::shared_ptr<RenderThreadState> state = getRenderThreadState();
    if (!state) {
        return;
    }
    std::unique_lock<std::mutex> guard(state->lock);
    state->cacheStats = CacheStats();
    state->assetCache.clearCache();
    clearInputImageCache();
    clearPackageCache();
}

CacheStats&
getCacheStats()
{
    // Test-only accessor. The returned reference is valid for as long as the
    // singleton lives, which is until atexit. Tests must not retain it past
    // process shutdown.
    std::shared_ptr<RenderThreadState> state = getRenderThreadState();
    static CacheStats sEmptyStats;
    if (!state) {
        return sEmptyStats;
    }
    return state->cacheStats;
}

} // namespace adobe::usd::sbsar
