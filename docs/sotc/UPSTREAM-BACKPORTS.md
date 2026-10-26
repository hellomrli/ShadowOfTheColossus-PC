# Backports do shadPS4 depois de b4e7ae7

Base vendorizada: `b4e7ae776071ceb1dd35004577a8719481e1d258`, commit de 2026-09-24
(a versao anterior deste documento confundia a data anotada de 2026-10-02 com a do commit).
A revisao original cobriu `b4e7ae7..12a45645`, ate 2026-10-24: 98 commits que tocam
`shader_recompiler` ou `video_core`.

**Revisao atualizada de estabilidade/CPU:** [2026-10-26](UPSTREAM-REVIEW-2026-10-26.md),
fixada no upstream `2cdefaa1`. A nova revisao nao aplica codigo ao pacote v0.66.

## Aplicados na v0.62

Estes dez backports ja estao no historico de `main`; nao sao candidatos pendentes.

| Upstream | Correcao | Motivo |
|---|---|---|
| `524be7c1` (#5427) | Excluir `GetUserData` do folding de Phi | User data e um valor de runtime; nao pode ser tratado como constante entre caminhos. |
| `78bedfac` (#5365) | Preservar arestas de fallthrough no CFG | O port ja tinha `block.cond = True`, mas faltavam o tratamento de break/return/unreachable e `AddBranch` no proximo bloco. |
| `f0ea8eb7` (#5459) | `S_GETREG_B32` / `HW_ID.SE_ID` | Retornar shader engine virtual 0 em vez de atingir `UNREACHABLE` por falta de traducao. |
| `c34ca21f` (#5220) | Preservar escopo EXEC depois de ramo vazio | Um escopo vazio ainda deve abrir a mascara EXEC quando a proxima instrucao exige isso. |
| `603cff43` (#5364) | Ordem estavel de deduplicacao de Phi | Evitar que a iteracao de `unordered_map` produza IR diferente entre execucoes. |
| `1c2b8f97` (#5346) | Clamp de RCP/RSQ/LOG | Respeitar limites GCN em vez de mapear variantes clamp para instrucoes comuns. A parte `V_CVT_PK_U8_F32` ja existia. |
| `b08fd9bb` (#5342) | Detectar fp64 pelos tipos das instrucoes | Nao depender apenas de PackDouble/UnpackDouble para habilitar suporte a double. |
| `6752d8b1` (#5447) | `BUFFER_WBINVL1` / `BUFFER_WBINVL1_SC` | Corrigir formato sem operandos e emitir barreira de memoria do device. |
| `c1b26558` (#5468) | `has_derivatives` inclui `CoarseDerivative` | Reconhecer tambem o modificador coarse na coleta de informacoes do shader. |
| `5ae2caf3` (#5203) | Coordenadas de atomicas em imagem 1D | Usar os componentes corretos em `OpImageTexelPointer` conforme o tipo da imagem. |

`ShaderBinaryVersion` passou de 33 para **38**, nao 37, contando os dois ultimos backports.
A recompilacao foi introduzida na v0.62; esta atualizacao de documentacao nao invalida cache.
Tambem houve a correcao local de chamar `Dlss::Shutdown()` antes de destruir o device,
documentada separadamente no historico da v0.62.

Commits locais que aplicaram os dez backports: `7a33f1b`, `e755f96`, `5e902e1`,
`1b6b07d` e `be7c546`. Outros backports anteriores estao em `gpu/VENDOR.txt`.

## Ja presente ou tratado por um caminho local

| Upstream | Situacao no SotC |
|---|---|
| `c01ae7e6` (#5150), probe limitado a pagina da falta | `PageManager::HandleGuestFault` limita o acesso a `min(8, GetNextPageAddr(addr) - addr)`. |
| `bcf083f0` (#5151), fim das paginas limitado a 40 bits | `PageManager::EnsurePages` limita o fim a `1 << 40`. |
| `41f2a428` (#5113), callback antes de EndSession | O scheduler local ja executa `on_submit` antes de `EndSession`. |
| `eedf851a` (#5450), bordas nao rastreadas sobrescritas pelo guest | Objetivo parcialmente tratado: UntrackImageHead/Tail marcam MaybeCpuDirty; SafeToDownload recusa Dirty; RefreshImage usa hash adiado dos primeiros texels. Nao e equivalencia integral com o hashing de bordas do upstream, nem prova de que todos os casos estejam cobertos. |
| Backports anteriores de V_INTERP_MOV, modo FP compute, S_SUBB/S_ADDC, literais fp64, V_BFM, ADDC, V_CVT_PK_U8 e V_ALIGNBIT | Registrados em `gpu/VENDOR.txt`; nao reaplicar como novidade. |

## Pendencias reais

A [revisao de 2026-10-26](UPSTREAM-REVIEW-2026-10-26.md) prioriza os guardas CE/DE,
o timer Windows e a preservacao de dados validos em ExpandImage. Tambem separa mudancas
menores de refatoracoes que exigem adaptacao ao draw thread/recorder do port.

Continuam exigindo avaliacao especifica: layout de mips tiled (`7e778987`), associacao
color/stencil (`fecfbed0`), stencil-only (`626f3ace`), copias D32/D32S8 (`92c7a884`), copias
de todos os mips (`b3d46e96`), LDS singleton (`08225c2b`) e gradientes/offsets 1D (`e754ce62`).
Nao importar em bloco GC2 (`e6faa2e3`), renderer optimizations (`8e23388a`), numero de buffers
(`4ddaada8`) ou remocao de batching (`fca10b26`). O port divergiu em backing memory, readback,
guardas de stack, twins depth/colour e comandos adiados.

A investigacao antiga de `0x490a0` e historica; o estado mais recente esta em
[CRASH-NVIDIA-2026-10-25.md](CRASH-NVIDIA-2026-10-25.md) e no
[guia de estabilidade/CPU](STABILITY-CPU-2026-10-25.md). A sessao do jogador com saida 0
nao comprova estabilidade total e tampouco impede investigar defeitos demonstraveis por
testes de codigo. Nao atribuir avisos genericos a uma causa sem evidencia.

## Regras de integracao

1. Identificar o commit exato e suas dependencias; adaptar somente a correcao necessaria.
2. Preservar comentarios `sotc:`/`bbport:`, switches de diagnostico e protecoes locais.
3. Se a saida de shader mudar, incrementar `ShaderBinaryVersion` e documentar a recompilacao.
4. Testar cada mudanca e executar o build Windows antes de integrar; distinguir testes
   sem GPU de validacao de gameplay no PC do jogador.
