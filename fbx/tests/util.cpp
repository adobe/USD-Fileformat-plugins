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
#include "util.h"

#include <iostream>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>

using namespace PXR_NS;

FbxLoaderSingleton::~FbxLoaderSingleton()
{
    // TODO: add callback to load images
    // if (readCallback) {
    //     readCallback->Destroy();
    // }
    if (manager) {
        manager->Destroy();
    }
}

FbxLoaderSingleton&
FbxLoaderSingleton::getInstance()
{
    static FbxLoaderSingleton instance;
    return instance;
}

FbxScene*
FbxLoaderSingleton::loadScene(std::string filename)
{
    std::lock_guard<std::recursive_mutex> lock(mFbxLoaderMutex);
    if (!manager) {
        TF_WARN("ERROR: FBX manager not initialized\n");
        return nullptr;
    }

    FbxImporter* importer = FbxImporter::Create(manager, IOSROOT);
    if (!importer) {
        TF_WARN("ERROR: FBX importer could not be initialized\n");
        return nullptr;
    }

    FbxIOSettings* ios = FbxIOSettings::Create(manager, IOSROOT);
    if (!ios) {
        TF_WARN("Failed to create FbxIOSettings\n");
        importer->Destroy();
        return nullptr;
    }

    FbxScene* scene = FbxScene::Create(manager, "root");

    bool onlyMaterials = false;
    bool importImages = true; // TODO: use this when adding callback below
    ios->SetBoolProp(IMP_FBX_MATERIAL, true);
    ios->SetBoolProp(IMP_FBX_TEXTURE, true);
    ios->SetBoolProp(IMP_FBX_ANIMATION, !onlyMaterials);
    ios->SetBoolProp(IMP_FBX_MODEL, !onlyMaterials);

    if (!importer->Initialize(filename.c_str(), -1, ios)) {
        FbxString error = importer->GetStatus().GetErrorString();
        TF_WARN("Call to FbxImporter::Initialize() failed on opening file %s\n", filename.c_str());
        TF_WARN("Error returned: %s\n", error.Buffer());
        importer->Destroy();
        ios->Destroy();
        if (scene) {
            scene->Destroy();
        }
        return nullptr;
    }

    // TODO: add callback. The following code has been copied from fbx.cpp as an example

    // // Create the read callback to handle loading embedded data (ie images)
    // FbxEmbeddedFileCallback* readCallback =
    // FbxEmbeddedFileCallback::Create(fbx.manager, "EmbeddedFileReadCallback");

    // if (!readCallback) {
    //     TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Failed to create FbxEmbeddedFileCallback");
    //     importer->Destroy();
    //     ios->Destroy();
    //     return false;
    // }

    // readCallback->RegisterReadFunction(EmbedReadCBFunction, (void*)&fbx);
    // importer->SetEmbeddedFileReadCallback(readCallback);

    // // let fbx own readCallback
    // fbx.readCallback = readCallback;

    if (!importer->Import(scene)) {
        FbxString error = importer->GetStatus().GetErrorString();
        TF_WARN("Call to FbxImporter::Import() failed.\n");
        TF_WARN("Error returned: %s\n", error.Buffer());
        importer->Destroy();
        ios->Destroy();
        if (scene) {
            scene->Destroy();
        }
        return nullptr;
    }

    importer->Destroy();
    ios->Destroy();
    return scene;
}

FbxLoaderSingleton::FbxLoaderSingleton()
{
    manager = FbxManager::Create();
    if (!manager) {
        TF_WARN("ERROR: Unable to create FBX manager\n");
    }
}

