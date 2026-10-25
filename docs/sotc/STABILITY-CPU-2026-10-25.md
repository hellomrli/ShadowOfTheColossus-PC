# Estabilidade e CPU: primeira etapa (0.66 preview)

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

## Correcoes desta etapa

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

Build Windows, testes sem GPU e auditoria do pacote nao comprovam estabilidade do jogo
inteiro. Sem um dump local e GPU de teste, a proxima etapa depende dos logs/trechos reproduziveis
do jogador: identificar gargalos de comando/compilacao/readback e corrigir a causa demonstrada,
sem paralelizar gravacao Vulkan indiscriminadamente nem relaxar as protecoes.
