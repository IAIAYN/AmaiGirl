# Third-Party Licenses

<p><a href="THIRD_PARTY_LICENSES.md#zh-cn">简体中文</a> | <a href="THIRD_PARTY_LICENSES.en.md#en-us">English</a></p>

<a id="en-us"></a>

## English

This document lists third-party components used by this project and their license terms.

> Note: This file documents third-party dependencies and compliance boundaries only.

### 1) Qt 6

- **Project**: Qt Framework
- **Used modules (from current CMake config)**:
  - Qt6::Core
  - Qt6::Gui
  - Qt6::Widgets
  - Qt6::OpenGL
  - Qt6::OpenGLWidgets
  - Qt6::Network
  - Qt6::Multimedia
  - Qt6::Svg
- **License model**:
  - Open-source usage in this project is typically under LGPL v3 (this project is not currently using GPL-based modules/components).
  - You can obtain the source code for relevant modules/components here: https://download.qt.io/official_releases/qt/
- **Upstream license information**:
  - https://www.qt.io/development/open-source-lgpl-obligations
  - https://www.gnu.org/licenses/lgpl-3.0.html

#### Qt compliance notes (for binary distribution)

- Keep Qt license texts and attribution notices available to users.
- Do not impose terms that conflict with LGPL rights.
- For LGPL usage, dynamic linking is generally easier to comply with than static linking.
- If any GPL-only Qt component is used, distribution obligations may escalate to GPL.

### 2) Live2D Cubism SDK

- **Project**: Cubism SDK for Native
- **Download page**:
  - https://www.live2d.com/en/sdk/about/
- **Used module in this project**:
  - Cubism Core
- **Current integration in this project**: linked via `libLive2DCubismCore.a` in CMake.
- **License**: Live2D Proprietary Software License (not open-source).
- **English license link**:
  - https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html
- **Redistributable files list**:
  - `CubismSdkForNative-5-r.4.1/Core/RedistributableFiles.txt`

#### Live2D compliance notes

- Live2D components are under proprietary terms and are **not** relicensed by this project.
- Distribution/publishing may require separate eligibility or agreements under Live2D terms.
- Only redistribute files allowed by Live2D terms and `RedistributableFiles.txt`.

### 3) Live2D model assets in `res/models`

- Model files and textures in `res/models` are third-party assets and may have separate use terms.
- The `Hiyori` model requires acceptance of the Free Material License Agreement (English link):
  - https://www.live2d.com/eula/live2d-free-material-license-agreement_en.html
- Before public redistribution (repository or release artifacts), verify each model's license/terms.
- If terms are unclear, remove these assets from public distribution and provide a user-side download step.

### 4) Ownership and licensing boundaries

- Original source code of this project is licensed by the maintainer via the root `LICENSE` (Apache-2.0).
- Third-party components listed here keep their own licenses.
- Their trademarks, copyrights, and other rights belong to their respective owners.

---

If this file is out of date with actual dependencies, please update it together with build/dependency changes.
