// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Rova-Remote: o que o Rova Games precisa do motor, sem janela nem tela do chiaki-ng.
// - Eventos em JSON, uma linha por evento no stdout, começando com "@rova " (o Rova lê só essas
//   linhas; o resto do stdout é log). Ex.: @rova {"event":"connected"}
// - "regist": registra o console com o PIN e devolve as chaves num evento, sem gravar nada.

#ifndef ROVA_ENGINE_H
#define ROVA_ENGINE_H

#include <QJsonObject>
#include <QString>

#include <functional>

// Versão do que o Rova pode pedir ao motor (comandos, opções e eventos). Sobe a cada mudança nisso.
#define ROVA_ENGINE_PROTOCOL 3

/** Escreve um evento para o Rova (uma linha "@rova {json}" no stdout, já descarregada). */
void RovaEmit(const QJsonObject &event);

/**
 * Registra o console em `host` (IP, ou 255.255.255.255 para a rede toda) com o PIN de 8 dígitos da tela
 * "Vincular dispositivo" e o ID da conta PSN em base64. Emite {"event":"regist","ok":true,...} com
 * nickname, mac, target, registKey e morning (base64), ou {"event":"regist","ok":false,"error":...}.
 * Devolve o código de saída do programa (0 = registrado).
 */
int RovaRegist(const QString &host, bool ps5, const QString &pin, const QString &account_id_b64);

/**
 * Comandos do Rova pela entrada padrão, um por linha ("close", "sleep", "stop"). Cada linha chega a
 * `handler` na thread principal. Fim da entrada (Rova sem pipe, ou /dev/null) não faz nada.
 */
void RovaListenCommands(std::function<void(const QString &)> handler);

class QScreen;

/**
 * A tela escolhida para a transmissão (--screen-at / --screen-name): a janela vai para ela antes de entrar
 * em tela cheia. nullptr = a tela principal do sistema.
 */
void RovaSetStreamScreen(QScreen *screen);
QScreen *RovaStreamScreen();

#endif // ROVA_ENGINE_H
