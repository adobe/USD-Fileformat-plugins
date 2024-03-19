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
#include "sbsarDebug.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(FILE_FORMAT_SBSAR, "Loading sbsar file format");
    TF_DEBUG_ENVIRONMENT_SYMBOL(SBSAR_PACKAGE_RESOLVER, "Resolving assets in package");
    TF_DEBUG_ENVIRONMENT_SYMBOL(SBSAR_RENDER, "Rendering maps from sbsar");
}

PXR_NAMESPACE_CLOSE_SCOPE
