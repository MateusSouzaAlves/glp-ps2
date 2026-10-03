# Avisos e fontes preservados

Este inventário se refere ao core ARM64 e à ponte dessa receita. Não é uma
certificação jurídica do aplicativo Android completo. Os textos e copyrights
originais permanecem nos quatro submódulos fixados; não foram substituídos por
uma licença genérica do EmuOrbit. Abra os links locais após clonar com submódulos.

## Core e integração

- ARMSX2/PCSX2: GPL-3.0-or-later, [texto completo](LICENSE) e avisos por arquivo
  em `sources/armsx2`. Main Libretro: copyright 2002–2026 PCSX2 Dev Team.
- Ponte e derivações EmuOrbit: os arquivos com SPDX GPL-3.0-or-later mantêm essa
  licença, incluindo a referência Vulkan reutilizada e a interface JNI mínima.
  As alterações de integração desta entrega são de 2026 e estão na receita
  CMake, nos fragmentos `.inc` e no host nativo; não modificam o checkout upstream.
- A ABI Vulkan Libretro e auxiliares marcados MIT conservam seus avisos e
  copyrights. Headers originais estão em `sources/armsx2/3rdparty/libretro`.
  A ABI mínima em `nintendo3dscore/src/main/cpp/libretro_vulkan_abi.h` deriva
  do RetroArch `81478f2aa2abb942cfacb2109cbc25a4bd3b46ca`; o aviso MIT
  completo, copyright (C) 2010–2020 The RetroArch team, é preservado pelo
  mesmo texto MIT abaixo, também presente no header Libretro original.
- Scripts públicos novos marcados MIT: copyright 2026 Mateus Souza Alves.
  Texto da licença MIT para esses scripts ao final deste documento.

## Dependências da receita

Base dos caminhos abreviados abaixo:
`sources/armsx2/platforms/android/app/src/main/cpp/3rdparty/`.
O link final também contém `common` e `pcsx2`, pertencentes ao mesmo upstream.
Os caminhos de licença identificam os textos originais, não novas licenças.

| Componente | Aviso/fonte original no checkout |
| --- | --- |
| shaderc | `shaderc/LICENSE`, Apache-2.0 |
| glslang | `sources/dependencies/glslang/LICENSE.txt` e avisos por arquivo |
| SPIRV-Headers | `sources/dependencies/spirv-headers/LICENSE` e avisos por arquivo |
| SPIRV-Tools | `sources/dependencies/spirv-tools/LICENSE`, Apache-2.0 |
| Vulkan headers | `vulkan/LICENSE.md`, `vulkan/include/LICENSE.txt` e copyrights por header |
| GLAD | avisos SPDX e textos incorporados aos arquivos `glad/include/glad/` e `glad/src/` |
| VIXL | `vixl/LICENCE`, BSD de três condições |
| libwebp / sharpyuv / cpu features | `libwebp/COPYING`, `libwebp/AUTHORS` e avisos por arquivo |
| rapidyaml / c4 | `rapidyaml/`; texto MIT também no agregado upstream `bin/docs/ThirdPartyLicenses.html`, seção Rapid YAML |
| ImGui | `imgui/LICENSE.txt`, MIT |
| fmt | `fmt/LICENSE`, MIT |
| libchdr | `libchdr/LICENSE.txt` e avisos dos leitores/decodificadores internos |
| libzip | `libzip/LICENSE`, BSD de três condições |
| cpuinfo / clog | `cpuinfo/LICENSE` e avisos de `cpuinfo/deps/clog/` |
| cubeb / código interno de áudio | `cubeb/LICENSE` e avisos por arquivo; preservar os avisos de speex/resampler |
| rcheevos | `rcheevos/LICENSE`, MIT |
| discord-rpc | `discord-rpc/LICENSE`, MIT; aparece no link mesmo com frontend Discord desativado |
| simpleini | aviso MIT em `simpleini/include/SimpleIni.h` |
| FreeSurround / kissfft | `freesurround/`; FreeSurround SPDX GPL-2.0+, kissfft tem copyright/licença próprios |
| FreeType | `freetype/LICENSE.TXT`, `freetype/docs/FTL.TXT`, `freetype/docs/GPLv2.TXT` e avisos por arquivo |
| SDL3 / código interno | `sdl3/LICENSE.txt` e licenças de fontes internas, como hidapi |
| LZ4 | `lz4/lib/LICENSE`, BSD de duas condições |
| SoundTouch | `soundtouch/COPYING.TXT`, LGPL-2.1; conservar os avisos por fonte |
| libjpeg-turbo | `libjpeg-turbo/LICENSE.md` e textos complementares listados por ele |
| libpng | `libpng/LICENSE.md`, licença libpng |
| LZMA | `lzma/LICENSE.txt` e avisos por fonte |
| zstd | `zstd/LICENSE` e `zstd/COPYING`, opções BSD/GPL preservadas |
| demangler / libiberty | `demangler/DemanglerLicenseGPL.txt`, `DemanglerLicenseLGPL.txt` e avisos por arquivo |
| ccc | copyrights/licença em `ccc/src/ccc/` |
| PlutoSVG / PlutoVG | `plutosvg/LICENSE`, `plutovg/LICENSE`, MIT |
| fast_float (headers) | `fast_float/LICENSE-MIT`, `LICENSE-APACHE`, `LICENSE-BOOST` |

