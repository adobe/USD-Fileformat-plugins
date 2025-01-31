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
#pragma once
#include "usdData.h"

namespace adobe::usd {

// Struct that holds information about found issues in the scene
struct Issue
{
    enum class Level
    {
        Error,
        Warning,
        Info,
        Hint
    };
    Level level;         // Severity
    std::string path;    // Path to scene object that has issue
    std::string message; // Message in English
    // TODO: add a code for type of issue to facilitate creation of nice UI (in different languages)
};

using IssueVector = std::vector<Issue>;

struct MeshVerificationOptions
{
    // Warn if faces have high vertex counts, which is usually an indication of a bad mesh
    // Set to -1 to disable the check and potential warning
    // XXX what is a good and reasonable default value? Some not so great meshes have very high
    // numbers
    int highFaceVertexCountWarningThreshold = -1;
    bool checkForFiniteFloats = true;
    // TODO add option to check for unused values/indices
    // TODO add option to check for zero or unnormalized normals?
    // TODO add option to check for out of [0,1] bounds UV coordinates? UDIM check?
};

/// \ingroup utils_geometry
/// \brief Check a mesh for inconsistencies between the topology and the primvars.
/// Returns true if no errors were detected.
/// If the issues pointer is valid, the issues are appended to the vector, which can include
/// warnings and hints, which won't fail the verification, but might still be issues. Some of the
/// checks can be controlled with the options struct.
USDFFUTILS_API bool
verifyMesh(const std::string& path,
           const Mesh& mesh,
           IssueVector* issues = nullptr,
           const MeshVerificationOptions& options = {});

/// \ingroup utils_geometry
/// \brief Checks all meshes in the UsdData for issues and collects them in the options issues
/// vector
USDFFUTILS_API bool
verifyMeshes(const UsdData& usdData,
             IssueVector* issues = nullptr,
             const MeshVerificationOptions& options = {});

/// \ingroup utils_geometry
/// \brief Prints the issues via the TfDebug::Helper and the provided debugTag
USDFFUTILS_API void
printIssues(const IssueVector& issues);

/// \ingroup utils_geometry
/// \brief If the TF_DEBUG flag for this module is set it will check all meshes in the UsdData and
/// report issues
USDFFUTILS_API void
checkAndPrintMeshIssues(const UsdData& usdData);

/// \ingroup utils_geometry
/// \brief Several file formats may require traingulated meshes.
USDFFUTILS_API void
createTriangulationIndices(Mesh& mesh);

/// \ingroup utils_geometry
/// \brief Triangulate an existing mesh with all its primvars and subsets.
// Note, the triangulation is done with a simple fan triangulation and hence
// this only works for correctly for convex faces.
USDFFUTILS_API bool
triangulateMesh(Mesh& mesh);

/// \ingroup utils_geometry
/// \brief Some formats like GLTF can't handle the complex mesh representation that is
// USD native, especially with regard to primvar interpolation. For these
// formats we expand the mesh into a version where all data can be vertex
// interpolated, which means a point and all its associated primvars (normals,
// tangent, uvs, colors, opacities) are aligned.
USDFFUTILS_API void
forceVertexInterpolation(Mesh& mesh);

/// \ingroup utils_geometry
/// \brief Given the topology of a complete mesh and a subset of face indices into that
// mesh, compute the corresponding face vertex indices
USDFFUTILS_API void
computeFaceVertexIndicesForSubset(const PXR_NS::VtIntArray& faceVertexCounts,
                                  const PXR_NS::VtIntArray& faceVertexIndices,
                                  const PXR_NS::VtIntArray& subsetFaceIndices,
                                  PXR_NS::VtIntArray& subsetFaceVertexIndices);

/// \ingroup utils_geometry
/// \brief Remove the indexing out of a set of values.
template<typename T>
void
expandIndexedValues(const PXR_NS::VtIntArray& indices, PXR_NS::VtArray<T>& values)
{
    if (values.empty()) {
        return;
    } else if (values.size() == 1) {
        values.assign(indices.size(), values.front());
    } else {
        PXR_NS::VtArray<T> temp = std::move(values);
        unsigned int size = indices.size();
        values.resize(size);
        for (unsigned int i = 0; i < size; i++) {
            int index = indices[i];
            if (index < 0 && index >= temp.size()) {
                // set invalid indices to 0
                index = 0;
            }
            values[i] = temp[index];
        }
    }
}

/// \ingroup utils_geometry
/// \brief Transform a mesh with the given transform
USDFFUTILS_API void
transformMesh(Mesh& mesh, const PXR_NS::GfMatrix4d& transform);

}
