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

#include <fbxsdk.h>
#include <iostream>
#include <map>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/pxr.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/hermiteCurves.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/nurbsCurves.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <vector>
