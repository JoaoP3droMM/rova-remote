# Rova Remote Play

Motor de PlayStation Remote Play do [Rova Games](https://github.com/JoaoP3droMM/i-can-play-everything),
baseado no [chiaki-ng](https://github.com/streetpea/chiaki-ng) e sob a mesma licença (AGPL-3.0, veja
`COPYING` e `LICENSES/`). O Rova baixa este motor do Release deste repositório e o chama como um programa
separado; a pessoa não instala nem abre nada além do Rova.

## O que muda em relação ao chiaki-ng

- Nome, ícone e configuração próprios (`Rova/RovaRemotePlay`), sem mexer num chiaki-ng instalado.
- `regist <ip> --pin <8 dígitos> --account-id <base64> [--console ps4]`: registra o console e devolve
  as chaves num evento, sem tela.
- `stream <nome> <ip> --registkey <chave> --morning <base64> [--console ps4]`: conexão direta, com
  `--resolution 360|540|720|1080`, `--fps 30|60`, `--bitrate <kbit/s>`, `--codec h264|h265|hdr` e
  `--on-exit sleep|nothing` (padrão `sleep`: ao fechar, o console vai para o repouso, sem perguntar —
  vale também para o X da janela).
- Fechar pelo Rova: uma linha na entrada padrão do motor — `close` (segue o `--on-exit`), `sleep`
  (repouso) ou `stop` (só desconecta). O motor avisa `closing` e, ao terminar, `quit`.
- `version`: a versão do motor e do protocolo com o Rova.
- Eventos para o Rova no stdout, uma linha por evento: `@rova {"event":"connected"}`. Eventos:
  `version`, `regist`, `starting`, `connected`, `login-pin`, `closing` (com `sleep`), `quit` (com
  `code`, `reason`, `error`, `message`).
- As opções valem antes ou depois do comando.

## Compilação

`.github/workflows/rova-release.yml` compila para Windows x64 (MSYS2) e Linux (AppImage x86_64) a cada
push em `rova/**`; uma tag `rova-v*` publica os dois no Release.
