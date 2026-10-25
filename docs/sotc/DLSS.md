# DLSS Super Resolution no SotC-PC

NVIDIA DLSS opcional (DLAA e Super Resolution), que substitui o passe FSR 1 quando as DLLs
da ponte estao ao lado de `bb-probe.exe`. O port carrega NGX em tempo de execucao, atraves
de uma ponte separada. Falhas retornadas pelas APIs mantem FSR 1; isso nao recupera uma
violacao de acesso dentro de NGX ou do driver.

## Componentes

| Local | Funcao |
|---|---|
| `gpu/dlss_bridge/` | `sotc_dlss.dll`, MIT, somente MSVC, compilada separadamente; unica parte que inclui `nvsdk_ngx_*.h`. A ABI 1 e somente SR; o caminho DLSS-G usa Streamline, nao incluido no ZIP atual. |
| `vk_dlss.cpp/.h` | Carrega ponte e `nvngx_dlss.dll`, negocia extensoes Vulkan, cria e avalia a feature. `SOTC_DLSS=0` impede essa inicializacao. Recusa handles de comando nulos/internos. |
| `host_passes/dlss_upscaler.cpp` | Converte cor para RGBA16F e profundidade para R32F; usa o scheduler para gravar criacao/avaliacao NGX; devolve uma view RGBA16F para o pos-processamento. |
| `vk_recorder.cpp` / `vk_scheduler.cpp` | Executa o callback NGX em ordem na thread proprietaria do comando real; espera pelo resultado antes de liberar os parametros ou escolher fallback. |
| `vk_dlssg.cpp` | Compartilha profundidade, motion vectors, jitter e reset do G-buffer com DLSS SR por `Upscaling()`/`GetUpscaleInputs()`; `Capturing()` captura tambem quando apenas SR esta ativo. |
| `vk_presenter.cpp` | Usa a view DLSS quando disponivel; caso contrario, mantem o caminho FSR 1. |

## Fluxo na apresentacao

```text
game image (render size, R8G8B8A8Srgb / B8G8R8A8Srgb / A2R10G10B10)
        |  dlss_color.comp: UNORM/sRGB -> RGBA16F
        v
   colour RGBA16F -+
   depth  R32F    -+--> DLSS (NGX) --> RGBA16F (presentation size)
   motion R16G16  -+                         |
   jitter/reset   -+                         v
                                      pp_pass -> swapchain
```

- **Cor:** DLSS SDR espera valores display-referred (sRGB). O sampler decodifica a imagem
  sRGB e o shader a recodifica; A2R10G10B10 e lido por uma view UNORM sem essa conversao.
  O pos-processamento trata a saida como sRGB (`srgb_input = 1`), como no caminho FSR de 10 bits.
- **Profundidade:** o jogo usa reversed-Z. O shader grava `1 - d` em R32F, com zero no plano
  proximo, por isso `depth_inverted = false`.
- **Motion vectors:** o G-buffer guarda `curUV - prevUV`; `mv_scale_x/y` sao largura/altura
  da renderizacao, e a feature usa `MVLowRes`.
- **Jitter:** o jitter TAA do jogo em pixels, compartilhado com DLSS-G.
- **Reset:** cortes de camera, primeira criacao e primeiro quadro apos pular DLSS por falta
  de dados, HDR ou selecao de outro upscaler.

### Gravacao em thread (0.65)

Com gravacao em thread, o produtor usa handles internos traduzidos pelos thunks Vulkan-Hpp.
NGX carrega entrypoints Vulkan diretamente e nao passa por essa traducao. Por isso, nao pode
receber o handle do produtor nem gravar em paralelo com o consumidor.

`Scheduler::RecordSynchronous` coloca criacao e avaliacao em um callback depois das conversoes
e barreiras anteriores. O consumidor entrega o `VkCommandBuffer` real ao callback. O produtor
espera a execucao e a destruicao das capturas antes de ler o resultado e gravar a transicao de
saida ou o fallback. E uma espera de CPU, nao um novo `deviceWaitIdle`. Com handle real, o
callback executa diretamente na thread chamadora, inclusive sem gravacao em thread.

## Configuracao

| Variavel / UI | Efeito |
|---|---|
| `SOTC_DLSS=0` | Impede NGX para a sessao; DLSS selecionado usa FSR 1. |
| `SOTC_DLSS=1` | Habilita quando ha DLLs e RTX compativel (padrao). |
| `SOTC_UPSCALER=dlss/fsr1/bilinear` | Selecao inicial, compartilhada com os menus. FSR 1/bilinear nao usa SR, mas permite inicializar NGX para trocar para DLSS ao vivo. |
| `SOTC_RENDER_RES=WxH` / `SOTC_OUTPUT_RES=WxH` | Resolucao do jogo e resolucao de apresentacao; DLSS amplia entre elas. |
| Menu > Configuracoes rapidas > Upscaler | DLSS / FSR 1 / Bilinear, aplicado no proximo quadro. |
| Menu > Nitidez (RCAS) | Tambem controla nitidez DLSS (0..1); ao contrario da atenuacao RCAS, maior significa mais nitidez. |

