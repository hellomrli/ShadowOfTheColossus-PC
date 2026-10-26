# Revisao do shadPS4: estabilidade e CPU (2026-10-26)

## Escopo e versoes

- Port analisado: `a1b340ea8bab2c6688b6ae9811433db86d2d01e2`, v0.66.
- Upstream oficial: [shadps4-emu/shadPS4](https://github.com/shadps4-emu/shadPS4).
- `main` consultado: [2cdefaa1298c6e4ece6c74bd84e90f3906282d51][head], de
  2026-10-25 22:56:16 UTC (2026-10-26 06:56:16 em UTC+8).
- Revisao anterior: `12a4564543ccec527f9723676d2180580d1d7a94`, de 2026-10-24.
- Base vendorizada continua `b4e7ae776071ceb1dd35004577a8719481e1d258`.
  O commit e de 2026-09-24; 2026-10-02 era a data anotada na revisao antiga, nao a data
  de criacao desse commit.

Ha **9 commits novos** desde o limite da revisao anterior. A lista de commits que tocam
`src/video_core` ou `src/shader_recompiler` desde a base passa de 98 para 103. Foram
conferidos os nove novos commits e reexaminados candidatos anteriores nas areas de espera,
sincronizacao e vida dos recursos; isso nao e uma auditoria integral de todos os subsistemas.

**A auditoria inicial foi somente uma revisao de codigo e documentacao; o pacote v0.66
nao foi alterado. Depois dela, a v0.67 adaptou as tres correcoes de prioridade alta.**
O diagnostico e as propostas abaixo descrevem o estado da v0.66, nao novas pendencias da
v0.67. Nao houve benchmark de jogo nem teste com GPU. As dez correcoes da v0.62 e as tres
novas constam de [UPSTREAM-BACKPORTS.md](UPSTREAM-BACKPORTS.md).

## Integracao posterior: v0.67

| Upstream | Commit local | Adaptacao |
|---|---|---|
| #5406 `e6c79a5f` | `b8284e8` | `ce_de_sync.h` concentra os dois guardas usados pelo Liverpool. Nao muda contadores, fila global, resets nem liberacao de coroutines. |
| #5444 `699009c9` | `b1d472e` | `image_content.h` compartilha o predicado ja usado por SafeToDownload e a ordem refresh/copy. Captura a validade antes do refresh; CopyImage local continua responsavel pelas flags. |
| #5435 `a7d0fe3f` | `5d4badc` | `sleep_timer.h` e `win32_sleep_timer.h` reutilizam o timer por thread. Acrescentam fallback bloqueante para falhas, tempo restante monotono e handle separado para espera reentrante em APC. |

Os tres testes portaveis passaram localmente com `-Wall -Wextra -Werror` e ASan/UBSan:

- `test_ce_de_sync.cpp`: limites unsigned, handle ausente/concluido e coroutines pequenas
  com espera aninhada/retomada. Nao executa o Liverpool completo nem buffers PM4 do jogo.
- `test_image_expansion.cpp`: combinacoes de flags, preservacao de mips e ordem de comandos
  adiados com callbacks em memoria. O ramo sem upload de MSAA e modelado; nao ha validacao
  de pixels MSAA/Vulkan, rebinding ou lifetime de imagens no driver.
- `test_sleep_timer.cpp`: reutilizacao, fallback sem alta resolucao, falhas de criar/armar/
  esperar, limites de duracao, interrupcao/reentrada e encerramento TLS. O build Windows
  executa tambem timers reais, APC aninhada e verificacao de handles apos 32 threads.

O build Windows e a auditoria do pacote sao exigidos antes de publicar a pre-release;
os resultados ficam nas notas da release. DLSS/FSR 3, cache de shaders 38, GC2, protecoes
e `SOTC_CP_FREE_TASKS` permanecem inalterados. Streaming/save/load e ganhos de CPU/FPS
continuam dependendo de teste controlado no PC do jogador, conforme o
[guia de estabilidade/CPU](STABILITY-CPU-2026-10-25.md).

## Diagnostico na v0.66: tres correcoes de prioridade alta

### 1. Contadores CE/DE: evitar acesso nulo e espera incorreta

Upstream: [#5406][p5406], commit `e6c79a5fe0ea2f3d03fcb9c5c50cf61af063f3f5`.

O port ainda tem as duas formas antigas em
`gpu/shadps4/video_core/amdgpu/liverpool.cpp:674` e
`gpu/shadps4/video_core/amdgpu/liverpool.cpp:1380`:

- `WaitOnDeCounterDiff` compara `de_count - ce_count`, com inteiros sem sinal.
  Com CE=1, DE=0 e limite=2, isso vira `0xffffffff` e espera, embora CE so esteja um passo
  adiante. O upstream compara CE-DE e verifica primeiro que CE nao esta atrasado.
- `WaitOnCeCounter` chama `ce_task.handle.done()` sem verificar se existe uma coroutine.
  Uma submissao sem CCB pode chegar a esse caminho com handle nulo.

**Proposta:** adaptar os dois guardas como uma correcao independente. Isso nao exige
recompilar o cache de shaders nem importar toda a fila global CE de #5446.

**Testes antes de integrar:** pacotes sem CCB, diferencas abaixo/iguais/acima do limite,
CE atrasado, buffers indiretos, retomada depois de uma espera e encerramento. Conferir os
contadores compartilhados pelo caminho local de draw/recorder; nao mudar sua vida util
no mesmo patch. O fato de a expressao antiga estar presente nao prova que ela causou as
esperas `PS4SyncEvent` do log do jogador.

### 2. ExpandImage: nao sobrescrever um upload novo com a textura antiga

Upstream: [#5444][p5444], commit `699009c94932c64080391cf08d8131fc653ecee8`.

Em `gpu/shadps4/video_core/texture_cache/texture_cache.cpp:661`, o port ainda faz
`RefreshImage(new_image)` e depois `CopyImage(src, new_image)` incondicionalmente. Se o
guest reutilizou a memoria e a origem esta CPU-dirty, o segundo comando recoloca texels
antigos sobre o upload novo. O problema de ordem existe no codigo local; nao foi
reproduzido no SotC nesta revisao.

O commit integrado usa `HasGpuValidData()`, nao apenas a condicao mostrada no texto inicial
da PR. A expressao dele corresponde a `Image::SafeToDownload()` do port
(`gpu/shadps4/video_core/texture_cache/image.h:118`): GPU-modified e nenhum bit Dirty.

**Proposta:** condicionar a preservacao da origem a dados GPU ainda validos. O
`Runtime::CopyImage` local ja propaga `GpuModified` e limpa Dirty; nao duplicar os ajustes
de flags do upstream sem conferir esse caminho.

**Testes antes de integrar:** origem CPU-dirty, MaybeCpuDirty, GpuDirty, GPU-modified limpa
e somente CPU; verificar tanto os dados como flags e ordem upload/copy. Testar MSAA,
rebinding e sobrevivencia dos recursos ate o recorder/GPU terminar. No jogo, repetir
trocas de area e save/load com FSR 1 + FSR 3, sem alterar outras opcoes.

### 3. AccurateSleep no Windows: um timer de alta resolucao por thread

Upstream: [#5435][p5435], commit `a7d0fe3fdd24f154552b4daf819be75b6693620e`.

O caminho C++ em `gpu/shadps4/common/thread.cpp:110` ainda cria e fecha um waitable timer
a cada chamada. `VideoOutDriver::PresentThread` usa esse caminho por `AccurateTimer`
(`gpu/shadps4/core/libraries/videoout/driver.cpp:917`). O upstream reutiliza um timer
thread-local, fecha-o na saida, tenta alta resolucao com fallback e trata interrupcoes.

**Proposta:** absorver a reutilizacao com RAII e testes de falha/encerramento. Preservar
as esperas reais; isso nao significa aumentar spin, prioridade ou numero de workers.

O runtime C ja tem outro cache de timer em `src/compat_win.c:111`; nao confundir os dois
caminhos nem atribuir esta melhoria a todas as threads guest. A PR relata reducao do
custo de chamadas em MediEvil, **sem ganho de FPS naquela medicao**. No port, as chamadas
identificadas sao de videoout; nao e justificativa para prometer grande ganho de CPU/FPS.

**Testes antes de integrar:** reutilizacao do handle, limpeza apos saida da thread,
zero/negativo/sub-100 ns, APC em espera interruptivel, falhas de criacao/armamento e fallback
sem alta resolucao. Comparar `[CPU] videoout`, frametimes e mesmo limite de FPS no Windows.

## Segunda etapa: adaptar e medir, nao copiar isoladamente

| Upstream | Evidencia local e cuidado necessario |
|---|---|
| [#5433][p5433] `d3ff0e0c` - preservar subrecursos ao criar a imagem pai | `texture_cache.cpp:641` so copia o mip se `merged_image_id` ja existe. O upstream cria o pai nesse caminho. Vale adaptar, mas preservar rebinding, mip tails e os twins depth/colour do SotC; validar pai criado depois do mip e invalidacao CPU. |
| [#5277][p5277] `99e33b7e` - evitar locks quando nenhum estado muda | O port ja tem uma leitura sem lock em `region_manager.h:212`, mas nao o desenho completo do upstream. Em `ChangeRegionState`, os bits mudam antes de `UpdatePageWatchersForRegion`; o upstream atualiza protecoes antes de publicar o estado usado pelo bypass. Revisar ordenacao/atomicidade e resumo de bits junto com FaultGrace, sweep e guardas de stack. Nao copiar apenas o retorno antecipado. |
| [#5446][p5446] `f9d07487` - fila global CE | O port ainda prende CE ao DCB e chama `cblock.Reset()` em `liverpool.cpp:707`, inclusive ao entrar em buffers indiretos. O upstream remove isso e agenda CE globalmente. A intencao serve de referencia, mas altera ordem, ownership, GPU-idle e shutdown; precisa de adaptacao ao back thread e recorder locais. |
| [#5231](https://github.com/shadps4-emu/shadPS4/pull/5231) `43e4c470` - estagios de waits/apresentacao | O port ja dimensiona quatro wait masks, mas a segunda ainda e ColorAttachmentOutput. O upstream cobre todos com AllCommands. Auditar os consumidores reais antes de mudar: o presenter local faz clear/blit por transfer, nao a composicao ImGui original. Aumentar masks nao deve substituir validacao de toda a cadeia de dependencia. |
| [#5429](https://github.com/shadps4-emu/shadPS4/pull/5429) `08225c2b` - dimensoes singleton em LDS | Candidato de correcao, mas o port tem analise propria de ReadLane uniforme e wave64 sobre subgroup32. Preservar essas regras; testar IR e shader. Se integrado, incrementar a versao do cache de shaders. |
| [#5352](https://github.com/shadps4-emu/shadPS4/pull/5352) `8bafaa5a` - outro padrao de descriptor patching | Trata uma combinacao especifica de AND/shift de stride/records; nao e uma correcao geral de Phi ou de indices dinamicos. So priorizar com shader que demonstre o padrao, preservando `dynamic_tsharp_array_size` local. |

## O que os nove commits novos significam aqui

| Commit / PR | Avaliacao para o port Windows |
|---|---|
| `489dcf4e` [#5507](https://github.com/shadps4-emu/shadPS4/pull/5507) | VMASKMOV no Rosetta 2/macOS: nao se aplica ao host Windows x86-64 do jogador. |
| `4f1a70f6` [#5504][p5504] | Reseta `gpu_hash` somente no refresh, em vez de a cada touch. Depende do GC2; o port ainda usa `hash`/`mip_hashes`, sem esse `gpu_hash`. Nao e uma linha esquecida que possa ser copiada diretamente. |
| `67dbc067` [#5489](https://github.com/shadps4-emu/shadPS4/pull/5489) | Corrige erro de `open(O_CREAT)` na camada IOFile/mount do emulador. O port usa `src/runtime_file.c:236`, abre pelo host e propaga errno; essa camada upstream nao e usada. O contrato ENOENT/ENOTDIR pode virar teste separado, nao um cherry-pick. |
| `e0845a12` [#5509](https://github.com/shadps4-emu/shadPS4/pull/5509) | Corrige o nome textual `Ubscaled`; melhoria pequena de diagnostico, nao estabilidade ou CPU. |
| `68818c76` [#5443](https://github.com/shadps4-emu/shadPS4/pull/5443) | Adiciona HLE de VdecCore/VideoCore e callbacks. O runtime de imports/video do port e diferente; estudar se uma cutscene demonstrar um import/caminho faltante. Nao importar servidores e resolvers inteiros para esta etapa. |
| `30f41708` [#5500](https://github.com/shadps4-emu/shadPS4/pull/5500) | Desbloqueio de trofeu de platina. O port usa sua propria implementacao offline; nao melhora o caminho critico de render/CPU. |
| `0815f892` [#5520](https://github.com/shadps4-emu/shadPS4/pull/5520) | Nome da biblioteca RenderDoc no macOS: nao se aplica ao pacote Windows. |
| `3a8ee87a` [#5519][p5519] | RenderDoc sem buffer-device-address/capture-replay: util para diagnostico em drivers afetados. Altera profile, allocator, buffers e geracao de shaders/DMA; nao desativar DMA genericamente na RTX 3070 para copiar esse fallback. |
| `2cdefaa1` [#5521](https://github.com/shadps4-emu/shadPS4/pull/5521) | Outro caminho de instalacao RenderDoc no macOS: nao se aplica ao pacote Windows. |

## O que manter do port e o que nao promete resolver os logs

- As correcoes anteriores de GetUserData/Phi, CFG/EXEC, fp64, clamp, CoarseDerivative e
  atomicas 1D ja estao presentes. A versao local do cache e 38; esta revisao nao a altera.
- O callback `on_submit` ja vem antes de `EndSession` no scheduler local, como em #5113.
- O `GetLodStats` de [#5493](https://github.com/shadps4-emu/shadPS4/pull/5493) aceita o pacote
  e registra diagnostico. O port ja coleta/escreve estatisticas reais de mip no caminho
  grafico em `liverpool.cpp:1398`, com layout observado no SotC. Nao substituir por um
  handler que so registra log; o caminho compute pode ser examinado separadamente.
- O upstream oficial nao tem a combinacao local `BackLoop`/`CommandRecorder`. Sua correcao
  [#5258](https://github.com/shadps4-emu/shadPS4/pull/5258) libera coroutines aninhadas, mas
  o port documenta regressao quando essa liberacao foi habilitada. `SOTC_CP_FREE_TASKS`
  deve continuar desligado ate rastrear todos os leitores, inclusive o recorder/GPU.
- GC2 [#3396](https://github.com/shadps4-emu/shadPS4/pull/3396), readback [#5473](https://github.com/shadps4-emu/shadPS4/pull/5473)
  e hash [#5504][p5504] formam um conjunto de contratos de dirty/baseline/backing memory.
  Nao substituir o cache inteiro, nem reativar hot readback ou relaxar guardas para facilitar
  o merge. O port usa a memoria guest nativa e protecoes diferentes das do emulador.
- No HEAD consultado, `IT_SET_PREDICATION` continua nao implementado,
  `sceVideoOutSetWindowModeMargins` continua stub, e `ComputeOffset` ainda nao trata Phi
  genericamente. Atualizar esses arquivos nao elimina automaticamente os avisos do log.
- Os avisos de espera `PS4SyncEvent` nao demonstram qual fila esta bloqueada. Nao ha aqui
  evidencia para vincula-los a #5406, nem para classificar uma sessao que sai com codigo 0
  como crash. Manter a instrumentacao de CPU/frametime da v0.66 e registrar o sintoma real.

## Sequencia recomendada de integracao

1. CE/DE #5406 e timer #5435 em commits independentes, com testes sem GPU e Windows CI.
2. ExpandImage #5444 com testes de dados/flags e depois teste de streaming/save/load no PC
   do jogador; avaliar #5433 separadamente para nao misturar duas mudancas de ownership.
3. Usar `[CPU]` e perfis para decidir a necessidade de #5277 e do trabalho maior em CE.
4. Tratar LDS, descriptors e captura somente quando houver um caso reproduzivel da area.

Para qualquer alteracao de lifetime/sincronizacao: preservar flags de diagnostico e
protecoes, revisar comandos adiados, testar shutdown e falhas, executar o build Windows
e publicar como preview apenas apos verificar o pacote. Validacao de gameplay continua
separada de compilacao e testes simulados. DLSS nao faz parte deste plano.

[head]: https://github.com/shadps4-emu/shadPS4/commit/2cdefaa1298c6e4ece6c74bd84e90f3906282d51
[p5406]: https://github.com/shadps4-emu/shadPS4/pull/5406
[p5444]: https://github.com/shadps4-emu/shadPS4/pull/5444
[p5435]: https://github.com/shadps4-emu/shadPS4/pull/5435
[p5433]: https://github.com/shadps4-emu/shadPS4/pull/5433
[p5277]: https://github.com/shadps4-emu/shadPS4/pull/5277
[p5446]: https://github.com/shadps4-emu/shadPS4/pull/5446
[p5504]: https://github.com/shadps4-emu/shadPS4/pull/5504
[p5519]: https://github.com/shadps4-emu/shadPS4/pull/5519
