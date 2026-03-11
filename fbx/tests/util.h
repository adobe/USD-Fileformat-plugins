/*
Copyright 2025 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#pragma once

#include <filesystem>
#include <mutex>

#include <fbxsdk.h>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>

class FbxLoaderSingleton
{
public:
    /**
     * Get the singleton instance of FbxLoaderSingleton.
     */
    static FbxLoaderSingleton& getInstance();
    ~FbxLoaderSingleton();

    /**
     * Load an FBX scene from a file.
     *
     * NOTE: This requires scene->Destroy() to be called eventually to free resources!
     *
     * @param filename The path to the FBX file to load.
     *
     * @return A pointer to the loaded FbxScene, or nullptr if loading failed.
     */
    FbxScene* loadScene(std::string filename);

    /**
     * Get the FbxManager associated with a given FbxScene.
     */
    inline fbxsdk::FbxManager* GetFbxManager(const fbxsdk::FbxScene* scene)
    {
        return scene ? scene->GetFbxManager() : nullptr;
    }

private:
    FbxLoaderSingleton();
    std::recursive_mutex mFbxLoaderMutex;

    FbxManager* manager;
};

/**
 * Export a USD file to an FBX file on disk, and load it in as an FbxScene. This can be used for
 * verifying FBX export.
 *
 * Note that a temporary file will be created and deleted on disk with the name of the USD file,
 * but with a ".fbx" extension. It will be located next to the original USD file.
 *
 * @param usdFilepath The path to the USD file to convert. It should have a valid USD extension
 *                    (e.g., .usd, .usda, .usdc), and should be relative to the current working
 *                    directory. It is recommended for this to simply be a filename
 * @param tempDirName The name of the temporary directory where the FBX file will be created. This
 *                    directory will be created next to the USD file, and the FBX file will be
 *                    placed inside it. It will be deleted after the conversion. Defaults to "tmp"
 *
 * WARNING: a directory next to the given USD with the same name as tempDirName (tmp by default)
 * will be deleted! Do not run this function if there is a folder with such name that should not
 * be removed
 *
 * @return A pointer to the loaded FbxScene, or nullptr if the conversion failed.
 */
FbxScene*
getFbxSceneFromUsd(const std::filesystem::path& usdFilepath,
                   const std::filesystem::path& tempDirName = "tmp");

/**
 * Get a specific FbxNode by its path within the FBX file. The path should be an absolute path
 * starting with "/RootNode" or RootNode, using forward slashes. A leading slash is optional.
 *
 * For instance, with the following hierarchy:
 *   RootNode -> ChildNode -> GrandChildNode
 * The expected path to find GrandChildNode would be "/RootNode/ChildNode/GrandChildNode".
 *
 * @param scene The FBX scene
 * @param nodePath The path to the node to find
 *
 * @return A pointer to the found FbxNode, or nullptr if not found.
 */
FbxNode*
getFbxNodeByPath(FbxScene* scene, std::string nodePath);

/**
 * Get the paths of all FbxNodes in the given FbxNode hierarchy.
 * The paths are returned as a vector of strings, where each string is a path starting with the
 * root node.
 *
 * @param scene The FbxScene from which to retrieve the node paths.
 *
 * @return A vector of strings containing the paths of all nodes.
 */
std::vector<std::string>
getFbxNodePaths(FbxScene* scene);