Configuracoes novas usam `dlss`; escolhas existentes `fsr1`/`bilinear` sao preservadas. DLSS
selecionado sempre deixa FSR 1 disponivel como fallback. Sem `SOTC_UPSCALER`, um `BB_FSR1=0`
explicito seleciona bilinear; caso contrario, DLSS e preferido. `SOTC_DLSS=0` impede ate a
inicializacao NGX, nao e apenas o seletor de upscaler.

## Fallbacks

- DLLs ausentes, GPU nao NVIDIA, falha de inicializacao/capacidade NGX ou extensoes ausentes.
- Sem dados G-buffer de profundidade/movimento em menus, filmes e carregamento: usa FSR 1.
- Criacao/avaliacao retorna falha: registra o motivo e desativa SR pelo resto da sessao.
- Resolucao ou parametros da feature mudam depois da criacao: mantem SR desligado ate reiniciar,
  pois reconstruir a feature enquanto a GPU ainda pode usa-la nao e seguro.
- Saida HDR: mantem FSR 1, pois o buffer de 10 bits esta em PQ e DLSS precisaria de valores lineares.

Esses caminhos nao garantem recuperacao de um acesso invalido no driver. A queda RTX 3070
relatada e o contorno FSR 1 estao documentados em
[CRASH-NVIDIA-2026-10-25.md](CRASH-NVIDIA-2026-10-25.md). A 0.65 corrige o handoff do comando;
o jogador confirmou criacao DLAA 1080p, cerca de 7,5 minutos com FSR 3 e saida 0. Sessoes
longas e qualidade visual ainda nao estao validadas; a proxima prioridade e estabilidade/CPU.

## Verificar uma execucao

1. Comece com geracao de quadros desligada e DLSS selecionado; preserve as demais opcoes.
2. `SOTC DLSS: ready` em `user/last_run.log` significa somente que DLLs e NGX inicializaram.
   Tambem aparece com FSR 1 selecionado; sozinho nao verifica uso de DLSS.
3. Na 0.65, `creating ... (real command buffer)` indica a tentativa na fronteira NGX. A linha
   seguinte `SOTC DLSS: DLSS WxH -> WxH, quality N` confirma a criacao, nao a qualidade visual.
   `feature creation failed` / `evaluation failed` indica falha retornada e fallback para FSR 1.
4. Entre no jogo, onde ha G-buffer. Na resolucao nativa, espere DLAA com HUD incluido; com
   `SOTC_RENDER_RES=1280x720` e `SOTC_OUTPUT_RES=1920x1080`, compare DLSS com FSR 1 e bilinear.
   Teste camera, movimento, cortes e nitidez Alta/Media/Baixa/Desligada. Nitidez DLSS deve diminuir.
5. Troque FSR 1/bilinear para DLSS no F1, volte e confira os menus/filmes. A selecao persiste
   ao fechar e abrir o lancador; sem inputs DLSS, seu fallback deve continuar disponivel.
6. Saia normalmente. NGX deve encerrar antes do device Vulkan em `Instance::~Instance`.
7. Se cair, guarde log e dump da mesma sessao. `GPU context` pode identificar
   `NGX DLSS CreateFeature` ou `NGX DLSS Evaluate` na thread real. Nao envie jogo/firmware.

O teste automatizado com driver simulado cobre o gravador de producao sem GPU/NGX: ordem,
handles, thread proprietaria, resultados e vida das capturas. Nao valida parametros NGX,
sincronizacao da GPU, qualidade visual ou estabilidade no jogo.

## Compilar

O CI compila a ponte com MSVC (`.github/workflows/build-windows.yml`) e copia
`out/dlss-bridge/sotc_dlss.dll` e `nvngx_dlss.dll` para o pacote. Localmente:

```bash
cmake -S gpu/dlss_bridge -B out/dlss-bridge -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSOTC_DLSS_SDK_ROOT=<NVIDIA/DLSS checkout> \
      -DVULKAN_INCLUDE=<Vulkan-Headers include>
cmake --build out/dlss-bridge
```

A ponte fica fora do CMake principal: `bbgpu` usa Clang e nao deve receber as bibliotecas
NGX exclusivas de MSVC.