O agregado [ThirdPartyLicenses.html](sources/armsx2/bin/docs/ThirdPartyLicenses.html)
também preserva textos de GLAD, FreeSurround, kissfft, libiberty, rapidyaml,
Font Awesome e outros. Inclui componentes de outros alvos upstream; a presença
de uma seção nesse agregado, sozinha, não prova que ela esteja ligada ao core.
Os fontes completos da receita e dependências estão disponíveis nos submódulos.

O NDK fornece headers e `libc++`/runtime estático. Conserve também os avisos da
distribuição NDK 29.0.14206865 usada ao redistribuir binários, incluindo o texto
Apache-2.0 com LLVM exception onde aplicável. O NDK/SDK são pré-requisitos, não
código privado do aplicativo nem arquivos substituídos por esta entrega.

## Recursos realmente selecionados

`config/ps2-armsx2-resources.json` fixa 33 arquivos com hashes. As fontes e
shaders vêm de `sources/armsx2/bin/resources`; patches vêm do caminho Android
fixado no manifesto. Preservar também os avisos nos shaders FidelityFX/FXAA.

| Fonte | Aviso/licença preservados |
| --- | --- |
| Font Awesome `fa-solid-900.ttf` | copyright Font Awesome; SIL OFL-1.1 para a fonte, texto completo na seção Font Awesome do agregado upstream |
| `promptfont.otf` | `bin/resources/fonts/promptfont-license`, SIL OFL-1.1 |
| `Roboto-Regular.ttf` | nome interno 13: Apache License 2.0; copyright Google; `Roboto-Regular-copyright` preservado |
| `RobotoMono-Medium.ttf` | nomes internos 0/13/14: copyright 2015 The Roboto Mono Project Authors; Apache License 2.0; texto completo Apache em `shaderc/LICENSE` |

O Roboto desta revisão é a fonte antiga Apache-2.0. Não aplicar a ela a licença
OFL de uma versão diferente só porque há uma seção Roboto no agregado. Twemoji
não faz parte dos 33 recursos selecionados (`BUNDLE_EMOJI_FONT=OFF`).
O leitor CHD neutro separado do app não é gerado por esta receita, nem está
incluído aqui como se fosse parte GPL do ARMSX2; seus avisos BSD/MIT permanecem
necessários na distribuição que o utiliza.

## MIT — scripts públicos identificados e ABI Vulkan mínima

Copyright (c) 2026 Mateus Souza Alves

Copyright (C) 2010-2020 The RetroArch team (somente ABI/header Vulkan)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
