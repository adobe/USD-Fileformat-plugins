/*
Copyright 2026 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include <fileformatutils/featureFlags.h>

#ifndef USD_FILEFORMATS_DEFAULT_WRITE_USDPREVIEWSURFACE
#define USD_FILEFORMATS_DEFAULT_WRITE_USDPREVIEWSURFACE true
#endif

#ifndef USD_FILEFORMATS_DEFAULT_WRITE_ASM
#define USD_FILEFORMATS_DEFAULT_WRITE_ASM true
#endif

#ifndef USD_FILEFORMATS_DEFAULT_WRITE_OPENPBR
#define USD_FILEFORMATS_DEFAULT_WRITE_OPENPBR false
#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(USD_FILEFORMATS_WRITE_USDPREVIEWSURFACE,
                      (bool)(USD_FILEFORMATS_DEFAULT_WRITE_USDPREVIEWSURFACE),
                      "Write UsdPreviewSurface material networks by default");

TF_DEFINE_ENV_SETTING(USD_FILEFORMATS_WRITE_ASM,
                      (bool)(USD_FILEFORMATS_DEFAULT_WRITE_ASM),
                      "Write AdobeStandardMaterial networks by default");

TF_DEFINE_ENV_SETTING(USD_FILEFORMATS_WRITE_OPENPBR,
                      (bool)(USD_FILEFORMATS_DEFAULT_WRITE_OPENPBR),
                      "Write OpenPBR / MaterialX material networks by default");

TF_DEFINE_ENV_SETTING(USD_FILEFORMATS_EXPERIMENTAL_OPENPBR_PROCESSING,
                      false,
                      "Enable experimental OpenPBR processing code paths");

PXR_NAMESPACE_CLOSE_SCOPE

namespace adobe::usd {

void
applyMaterialModelDefaults(bool& writeUsdPreviewSurface, bool& writeASM, bool& writeOpenPBR)
{
    writeUsdPreviewSurface =
      PXR_NS::TfGetEnvSetting(PXR_NS::USD_FILEFORMATS_WRITE_USDPREVIEWSURFACE);
    writeASM = PXR_NS::TfGetEnvSetting(PXR_NS::USD_FILEFORMATS_WRITE_ASM);
    writeOpenPBR = PXR_NS::TfGetEnvSetting(PXR_NS::USD_FILEFORMATS_WRITE_OPENPBR);
}

}
