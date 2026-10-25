# Investigacao: queda NVIDIA ao iniciar (0.65 preview)

## Evidencia disponivel

Tres logs de queda de 2026-10-25: Ryzen 5 9600X (12 threads), 32 GB, RTX 3070, driver
registrado como `621.1.0.0`, Vulkan 1.4. Em todos, cerca de sete segundos apos iniciar:

```text
Fault: read access to 0000000000000000 at nvoglv64.dll+0xe35140
Crash at nvoglv64.dll+0xe35140 (code 0xc0000005)
```

- Ha relatos com FSR 3 e com `SOTC FRAMEGEN: off` no mesmo offset. Desligar apenas a
  geracao de quadros nao resolveu essas amostras.
- Todas dizem `SOTC DLSS: ready`. A linha de saida com FSR 1 nao prova que o NGX esteja
  desligado: menus e quadros sem G-buffer podem usar o fallback.
- Os erros de `.spv` ausente e `ComputeOffset: ... Phi` aparecem na primeira sessao, mas nao
  na segunda. Nao sao evidencia suficiente da causa da mesma queda.
- A mudanca unica de thread produtora do gravador e permitida pelo codigo; o aviso sozinho
  nao demonstra corrida entre produtores.
- O caminho do dump termina em `CUSA08804`, enquanto os metadados lidos dizem `CUSA08809`.
  O nome da pasta nao identifica a regiao/versao do executavel. Nas duas sessoes os modos
  recusam o eboot: `this eboot is not EU 1.01, not applied`.
- Apesar dessa recusa, a preparacao antiga escolhia tres patches de red zone pelos metadados
  apenas. Isso e um defeito de seguranca dos patches, nao uma prova de que causou a queda.
- Na terceira queda, ja com a 0.64, o log confirma zero patches fixos de red zone e geracao
  de quadros desligada. A queda persiste, agora com uma pilha nativa util.

Os dumps citados sao `2026-10-25_11-57-36_bb-probe.dmp`,
`2026-10-25_12-02-14_bb-probe.dmp` e `2026-10-25_14-17-23_bb-probe.dmp`, em
`user/crashes/` no PC do jogador. Eles nao estao disponiveis neste ambiente de desenvolvimento.
Nao foi feita reproducao local com RTX ou com o jogo. Esta nao e a falha antiga em codigo
guest `0x490a0`.

### Pilha nativa da terceira queda

```text
nvoglv64.dll+0xe35140
160_E658700.bin+... (sete frames)
_nvngx.dll+0xf61e
_nvngx.dll+0xe567
sotc_dlss.dll+0x3fe7
sotc_dlss.dll+0x188c
bb-probe.exe+0x254c89
...
```

O disassembly da ponte publicada na 0.64 liga os retornos `+0x188c` e `+0x3fe7` a
`NVSDK_NGX_VULKAN_CreateFeature1`. Portanto, a pilha passa pela criacao da feature DLSS,
nao pela avaliacao de um shader especifico do jogo. A linha
`GPU context: no instrumented operation on the faulting thread` era esperada nesse ponto:
a fronteira NGX ainda nao tinha um scope de diagnostico.

### Controle com FSR 1

O jogador depois confirmou que desligou o uso de DLSS e selecionou FSR 1. O novo log dura
aproximadamente dez minutos e termina com codigo 0, sem a queda anterior. Apos ativar o
limite de 30 FPS, os ultimos minutos registram 30 FPS e nenhum quadro acima de 50 ms.
FSR 3 foi ativado durante essa sessao sem reproduzir a queda de sete segundos.

O log ainda contem `SOTC DLSS: ready`: a biblioteca pode inicializar mesmo com FSR 1
selecionado, mas a selecao evita a criacao/avaliacao da feature SR. Nao ha a linha de
criacao bem-sucedida `SOTC DLSS: DLSS ...`. Isso confirma FSR 1 como contorno observado
nesse PC e reforca o caminho DLSS como alvo. **Nao valida o DLSS da 0.65:** essa execucao
ocorreu antes da publicacao da correcao. Os avisos SRT/Phi/stack/event flag tambem aparecem
na sessao sem queda; nao justificam, por si so, mudar esses subsistemas.

## Defeito de handoff corrigido na 0.65

Com `SOTC_RECORD_THREAD` ativo, o produtor recebe um handle interno de comando. Os thunks
do dispatcher Vulkan-Hpp copiam as chamadas para uma fila; a thread consumidora traduz esse
handle para o `VkCommandBuffer` real antes de chamar o driver. O caminho antigo entregava
o handle interno diretamente a `sotc_dlss.dll`. NGX carrega funcoes Vulkan por
`vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`, sem passar por esses thunks. O handle interno
nao e um objeto Vulkan valido.

Apenas traduzir o handle no produtor nao basta: as conversoes de cor/profundidade precisam
ser gravadas primeiro, e duas threads nao podem gravar simultaneamente no mesmo buffer.
Na 0.65, criacao e avaliacao NGX usam um callback na fila do gravador, na ordem das
conversoes/barreiras e com o handle real. Uma espera de CPU confirma o resultado e o fim
das capturas antes de liberar parametros ou selecionar FSR 1; nao ha `deviceWaitIdle` novo.
Sem gravador, a chamada continua sincrona na thread que ja possui o buffer real.

A fronteira DLSS tambem recusa handles nulos/internos, informa dimensoes antes da criacao e
anota `NGX DLSS CreateFeature` / `NGX DLSS Evaluate` no contexto da thread executora.
Falhas retornadas por NGX mantem FSR 1 e agora tambem fecham a label de debug. Um acesso
invalido dentro do driver nao e uma falha retornada: nao ha promessa de captura/recuperacao.

