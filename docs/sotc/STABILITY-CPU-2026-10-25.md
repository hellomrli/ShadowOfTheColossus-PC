# Estabilidade e CPU: etapas 0.66 e 0.67 (preview)

## Prioridade e evidencia

O jogador pediu estabilidade geral e melhor eficiencia de CPU antes de continuar o trabalho
de DLSS; pode seguir com FSR 1 e FSR 3 frame generation. A 0.65 criou DLAA 1920x1080 na RTX
3070, rodou cerca de 7,5 minutos com FSR 3 e terminou com codigo 0. A queda antiga de sete
segundos nao reapareceu nessa amostra. Nao houve novo trabalho em parametros ou recursos DLSS.

No log, F7 ativa o limite de 30 FPS em 2:45. Os minutos seguintes ficam em 30 FPS, sem quadros
acima de 50 ms por algumas janelas; depois, uma troca de area coincide com novos quadros longos.
Isso nao mede CPU nem prova vazamento ou deadlock. Avisos de event flag registram esperas
por eventos; os guardas de stack evitam proteger/sobrescrever pilhas guest. Nao foram removidos
para esconder o log. Nao se deve forcar patches EU quando o executavel nao e verificado.

## Correcoes da 0.66

- **Polling ocioso:** o timeout de 500 us do gravador podia virar zero no Windows. Depois
  do spin curto, a thread agora espera notificacao de trabalho ou shutdown. Publicacoes
  por lote, `Kick` e passagem de chunk usam a mesma notificacao. Nao adiciona espera da GPU.
- **Vida da fila:** os ultimos chunks nunca passavam por outro marcador End e nao voltavam
  ao pool. Agora sao reciclados depois dos callbacks, antes de encerrar a thread.
- **Log de carregamento:** o fim era registrado somente quando a UI voltava a desenhar
  algo. Sem menu/FPS/toast, a funcao retornava antes desse registro; F7 acabava revelando
  uma duracao inflada. Agora a transicao e observada mesmo quando nao ha overlay a desenhar.
  Isso corrige a medicao, nao promete diminuir o carregamento real.
- **Flags entre threads:** inicializacao e visibilidade dos dialogos usam atomicos; o texto
  e os dados ImGui continuam protegidos pelo mutex existente.
- **CPU real:** clocks do SO medem processo e threads uma vez por minuto. A consulta usa
  handles validos durante a vida da thread e descarta diferencas de geracoes distintas.
  Nao ha contadores/locks de diagnostico adicionados a cada draw.

O spin curto mantem o padrao anterior: 250 us em maquinas com mais de oito CPUs logicas,
zero nas menores. `SOTC_REC_SPIN_US=0` permite um controle separado sem spin. O padrao de
`SOTC_REC_SLEEP_US` agora e espera por evento (ausente, zero ou negativo). Um valor positivo
seleciona espera temporizada, arredondada para cima a um milissegundo, no intervalo 1..1000 ms.
Mesmo a espera temporizada acorda quando chega uma notificacao; nao e um sleep fixo por frame.

## Backports da 0.67

A [revisao de 2026-10-26](UPSTREAM-REVIEW-2026-10-26.md) foi fixada no upstream oficial
`2cdefaa1`. Tres correcoes foram adaptadas, sem substituir o renderer inteiro:

- **#5406, CE/DE:** direcao e limite da diferenca dos contadores, e verificacao do handle CE
  antes de consultar/retomar a coroutine. Nao muda a fila CE nem libera tasks aninhadas.
- **#5444, expansao de textura:** so copia a origem se ela tem conteudo GPU valido e nenhum
  bit Dirty. Isso impede que a copia antiga sobrescreva o upload novo; mantem as regras
  locais de flags, rebinding, rastreamento e liberacao adiada.
- **#5435, timer Windows:** reutiliza o handle por thread, com alta resolucao quando
  disponivel. Falhas usam espera bloqueante; interrupcao/APC e chamadas reentrantes nao
  reutilizam um handle ainda em espera. O timer e fechado na saida da thread.

