# EmuOrbit PS2 — ARMSX2 native source

Fonte do componente PlayStation 2 usado no EmuOrbit Advance: ARMSX2/PCSX2
2.7.2, as modificações de integração e a ponte Android JNI/Vulkan.
Este repositório não é o código completo do aplicativo Android.

O objetivo é disponibilizar o componente nativo e sua receita verificável,
com o mínimo de código do EmuOrbit necessário para reconstruí-lo.
Não contém BIOS, ROMs, saves pessoais, chaves de assinatura, banco de dados,
analytics, telas ou imagens privadas do aplicativo. A BIOS deve ser fornecida
separadamente por quem executar o componente; não é necessária para compilá-lo.

## Obter a fonte

```bash
git -c core.longpaths=true clone --recurse-submodules https://github.com/MateusSouzaAlves/glp-ps2.git
cd glp-ps2
```

Use o clone recursivo: o ZIP automático do GitHub não inclui as fontes dos
submódulos. A receita atual é identificada pela tag `emuorbit-0.3.15-ps2-source-v2`;
a tag inicial continua preservada.

Os submódulos fixam revisões, não acompanham a branch mais recente:

| Fonte | Revisão |
| --- | --- |
| ARMSX2/PCSX2 | `ae93e16a0cc4bad8d4679312e921b158b70a8131` |
| glslang | `09c541ee5b22bbac307987b50d86ec2b4f683d75` |
| SPIRV-Headers | `465055f6c9128772e20082e893d974146acf7a02` |
| SPIRV-Tools | `e4bceacf59fdfe742047e94e41ef65a48999a0dd` |

Os checkouts upstream conservam suas fontes e avisos originais. A receita
compila o core Libretro ARM64 e a ponte, não a interface do app oficial.
O checkout upstream contém outros alvos históricos, que não são distribuídos
por essa receita. A fonte pública do EmuOrbit aqui fica limitada aos arquivos
inventariados em `component-source-manifest.json`.

## Compilar

Pré-requisitos: Windows, PowerShell 7, Git, Android SDK, NDK **29.0.14206865**,
CMake/Ninja **3.22.1**, Python 3. Use caminhos ASCII curtos para checkout/cache.

```powershell
./scripts/build-public-core.ps1 `
  -CacheDir 'D:/armsx2-public-build' `
  -SdkDir 'C:/Users/SEU_USUARIO/AppData/Local/Android/Sdk' `
  -Jobs 4
```

O cache deve ficar fora deste checkout. O script verifica as revisões e os
hashes dos arquivos, compila Android ARM64/API 26 com `c++_static`, e produz:

- `dist/libarmsx2_core.so`: core Libretro com as derivações usadas no produto.
- `dist/libemuorbit_armsx2.so`: ponte JNI/Vulkan.
- `dist/resources`: shaders, GameIndex, patches e fontes inventariados.

Os hashes de resources/avisos upstream desta entrega foram inventariados com
checkout Windows/CRLF. A receita fixa `core.autocrlf=true` somente nos novos
caches upstream, sem mudar a configuração global nem os bytes das entradas
copiadas do EmuOrbit. Use um cache novo se um cache anterior tiver sido criado
com outra política de finais de linha; não altere os hashes para ocultar isso.

Não é gerado APK/AAB, não é embutida BIOS, e nenhum executável do app oficial
é usado como entrada de build. Os probes de diagnóstico existem porque a
receita original os declara, mas ficam fora dos dois alvos de entrega.
As instruções e hashes de reconstrução do componente não certificam
compatibilidade de todos os jogos nem funcionamento em todos os aparelhos.

## Integração e alterações

- Libretro com roots explícitos para resources, BIOS e armazenamento externo
  fornecido pelo frontend; sem varredura de BIOS ou ROMs pessoais.
- Contrato de pausa, troca de Surface, fila PCM limitada, controles e teardown.
- Escrita de estados e ajuste de conversão PCM/avisos de OSD para mobile.
- `bindings/` contém somente as nove declarações JNI, extraídas da classe real,
  com visibilidade pública para consulta. Não é a implementação do frontend Java.
- Os três arquivos no caminho `nintendo3dscore/src/main/cpp` são apenas a
  referência de host Vulkan e sua ABI usada para derivar a ponte PS2; não
  incluem o core 3DS nem outras partes do aplicativo.

As derivações CMake mantêm o checkout upstream intacto e validam as âncoras e
hashes antes de gerar código modificado. Conserve nomes de pacote/métodos JNI
ou adapte os exports e as chamadas juntos. O frontend deve serializar chamadas,
fornecer uma Surface válida, instalar resources e disponibilizar BIOS autorizada
e uma imagem de jogo própria. A interface Libretro também pode ser utilizada
independentemente da ponte específica do EmuOrbit.

`python scripts/verify-public-source.py` confere as entradas exatas e os pins;
`python scripts/test-public-source.py` executa os testes negativos de hash/path.
A configuração Git preserva os bytes originais, inclusive finais de linha.

Validação de 03/10/2026: build limpa dos dois alvos, 33 recursos, nove declarações
JNI compiladas e testes negativos aprovados. Os cinco `.cpp` derivados são
idênticos aos do produto e os exports conferem (29 core / nove ponte).
Os ELFs reconstruídos não têm o mesmo SHA-256 do APK; não se declara reprodução
binária byte a byte nem teste de gameplay nesta cópia. Registro em
`verification.json`. Binários e caches de build não são publicados.

## Licenças e escopo

ARMSX2/PCSX2 e os arquivos da ponte identificados como GPL são disponibilizados
sob **GPL-3.0-or-later**, conforme os avisos originais. O texto está em `LICENSE`.
Scripts/declarações auxiliares com SPDX MIT conservam essa licença. As
dependências e fontes têm suas próprias licenças, preservadas nos submódulos
e relacionadas em [NOTICES.md](NOTICES.md).

A disponibilidade deste componente não é uma certificação de que publicar
somente um core encerra todas as obrigações de qualquer aplicativo que o
combine. A GPL também trata interfaces, modificações, scripts e o escopo do
trabalho combinado; uma distribuição final deve avaliar esse escopo e os
avisos efetivamente empacotados. Este repositório não relicencia arquivos de
terceiros e não declara o aplicativo privado inteiro licenciado pela GPL.

Referências: [GPLv3](https://www.gnu.org/licenses/gpl.en.html) e
[FAQ de ligação da GNU](https://www.gnu.org/licenses/gpl-faq.en.html#GPLStaticVsDynamic).


## Component source refresh — 2026-10-24

This tip updates the component sources/recipes identified in the source
snapshot manifest. Older tags/commits remain historical snapshots. It does
not assert that this entire current snapshot was the source of an earlier
Play release, and does not certify the closed combined app or publish its
remaining private host. References above to older artifact hashes or private
application tags retain historical scope; they are not publicly available
host-source substitutes. See SOURCE_REFRESH.md and releases/component-source-2026-10-24/manifest.json.
