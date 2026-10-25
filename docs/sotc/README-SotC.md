# Shadow of the Colossus — PC (port nativo)

Port nativo do **Shadow of the Colossus** de PS4 (CUSA08809, versão EU 1.01) para Windows.
O código do jogo roda direto na CPU. Isto não é um emulador completo: há um carregador e as
funções de sistema de que o jogo precisa. A GPU usa o núcleo de vídeo Vulkan do fork
shadPS4 SotC. A base é o [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (GPL-2.0).

Este pacote **não contém o jogo nem firmware da Sony**. Você precisa ter os seus.

**0.65 (pre-release):** corrige o envio de comandos ao DLSS com gravação em thread.
O teste com DLSS na RTX 3070 afetada ainda está pendente; selecionar FSR 1 é o contorno
confirmado pelo jogador. Previews são baixadas manualmente, sem mudar o canal estável.

## Requisitos

- Windows 10/11 64-bit e GPU com Vulkan 1.3.
- Python não é necessário para o `SotC.exe`; só o `SotC.bat` precisa de
  [Python 3](https://www.python.org/downloads/).
- O **seu dump** do jogo CUSA08809 (pasta com `eboot.bin`, `sce_module`, `sce_sys`...).
- Para ter som, `libSceNgs2.sprx` e `libSceUlt.sprx` do **seu próprio console**, decriptados.
  São os mesmos arquivos da pasta `user/sys_modules` do shadPS4; num PS4 com GoldHEN ficam em
  `/system/common/lib/` (FTP na porta 2121). Sem eles o jogo funciona, mas sem som.

## Como jogar

1. Abra `SotC.exe`, o lançador.
2. Na primeira vez ele **procura sozinho** nos discos do PC:
   - a pasta do jogo (CUSA08809);
   - os dois `.sprx` de som de um shadPS4 instalado (pasta `user/sys_modules`), que ele copia
     para a pasta de dados do port;
   - um save do jogo no shadPS4, que ele oferece importar;
   - as DLCs do jogo (pasta `addcont` do shadPS4 ou uma pasta com uma subpasta por DLC).
3. Confira em **Jogo e saves**. A **biblioteca de jogos** funciona como no shadPS4: adicione as
   pastas onde ficam os seus jogos e escolha o jogo na lista (duplo clique). **Procurar no PC**
   repete a busca automática.
4. Clique em **JOGAR**.

A tela **Jogar** confere tudo antes: a versão do jogo, os módulos de som, os saves e a placa de vídeo.
Na primeira vez a imagem do jogo é preparada, o que leva alguns segundos. Isso só se repete
quando o jogo ou os módulos mudam. Os saves e o cache de shaders ficam em `user/`.

Sem o lançador, `SotC.bat` faz o mesmo lendo `sotc.ini`. `SotC.exe --play` abre o jogo direto
com as últimas opções do lançador, o que serve para atalhos e para o Steam.

## Imagem e desempenho

Tudo isto está no lançador, na página **Imagem e FPS** (e os principais em **Jogar**):

- **Saída** 720p, 1080p, 1440p ou 4K. O jogo renderiza em 1080p, como no PS4, e a imagem é
  ampliada para a saída com **FSR 1** + nitidez (RCAS) ou bilinear. A janela nunca passa de 90%
  da tela; a tela cheia usa o monitor inteiro.
- **DLSS (RTX 20+):** o pacote inclui `sotc_dlss.dll` (ponte MIT do projeto) e o
  `nvngx_dlss.dll` da NVIDIA. Selecione **DLSS / FSR 1 / Bilinear** nas configurações rápidas
  do lançador ou no F1; a escolha fica salva. Com DLSS selecionado numa RTX, usa **DLAA** quando o
  jogo desenha na mesma resolução da saída, **Super Resolution** quando desenha abaixo dela (por
  exemplo `SOTC_RENDER_RES=1280x720` com `SOTC_OUTPUT_RES=1920x1080`). `SOTC_DLSS=0` desliga.
  Saída HDR e quadros sem G-buffer continuam no FSR 1. O HUD faz parte da imagem ampliada.
- **60 FPS** (experimental): tira o limite de 30 FPS do jogo (patch GoldHEN de illusion), com
  vblank a 120 Hz. Mediu 38 FPS no campo aberto e ~25 no templo.
- **Reutilização de binds** (desligada por padrão desde a 0.60.6; `SOTC_FAST_BIND=1` liga): pula
  a remontagem dos descritores quando o desenho seguinte usa o mesmo pipeline, recursos e
  uniforms. Medido no Santuário: ~25 → 30 FPS. Na 0.60.5 deixava pontos pretos em grade no chão de
  areia; a correção aguarda teste.
- **Texturas nítidas**: filtro anisotrópico 16x, quase sem custo.
- **Modo PS4 Pro**: o jogo se vê num PS4 Pro e oferece no próprio menu (Opções > Apresentação)
  o Modo Grafismo **Cinematográfico** (1440p, mais detalhe) ou **Desempenho**. É mais pesado:
  ~25 FPS no campo aberto.
- **Efeitos**: vinheta, névoa atmosférica e oclusão ambiente (SSAO) podem ser desligados.
  O motion blur, o brilho e os filtros ficam no menu do próprio jogo.
- **Cvars do motor** (Avançado): variáveis do motor Bluepoint, `NOME=valor`, para quem quer
  experimentar.

Os modos de patch só valem para a versão EU 1.01; em outra versão eles não são aplicados.
Desde a 0.64 o código do executável também precisa ser verificado antes dos patches de CPU
com offsets fixos. O nome da pasta e a versão no `param.sfo` não provam compatibilidade; não
renomeie o dump nem altere os metadados para forçar os patches.

## Controles

**Mouse:** clique na janela do jogo para capturar o mouse, que passa a girar a câmera; Esc ou
trocar de janela solta o mouse. Botão esquerdo = R1 (agarrar), direito = □ (atacar),
meio = L1. Sensibilidade e inversão ficam em **Controles** no lançador. Para remapear os botões
do mouse, use `mouse.<entrada>=left,right,middle,x1,x2`.

Um controle (DualShock 4, DualSense, Xbox...) é usado automaticamente. Com vários conectados,
escolha um em **Controles** no lançador (ou `BB_GAMEPAD=` no `sotc.ini`). O teclado funciona
junto com o controle:

| Teclado | PS4 |
|---|---|
| W A S D | analógico esquerdo |
| setas | analógico direito (câmera) |
| Espaço | ✕ |
| Shift esquerdo | ○ |
| E | □ |
| Q | △ |
| 1 / 3 | L1 / R1 |
| R / F | L2 / R2 |
| Z / C | L3 / R3 |
| Enter | Options |
| I J K L | direcional |
| Tab / Backspace | touchpad (esquerda / direita) |

As teclas podem ser trocadas em **Controles** no lançador: clique na ação e aperte a nova tecla.
O controle e o mouse também podem ser remapeados no `sotc.ini`, por exemplo `pad.circle=b`.
As entradas são:

- botões: `cross circle square triangle l1 r1 l2 r2 l3 r3 options touchpad touchpad_right`;
- direcional: `up down left right`;
- no teclado, os analógicos: `move_up` `move_down` `move_left` `move_right` e
  `look_up` `look_down` `look_left` `look_right`.

## Estado

Funcionam: logos, abertura, menus com texto, carregar save, exploração a cavalo,
som (com os módulos do usuário).

Desempenho medido no demo da tela de título, modo 60 FPS, i9-13900K + RTX 2060 SUPER:
média de ~67 FPS; o trecho mais pesado (a ponte do cânion) fica em 29–34 FPS. No modo
normal o jogo roda travado em 30 FPS, como no PS4.

## Perguntas comuns

- **DirectX 11/12?** Não. O núcleo de vídeo (shadPS4) traduz a GPU do PS4 para Vulkan; os
  shaders do jogo viram SPIR-V. Toda placa de vídeo com DirectX 12 recente também tem Vulkan
  1.3: atualize o driver. Um modo DirectX exigiria reescrever o núcleo de vídeo inteiro.
- **Placas AMD e Intel?** As otimizações valem para qualquer placa, menos duas que só ligam
  sozinhas na NVIDIA (o envio periódico de trabalho à GPU e as leituras antecipadas).
  Em **Imagem e FPS > Envio de trabalho à GPU**, escolha **Ligado** para testá-las na AMD ou
  Intel; se a tela ficar preta, volte para **Automático**.
- **Por que o som precisa de arquivos meus?** `libSceNgs2.sprx` e `libSceUlt.sprx` são firmware
  da Sony e não podem ser distribuídos. Eles saem do seu PS4 (ou do shadPS4 que você já usa).

## Créditos

- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (bbport, deadinside28 e forks): carregador,
  runtime nativo e lançador, base deste port.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) e o fork shadPS4 SotC: núcleo de vídeo Vulkan.
- [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs): as cvars de culling, a proteção
  do heap do jogo contra leituras da GPU (`bpe_heap_guard`), o flush periódico, as leituras antecipadas,
  o marcador de espera e o gravador de comandos Vulkan em thread própria (`cp_record_thread`).

## Diagnóstico

### Queda no driver NVIDIA ao iniciar (RTX 3070)

Os relatos de 2026-10-25 caem em `nvoglv64.dll+0xe35140`, inclusive com geração de quadros
desligada. A pilha nativa da 0.64 passa pela criação de uma feature NGX. O jogador confirmou
que selecionar FSR 1 evita a queda: cerca de dez minutos, saída com código 0. Essa sessão
ocorreu antes da correção e não valida DLSS na 0.65.

A 0.65 corrige um handle interno inválido entregue a NGX: criação e avaliação agora rodam
na ordem correta, na thread proprietária do comando Vulkan real. **A confirmação de que isso
elimina a queda com DLSS ainda depende do PC afetado.**

1. Para continuar jogando sem testar DLSS, mantenha **FSR 1**, o contorno observado nesse PC.
2. Para testar a 0.65, selecione **DLSS** e mantenha **Geração de quadros: Desligada**.
   Retire `SOTC_DLSS=0`, se presente, sem trocar driver, resolução, cache e outras opções.
   `SOTC DLSS: ready` indica apenas inicialização, inclusive com FSR 1 selecionado. Procure
   `creating ... (real command buffer)` e depois `SOTC DLSS: DLSS ...` para confirmar criação.
3. Se cair, preserve `user/last_run.log` e o `.dmp` da mesma sessão em `user/crashes/`.
   Procure `GPU context`, `NGX DLSS CreateFeature` / `NGX DLSS Evaluate` e
   `Native stack (Windows unwind tables)`. Nem todo caminho do driver está instrumentado.
4. Em testes separados, compare FSR 1 e `SOTC_RECORD_THREAD=0` com DLSS selecionado.
   `SOTC_DLSS=0` em **Avançado → Variáveis extras** é um controle mais forte: após reiniciar,
   impede até a inicialização NGX e remove `ready`. Registre cada configuração testada.

Preserve os dumps anteriores e um backup de `user/` e `sotc.ini` ao trocar de pacote. Não é
necessário enviar arquivos do jogo ou firmware. A investigação e o funcionamento do DLSS
estão em `docs/sotc/CRASH-NVIDIA-2026-10-25.md` e `docs/sotc/DLSS.md`, incluídos no pacote.

Estas variáveis de ambiente ajudam a investigar problemas:

| Variável | Efeito |
|---|---|
| `SOTC_FPS=1` | taxa de quadros a cada 5 s |
| `SOTC_DLSS=0` | desliga a ponte DLSS/NGX durante toda a sessão (exige reinício; diferente de selecionar FSR 1) |
| `BB_FAULT_RING=arquivo` | grava as últimas exceções (sobrevive a quedas) |
| `BB_STACKS_TRIGGER=arquivo` | criar o arquivo despeja as pilhas das threads |
| `BB_READBACKS=0/1/2` | leituras da GPU; o padrão 2 é o exigido pelo jogo |
| `SOTC_FAST_BIND=1` | reaproveita binds de descritores entre desenhos iguais (padrão: desligado desde a 0.60.6) |
| `SOTC_RECORD_THREAD=0` | grava os comandos Vulkan na própria thread de comandos (padrão: thread própria) |
| `SOTC_INDIRECT_DIRECT=0` | mantém indiretos os draws cujos argumentos estão na CPU |
| `SOTC_WAIT_MARKER=1/2` | esperas pela GPU consultam um marcador escrito por ela (experimental) |
| `SOTC_CMD_STATS=1/2` | draws e dispatches por quadro (2: shaders de compute mais usados) |
| `SOTC_TSC=qpc/native` | relógio do jogo: `qpc` sempre usa o QPC do Windows, `native` sempre o TSC do processador (padrão: QPC só quando os núcleos discordam no TSC) |