O terceiro item atua no `AccurateTimer` do videoout. O runtime C ja tinha outro cache de
timer; nao e uma otimizacao nova de todas as threads guest. Reduzir trabalho redundante
nao implica maior utilizacao total da CPU ou mais FPS. DLSS/FSR 3, limites de FPS, protecoes
de stack/memoria, GC2 e a versao de cache de shaders 38 permanecem inalterados.

## Interpretar a linha CPU

Exemplo ilustrativo, nao medido no jogo:

```text
[CPU] Process 25.0% of 12 logical CPUs; threads % of one logical CPU: command 95.0, draw -1.0, recorder 40.0, presenter 5.0, videoout 1.0 (-1 = unavailable)
```

- `Process`: soma de CPU user/kernel de todo o processo, dividida pela capacidade das CPUs
  logicas disponiveis. Inclui threads guest, shader workers e audio.
- Threads: cada 100% equivale a uma CPU logica, nao ao processador inteiro. Num sistema de
  12 threads, uma thread saturada sozinha corresponde a cerca de 8,3% do total.
- `-1`: thread nao registrada, recriada no intervalo, clock indisponivel ou afinidade alterada.
  Nao significa utilizacao negativa. Nem toda thread do jogo tem uma coluna individual.
- CPU baixo com limite de 30 FPS pode ser normal. CPU total baixo com `command` perto de
  100% pode indicar gargalo serial; aumentar polling ou criar mais threads nao o resolve.
- `[PERF]` mede flips do jogo, nao os quadros interpolados pelo FSR 3. Nao use essa linha
  sozinha para concluir que frame generation esta inativo.

## Verificacao no PC do jogador

1. Guarde backup de `user/` e `sotc.ini`; mantenha FSR 1 + FSR 3 e as mesmas resolucao/texturas.
   Nao altere driver, cache, memoria e varios switches de uma vez.
2. Faca uma sessao normal de 10-15 minutos: carregar save, caminhar, transitar de area,
   salvar/recarregar e sair. Preserve o log inteiro; uma queda tambem precisa do dump correspondente.
   Observe texturas depois das trocas de area; se estiver normal, repita por pelo menos
   30 minutos para ampliar a amostra. Isso ainda nao e um teste do jogo inteiro.
3. Compare o mesmo trecho com o mesmo limite de FPS. Se quiser medir desempenho sem limite,
   faca uma amostra separada e anote o uso de F7; nao compare uma janela limitada com outra livre.
4. Envie as linhas `[CPU]` / `[PERF]` e descreva travadas, imagem congelada ou audio interrompido.
   Para detalhar o tempo de pipelines, copias e waits, `SOTC_FPS=1` ativa o relatorio existente.
5. Se surgirem regressao ou travamento novos, preserve a configuracao e o log antes de tentar
   um controle separado com `SOTC_RECORD_THREAD=0`. Nao desative protecoes de memoria.

## Limites e proxima etapa

Os testes usam o gravador real com driver simulado: ordem e lifetime, publicacao durante o
spin/park, `Kick`, lote, End de chunk e shutdown ocioso. ASan/UBSan com deteccao de leaks
cobre a fila. Testes separados verificam loading sem UI/hotkey e clocks reais/sinteticos de
CPU, inclusive encerramento da thread durante a amostragem. O teste ocioso imprime consumo
de uma CPU logica sem usar GPU; esse numero nao e um ganho de FPS do jogo.

Na 0.67, os testes usam os mesmos helpers do runtime: contadores/handle CE com coroutines
pequenas (nao o processador PM4 completo), e a politica de expansao com callbacks de upload/copy
em memoria (nao renderizacao Vulkan/MSAA). O backend de timer recebe falhas simuladas e testa
APC reentrante, limites e TLS. O teste Windows usa ainda timers reais, QueueUserAPC e contagem
de handles apos encerrar threads. ASan/UBSan cobre os tres testes portaveis, nao o driver Windows.
Os resultados do build e da auditoria do ZIP ficam nas notas da pre-release correspondente.

Build Windows, testes sem GPU e auditoria do pacote nao comprovam estabilidade do jogo
inteiro. Sem um dump local e GPU de teste, a proxima etapa depende dos logs/trechos reproduziveis
do jogador: identificar gargalos de comando/compilacao/readback e corrigir a causa demonstrada,
sem paralelizar gravacao Vulkan indiscriminadamente nem relaxar as protecoes.