FbxScene*
getFbxSceneFromUsd(const std::filesystem::path& usdFilepath,
                   const std::filesystem::path& tempDirName)
{
    std::filesystem::path fbxFilename = usdFilepath.filename();
    fbxFilename.replace_extension(".fbx");

    // Create a temporary folder
    std::filesystem::path tempDir = usdFilepath.parent_path() / tempDirName;
    if (!std::filesystem::exists(tempDir)) {
        std::filesystem::create_directory(tempDir);
    }
    std::filesystem::path fbxPath = tempDir / fbxFilename;

    // Convert USD to FBX
    UsdStageRefPtr stage = UsdStage::Open(usdFilepath.string());
    if (!stage) {
        TF_WARN("Failed to open USD stage");
        return nullptr;
    }
    stage->Export(fbxPath.string());

    // Initialize the FBX loader
    auto& fbxLoader = FbxLoaderSingleton::getInstance();

    // Load the FBX file
    FbxScene* fbxScene = fbxLoader.loadScene(fbxPath.string());

    // Delete the fbx file now that it's been loaded
    std::filesystem::remove(fbxPath);

    // Delete the temporary folder
    std::filesystem::remove(tempDir);

    return fbxScene;
}

FbxNode*
getFbxNodeByPath(FbxScene* scene, std::string nodePath)
{
    if (!scene) {
        TF_WARN("Cannot find node with path '%s' because scene is null", nodePath.c_str());
        return nullptr;
    }

    FbxNode* rootNode = scene->GetRootNode();
    if (!rootNode) {
        TF_WARN("Cannot find node with path '%s' because root node is null", nodePath.c_str());
        return nullptr;
    }

    std::vector<std::string> nodePathVector = TfStringTokenize(nodePath, "/");
    if (nodePathVector.empty()) {
        TF_WARN("Cannot find node with non tokenizable name %s", nodePath.c_str());
        return nullptr;
    }

    // Verify that the first node in the path matches the root node's name
    if (nodePathVector[0] != rootNode->GetName()) {
        TF_WARN("Root node \"%s\" not found in path %s", rootNode->GetName(), nodePath.c_str());
        return nullptr;
    }

    // Iterate over the path, looking for a child node with the next expected name. We start with
    // index 1 because we already verified index 0 (the root node) above
    FbxNode* currentNode = rootNode;
    for (size_t pathIndex = 1; pathIndex < nodePathVector.size(); pathIndex++) {
        const std::string& nodeName = nodePathVector[pathIndex];
        currentNode = currentNode->FindChild(nodeName.c_str(), false);

        if (!currentNode) {
            TF_WARN("Could not find expected node with name \"%s\" in path %s",
                    nodeName.c_str(),
                    nodePath.c_str());
            return nullptr;
        }
    }

    // Found all nodes in the path, so we return the last one
    return currentNode;
}

/**
 * Private helper function:
 *
 * Recursively traverse the FBX node tree and collect paths to all nodes. The paths found will be
 * added to the provided vector. Paths will be constructed using forward slashes.
 *
 * @param node The current FbxNode to process.
 * @param currentPath The path accumulated so far, which will be updated with the current node
 *                    name.
 * @param paths A vector to collect all paths found in the FBX node tree. This vector will be
 *              modified.
 */
void
getFbxNodePathsHelper(FbxNode* node, std::string currentPath, std::vector<std::string>& paths)
{
    if (!node) {
        return;
    }

    // Traverse the node
    std::string nodeName = node->GetName();

    // Matches USD paths with forward slash, as opposed to the filesystem path
    currentPath += "/" + nodeName;
    paths.push_back(currentPath);

    // Recursively process all child nodes
    for (int i = 0; i < node->GetChildCount(); ++i) {
        getFbxNodePathsHelper(node->GetChild(i), currentPath, paths);
    }
}

std::vector<std::string>
getFbxNodePaths(FbxScene* scene)
{
    if (!scene) {
        TF_WARN("Cannot get FBX node paths because scene is null");
        return {};
    }

    FbxNode* rootNode = scene->GetRootNode();
    if (!rootNode) {
        TF_WARN("Cannot get FBX node paths because root node is null");
        return {};
    }

    // Start with an empty path and collect all paths
    std::vector<std::string> paths;
    getFbxNodePathsHelper(rootNode, "", paths);
    return paths;
}