Esse handoff invalido e um defeito verificavel no codigo e e consistente com a pilha e com
o controle FSR 1. A ausencia da queda com DLSS na RTX 3070 ainda precisa de teste da 0.65.
Nao foram trocados parametros NGX, driver, padroes do renderer ou cache para esconder a falha.

## Achados de review corrigidos na 0.64

| Prioridade | Defeito | Correcao |
|---|---|---|
| P1 | Offsets fixos de CPU autorizados so pelo SFO, mesmo quando os modos rejeitavam o codigo. | Exigir a verificacao do executavel EU 1.01 antes dos offsets de red zone. Manter o suporte generico a EH/SSE quando disponivel. |
| P1 | Extracao de um blob ZIP com tamanho nao multiplo de 4 podia escrever mais bytes que o vetor alocado; falhas de leitura/extracao deixavam dados parciais. | Validar tamanho, alinhamento, leitura completa e CRC antes de publicar os dados. Rejeitar blobs maiores que 64 MiB. |
| P2 | Escritas curtas e substituicao do cache no Windows nao eram verificadas por completo. | Confirmar escrita/flush/close e publicar por substituicao atomica do temporario; preservar o arquivo anterior se a publicacao falhar. |
| P2 | O estado do lancador podia confiar na versao do SFO ou verificar o eboot base quando a atualizacao seria executada. | Verificar o codigo do executavel efetivo com a precedencia do ini, ambiente e variaveis extras. Atualizacao sem eboot mantem o executavel base. |
| P2 | A cadeia RBP nao localizava o chamador em DLLs de driver compiladas sem frame pointer. | Desenrolar o CONTEXT original pelas tabelas Windows e registrar o contexto Vulkan ativo por thread. |

A verificacao SPIR-V e estrutural (cabecalho e limites das instrucoes), nao uma validacao
semantica completa. Um arquivo ausente num cache frio e normal e nao gera mais um erro por
shader. Blobs rejeitados sao recompilados; nao houve mudanca do formato, `ShaderBinaryVersion`
ou da saida do recompiler nesta versao. Atualizar da 0.63 nao exige limpar um cache valido.

Esses defeitos foram encontrados no codigo e cobertos por regressoes independentes de GPU.
Nenhum deles, isoladamente, comprova a causa do acesso nulo dentro da DLL NVIDIA.

## Teste controlado no PC afetado

Guarde um backup de `user/` e `sotc.ini`. Use a preview em uma pasta separada, mantenha o
mesmo dump e as mesmas opcoes. Nao renomeie a pasta do jogo, edite o SFO ou force offsets EU.

1. Para continuar jogando sem testar DLSS, mantenha FSR 1, o contorno confirmado pelo jogador.
2. Para testar a correcao, use a 0.65, selecione DLSS e mantenha a geracao de quadros
   desligada. Retire `SOTC_DLSS=0`, se presente; nao troque as demais opcoes ao mesmo tempo.
   Procure `creating ... (real command buffer)` e depois `SOTC DLSS: DLSS ...`. Apenas
   `ready` nao verifica criacao/avaliacao. Entre no jogo e teste tambem a saida normal.
3. Se cair, guarde primeiro o log/dump. Em testes separados, compare FSR 1 e
   `SOTC_RECORD_THREAD=0` com DLSS selecionado. Registre qual configuracao foi usada.
   `SOTC_DLSS=0` e um controle mais forte: impede ate a inicializacao NGX e remove `ready`.
   Selecionar FSR 1 evita o uso SR, mas nao desliga essa inicializacao.
4. Envie o log completo e o dump da mesma sessao. Nao e necessario enviar jogo,
   firmware, saves ou o cache inteiro. Dumps podem conter memoria do processo: use um canal
   privado quando necessario.

Na 0.64, `GPU context` identifica uma operacao instrumentada na thread que falhou, com hash,
permutacao e estagio do shader ou chaves do pipeline quando disponiveis. O callback do gravador
tambem e registrado; na 0.65 a criacao/avaliacao NGX acrescenta seu proprio scope.
Nem todo caminho Vulkan/NGX esta instrumentado; contexto ausente e util.
`Native stack (Windows unwind tables)` tenta mostrar o driver e seus chamadores nativos; a
cadeia RBP continua separada para o guest. Uma falha no unwind nao deve impedir o dump original.

## Limites da verificacao

Os testes de patch usam ELF/SFO pequenos e sinteticos. Os testes de cache usam blobs artificiais,
inclusive desalinhamento, truncamento, falha de leitura e falha ao substituir o arquivo. O teste
de contexto cobre aninhamento, excecoes C++ e isolamento por thread. O teste Windows captura
uma pilha nativa viva compilada com `-fomit-frame-pointer`.

O novo teste sem GPU usa o `vk_recorder.cpp` de producao contra um driver simulado: cobre
thunks, varios chunks, ordem, thread proprietaria, handles reais, sucesso/falha e capturas
move-only destruidas antes do retorno. Cobre tambem o caminho sem gravador e uma chamada
aninhada com handle real. Nao usa NGX e nao prova validade de parametros ou sincronizacao GPU.

O CI compila e empacota o port no Windows, testa o lancador congelado e inicia o loader sem
MSYS2 no PATH. Isso verifica o pacote, nao a renderizacao, sincronizacao Vulkan, avaliacao NGX,
qualidade da imagem ou a ausencia de quedas no jogo. A 0.65 permanece pre-release.
