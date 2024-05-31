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
#include "plyImport.h"
#include "debugCodes.h"
#include <array>
#include <common.h>
#include <geometry.h>
#include <happly.h>
#include <images.h>
#include <pxr/base/gf/range3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/tokens.h>
#include <regex>
#include <string>

using namespace PXR_NS;
using namespace happly;

namespace adobe::usd {
template<typename T>
std::vector<T>*
getPropertyDataPtr(happly::Element& element, const std::string& target)
{
    happly::TypedProperty<T>* property =
      dynamic_cast<happly::TypedProperty<T>*>(element.getPropertyPtr(target).get());
    if (property) {
        return &(property->data);
    }
    throw std::runtime_error("PLY import: element " + element.name + " does not have property " +
                             target + " with the specific type.");
}

bool
importPly(const ImportPlyOptions& options, PLYData& ply, UsdData& usd)
{
    // It would be nice to print the ply data format and version but they are private.
    for (const std::string& comment : ply.comments) {
        TF_DEBUG_MSG(FILE_FORMAT_PLY, "Comment: %s\n", comment.c_str());
    }

    std::vector<float>* positionsX = nullptr;
    std::vector<float>* positionsY = nullptr;
    std::vector<float>* positionsZ = nullptr;
    std::vector<float>* nx = nullptr;
    std::vector<float>* ny = nullptr;
    std::vector<float>* nz = nullptr;
    std::vector<float>* u = nullptr;
    std::vector<float>* v = nullptr;
    std::vector<unsigned char>* r = nullptr;
    std::vector<unsigned char>* g = nullptr;
    std::vector<unsigned char>* b = nullptr;
    std::vector<unsigned char>* a = nullptr;

    // These properties are used by Gaussian splats
    std::vector<float>* gsColorCoeff0 = nullptr;
    std::vector<float>* gsColorCoeff1 = nullptr;
    std::vector<float>* gsColorCoeff2 = nullptr;
    std::vector<float>* gsOpacity = nullptr;
    std::vector<float>* gsScale0 = nullptr;
    std::vector<float>* gsScale1 = nullptr;
    std::vector<float>* gsScale2 = nullptr;
    std::vector<float>* gsRotation0 = nullptr;
    std::vector<float>* gsRotation1 = nullptr;
    std::vector<float>* gsRotation2 = nullptr;
    std::vector<float>* gsRotation3 = nullptr;
    std::array<std::vector<float>*, 45> gsSHCoeffs = {};

    auto [meshIndex, mesh] = usd.addMesh();
    mesh.asPoints = options.importAsPoints || !ply.hasElement("face");
    // Will check later. An asset is a Gsplat only if it contains points and has all the Gsplat-related fields.
    mesh.asGsplats = mesh.asPoints;

    try {
        Element& element = ply.getElement("vertex");
        // happly provides plyIn.getVertexPositions(), but it uses double, so avoid it to avoid
        // extra work.
        try {
            positionsX = getPropertyDataPtr<float>(element, "x");
            positionsY = getPropertyDataPtr<float>(element, "y");
            positionsZ = getPropertyDataPtr<float>(element, "z");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid position data: %s\n", e.what());
            return false;
        }
        try {
            nx = getPropertyDataPtr<float>(element, "nx");
            ny = getPropertyDataPtr<float>(element, "ny");
            nz = getPropertyDataPtr<float>(element, "nz");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid normal data: %s\n", e.what());
        }
        try {
            u = getPropertyDataPtr<float>(element, "texture_u");
            v = getPropertyDataPtr<float>(element, "texture_v");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid uv data: %s\n", e.what());
        }
        try {
            r = getPropertyDataPtr<unsigned char>(element, "red");
            g = getPropertyDataPtr<unsigned char>(element, "green");
            b = getPropertyDataPtr<unsigned char>(element, "blue");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid color data: %s\n", e.what());
        }
        try {
            a = getPropertyDataPtr<unsigned char>(element, "alpha");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid alpha color data: %s\n", e.what());
        }
        if (element.hasProperty("f_dc_0") && element.hasProperty("f_dc_1") &&
            element.hasProperty("f_dc_2")) {
            try {
                gsColorCoeff0 = getPropertyDataPtr<float>(element, "f_dc_0");
                gsColorCoeff1 = getPropertyDataPtr<float>(element, "f_dc_1");
                gsColorCoeff2 = getPropertyDataPtr<float>(element, "f_dc_2");
            } catch (std::exception& e) {
                TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid Gaussian splatting color data: %s\n", e.what());
                mesh.asGsplats = false;
            }
        } else {
            mesh.asGsplats = false;
        }
        if (mesh.asGsplats && element.hasProperty("scale_0") && element.hasProperty("scale_1") &&
            element.hasProperty("scale_2")) {
            try {
                gsScale0 = getPropertyDataPtr<float>(element, "scale_0");
                gsScale1 = getPropertyDataPtr<float>(element, "scale_1");
                gsScale2 = getPropertyDataPtr<float>(element, "scale_2");
            } catch (std::exception& e) {
                TF_DEBUG_MSG(
                  FILE_FORMAT_PLY, "Invalid Gaussian splatting scaling data: %s\n", e.what());
                mesh.asGsplats = false;
            }
        } else {
            mesh.asGsplats = false;
        }
        if (mesh.asGsplats && element.hasProperty("rot_0") && element.hasProperty("rot_1") &&
            element.hasProperty("rot_2") && element.hasProperty("rot_3")) {
            try {
                gsRotation0 = getPropertyDataPtr<float>(element, "rot_0");
                gsRotation1 = getPropertyDataPtr<float>(element, "rot_1");
                gsRotation2 = getPropertyDataPtr<float>(element, "rot_2");
                gsRotation3 = getPropertyDataPtr<float>(element, "rot_3");
            } catch (std::exception& e) {
                TF_DEBUG_MSG(
                  FILE_FORMAT_PLY, "Invalid Gaussian splatting rotation data: %s\n", e.what());
                mesh.asGsplats = false;
            }
        } else {
            mesh.asGsplats = false;
        }
        if (mesh.asGsplats && element.hasProperty("opacity"))
        {
            try {
                gsOpacity = getPropertyDataPtr<float>(element, "opacity");
            } catch (std::exception& e) {
                TF_DEBUG_MSG(
                  FILE_FORMAT_PLY, "Invalid Gaussian splatting opacity data: %s\n", e.what());
                mesh.asGsplats = false;
            }
        } else {
            mesh.asGsplats = false;
        }
        for (int i = 0; mesh.asGsplats && i < 45; ++i)
        {
            std::string propName = std::string("f_rest_") + std::to_string(i); 
            if (!element.hasProperty(propName)) {
                mesh.asGsplats = false;
                break;
            }
            
            try {
                gsSHCoeffs[i] = getPropertyDataPtr<float>(element, propName);
            } catch (std::exception& e) {
                TF_DEBUG_MSG(
                  FILE_FORMAT_PLY, "Invalid Gaussian splatting SH data: %s\n", e.what());
                mesh.asGsplats = false;
            }
        }
    } catch (std::exception& e) {
        TF_DEBUG_MSG(FILE_FORMAT_PLY, "Could not find vertex element %s\n", e.what());
        mesh.asGsplats = false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_PLY,
                 "Importing as points: %s, width: %f\n",
                 mesh.asPoints ? "true" : "false",
                 options.pointWidth);

    mesh.points.resize(positionsX->size());
    for (size_t i = 0; i < mesh.points.size(); i++) {
        mesh.points[i][0] = (*positionsX)[i];
        mesh.points[i][1] = (*positionsY)[i];
        mesh.points[i][2] = (*positionsZ)[i];
    }

    if (nx && nx->size()) {
        mesh.normals.values.resize(nx->size());
        for (size_t i = 0; i < mesh.normals.values.size(); i++) {
            mesh.normals.values[i][0] = (*nx)[i];
            mesh.normals.values[i][1] = (*ny)[i];
            mesh.normals.values[i][2] = (*nz)[i];
        }
        mesh.normals.interpolation = UsdGeomTokens->vertex;
    }

    if (u && u->size()) {
        mesh.uvs.values.resize(u->size());
        for (size_t i = 0; i < mesh.uvs.values.size(); i++) {
            mesh.uvs.values[i][0] = (*u)[i];
            mesh.uvs.values[i][1] = (*v)[i];
        }
        mesh.uvs.interpolation = UsdGeomTokens->vertex;
    }

    // Prioritize Gsplat loading, if there're properties for colors.
    // Both the Gsplat and regular point cloud have definitions to color,
    // but with different names and conversions.
    if (mesh.asGsplats && gsColorCoeff0->size()) {
        auto [colorIndex, colors] = usd.addColorSet(meshIndex);
        colors.interpolation = UsdGeomTokens->vertex;
        colors.values.resize(gsColorCoeff0->size());

        // The 0th-order coefficient of spherical harmonics, which
        // is 1/sqrt(4*pi).
        constexpr float shC0 = 0.28209479177387814f;
        for (size_t i = 0; i < colors.values.size(); i++) {
            colors.values[i][0] = std::clamp((*gsColorCoeff0)[i] * shC0 + 0.5f, 0.0f, 1.0f);
            colors.values[i][1] = std::clamp((*gsColorCoeff1)[i] * shC0 + 0.5f, 0.0f, 1.0f);
            colors.values[i][2] = std::clamp((*gsColorCoeff2)[i] * shC0 + 0.5f, 0.0f, 1.0f);
        }
    } else if (r && r->size()) {
        auto [colorIndex, colors] = usd.addColorSet(meshIndex);
        colors.interpolation = UsdGeomTokens->vertex;
        colors.values.resize(r->size());
        for (size_t i = 0; i < colors.values.size(); i++) {
            colors.values[i][0] = static_cast<float>((*r)[i]) / 255.0f;
            colors.values[i][1] = static_cast<float>((*g)[i]) / 255.0f;
            colors.values[i][2] = static_cast<float>((*b)[i]) / 255.0f;
        }
    }

    // Prioritize Gsplat loading, if there're properties for opacity.
    // Both the Gsplat and regular point cloud have definitions to opacity,
    // but with different names and conversions.
    if (mesh.asGsplats && gsOpacity->size()) {
        auto [opacityIndex, opacity] = usd.addOpacitySet(meshIndex);
        opacity.interpolation = UsdGeomTokens->vertex;
        opacity.values.resize(gsOpacity->size());
        for (size_t i = 0; i < opacity.values.size(); i++) {
            opacity.values[i] = 1.0f / (1.0f + std::exp(-(*gsOpacity)[i]));
        }
    } else if (a && a->size()) {
        auto [opacityIndex, opacity] = usd.addOpacitySet(meshIndex);
        opacity.interpolation = UsdGeomTokens->vertex;
        opacity.values.resize(a->size());
        for (size_t i = 0; i < opacity.values.size(); i++) {
            opacity.values[i] = static_cast<float>((*a)[i]) / 255.0f;
        }
    }

    // Prioritize Gsplat loading, if there're properties for point width.
    // Otherwise we use a constant value from option.
    if (mesh.asPoints) {
        if (mesh.asGsplats && gsScale0->size()) {
            mesh.pointWidths.resize(gsScale0->size());
            for (size_t i = 0; i < mesh.pointWidths.size(); i++) {
                mesh.pointWidths[i] = std::exp((*gsScale0)[i]) * 2.0f;
            }

            auto [width1Index, widths1] = usd.addExtraPointWidthSet(meshIndex);
            widths1.interpolation = UsdGeomTokens->vertex;
            widths1.values.resize(gsScale1->size());
            for (size_t i = 0; i < widths1.values.size(); i++) {
                widths1.values[i] = std::exp((*gsScale1)[i]) * 2.0f;
            }

            auto [width2Index, widths2] = usd.addExtraPointWidthSet(meshIndex);
            widths2.interpolation = UsdGeomTokens->vertex;
            widths2.values.resize(gsScale2->size());
            for (size_t i = 0; i < widths2.values.size(); i++) {
                widths2.values[i] = std::exp((*gsScale2)[i]) * 2.0f;
            }
        } else {
            mesh.pointWidths.resize(mesh.points.size());
            for (size_t i = 0; i < mesh.pointWidths.size(); i++) {
                mesh.pointWidths[i] = options.pointWidth;
            }
        }
    }

    if (!mesh.asPoints) {
        try {
            std::vector<std::vector<size_t>> indices = ply.getFaceIndices<size_t>();
            mesh.faces.resize(indices.size());
            for (size_t i = 0; i < indices.size(); i++) {
                mesh.faces[i] = indices[i].size();
                for (size_t j = 0; j < indices[i].size(); j++) {
                    mesh.indices.push_back(indices[i][j]);
                }
            }
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid index data: %s\n", e.what());
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Creating triangulation indices\n");
            createTriangulationIndices(mesh);
        }
    }

    // Load other Gsplat-specific attributes.
    if (mesh.asGsplats) {
        mesh.pointRotations.interpolation = UsdGeomTokens->vertex;
        mesh.pointRotations.values.resize(gsRotation0->size());
        for (size_t i = 0; i < mesh.pointRotations.values.size(); i++) {
            mesh.pointRotations.values[i].SetReal((*gsRotation0)[i]);
            mesh.pointRotations.values[i].SetImaginary(
              (*gsRotation1)[i], (*gsRotation2)[i], (*gsRotation3)[i]);
            mesh.pointRotations.values[i] = mesh.pointRotations.values[i].GetNormalized();
        }

        for (std::size_t shIndex = 0; shIndex < gsSHCoeffs.size(); ++shIndex)
        {
            auto [shCoeffIndex, shCoeffs] = usd.addPointSHCoeffSet(meshIndex);
            shCoeffs.interpolation = UsdGeomTokens->vertex;
            shCoeffs.values.resize((*gsSHCoeffs[shIndex]).size());
            for (size_t i = 0; i < shCoeffs.values.size(); i++) {
                shCoeffs.values[i] = (*gsSHCoeffs[shIndex])[i];
            }
        }
    }

    auto [nodeIndex, node] = usd.addNode(-1);
    node.staticMeshes.push_back(meshIndex);

    if (options.importWithUpAxisCorrection) {
        // We filter out useful convention info from the comment.
        bool useZup = false;

        // The input source is probably Z-up if the comment contains these words. 
        const std::vector<std::regex> zUpTokens = { 
            std::regex("\\bZ-axis up\\b"), 
            std::regex("\\bBlender\\b"), 
            std::regex("\\bArtec\\b"), 
            std::regex("\\bRhinoceros\\b")
        };

        for (const std::string& comment : ply.comments) 
        {
            if (!useZup) {
                for (const std::regex& pattern : zUpTokens) {
                    if (std::regex_search(comment, pattern)) {
                        useZup = true;
                        break;
                    }
                }            
            }
        }

        if (useZup)
            usd.upAxis = UsdGeomTokens->z;
        else
            usd.upAxis = UsdGeomTokens->y;
    }

    if (mesh.asGsplats && options.importGsplatWithClipping) 
    {
        // We apply a clipping box from -2 to 2 for Gsplat, to avoid
        // rendering the low quality splats far from the reconstruction
        // center. This range will be part of the USD asset and can be
        // adjusted on-the-fly.
        mesh.clippingBox.values.resize(2);
        mesh.clippingBox.values[0] = PXR_NS::GfVec3f(-2.0f, -2.0f, -2.0f);
        mesh.clippingBox.values[1] = PXR_NS::GfVec3f(2.0f, 2.0f, 2.0f);
        mesh.clippingBox.interpolation = UsdGeomTokens->constant;
    }
    return true;
}

}