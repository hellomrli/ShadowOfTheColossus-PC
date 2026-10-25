# Investigacao: queda NVIDIA ao iniciar (0.64 preview)

## Evidencia disponivel

Dois logs de 2026-10-25: Ryzen 5 9600X (12 threads), 32 GB, RTX 3070, driver registrado como
`621.1.0.0`, Vulkan 1.4. Em ambos, cerca de sete segundos apos iniciar:

```text
Fault: read access to 0000000000000000 at nvoglv64.dll+0xe35140
Crash at nvoglv64.dll+0xe35140 (code 0xc0000005)
```

- A primeira sessao usa FSR 3; a segunda diz explicitamente `SOTC FRAMEGEN: off` e cai no
  mesmo offset. Desligar apenas a geracao de quadros nao resolveu nessas duas amostras.
- Ambas dizem `SOTC DLSS: ready`. A linha de saida com FSR 1 nao prova que o NGX esteja
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

Os dumps citados sao `2026-10-25_11-57-36_bb-probe.dmp` e
`2026-10-25_12-02-14_bb-probe.dmp`, em `user/crashes/` no PC do jogador. Eles nao estao
disponiveis neste ambiente de desenvolvimento. Nao foi feita reproducao com RTX ou com o jogo.
Esta nao e a falha antiga em codigo guest `0x490a0`.

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

1. Mantenha a geracao de quadros desligada, como na segunda sessao relatada.
2. Acrescente `SOTC_DLSS=0` em **Avancado > Variaveis extras**, reinicie o jogo e guarde o log.
   `SOTC DLSS: ready` deve desaparecer. Selecionar FSR 1 nao substitui esse teste.
3. Em outro teste, acrescente apenas `SOTC_RECORD_THREAD=0` a uma configuracao registrada.
   Informe se DLSS continuou desligado. Nao troque driver, cache e outras opcoes junto.
4. Se cair, envie o log completo e o dump da mesma sessao. Nao e necessario enviar jogo,
   firmware, saves ou o cache inteiro. Dumps podem conter memoria do processo: use um canal
   privado quando necessario.

Na 0.64, `GPU context` identifica uma operacao instrumentada na thread que falhou, com hash,
permutacao e estagio do shader ou chaves do pipeline quando disponiveis. O callback do gravador
tambem e registrado. Nem todo caminho Vulkan/NGX esta instrumentado; contexto ausente e util.
`Native stack (Windows unwind tables)` tenta mostrar o driver e seus chamadores nativos; a
cadeia RBP continua separada para o guest. Uma falha no unwind nao deve impedir o dump original.

## Limites da verificacao

Os testes de patch usam ELF/SFO pequenos e sinteticos. Os testes de cache usam blobs artificiais,
inclusive desalinhamento, truncamento, falha de leitura e falha ao substituir o arquivo. O teste
de contexto cobre aninhamento, excecoes C++ e isolamento por thread. O teste Windows captura
uma pilha nativa viva compilada com `-fomit-frame-pointer`.

O CI compila e empacota o port no Windows, testa o lancador congelado e inicia o loader sem
MSYS2 no PATH. Isso verifica o pacote, nao a renderizacao, sincronizacao Vulkan, avaliacao NGX,
qualidade da imagem ou a ausencia de quedas no jogo. A 0.64 permanece pre-release.
