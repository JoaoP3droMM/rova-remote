// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <rovaengine.h>

#include <chiaki/common.h>
#include <chiaki/log.h>
#include <chiaki/regist.h>

#include <QByteArray>
#include <QJsonDocument>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>

// O console responde ao registro em poucos segundos; passou disso, algo está errado (PIN expirado, rede).
static const auto REGIST_TIMEOUT = std::chrono::seconds(60);

void RovaEmit(const QJsonObject &event)
{
	QByteArray line = "@rova " + QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n";
	fwrite(line.constData(), 1, line.size(), stdout);
	fflush(stdout);
}

// O log da biblioteca vai para o stderr: o stdout fica só com os eventos para o Rova.
static void LogToStderr(ChiakiLogLevel level, const char *msg, void *)
{
	fprintf(stderr, "[%c] %s\n", chiaki_log_level_char(level), msg);
}

namespace
{
struct RegistWait
{
	std::mutex mutex;
	std::condition_variable done;
	bool finished = false;
	ChiakiRegistEventType type = CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED;
	ChiakiRegisteredHost host = {};
};
}

static void RegistCb(ChiakiRegistEvent *event, void *user)
{
	auto wait = static_cast<RegistWait *>(user);
	std::lock_guard<std::mutex> lock(wait->mutex);
	wait->type = event->type;
	if(event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS && event->registered_host)
		wait->host = *event->registered_host;
	wait->finished = true;
	wait->done.notify_all();
}

static int RegistFailed(const QString &error)
{
	RovaEmit({{"event", "regist"}, {"ok", false}, {"error", error}});
	return 1;
}

int RovaRegist(const QString &host, bool ps5, const QString &pin, const QString &account_id_b64)
{
	bool pin_ok = false;
	uint32_t pin_value = pin.trimmed().toUInt(&pin_ok);
	if(!pin_ok || pin.trimmed().length() != 8)
		return RegistFailed("pin");
	QByteArray account_id = QByteArray::fromBase64(account_id_b64.toUtf8());
	if(account_id.size() != CHIAKI_PSN_ACCOUNT_ID_SIZE)
		return RegistFailed("account-id");
	if(host.trimmed().isEmpty())
		return RegistFailed("host");

	ChiakiLog log;
	chiaki_log_init(&log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, LogToStderr, nullptr);

	QByteArray host_utf8 = host.trimmed().toUtf8();
	ChiakiRegistInfo info = {};
	info.host = host_utf8.constData();
	info.target = ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
	info.broadcast = host_utf8 == "255.255.255.255";
	info.psn_online_id = nullptr;
	memcpy(info.psn_account_id, account_id.constData(), CHIAKI_PSN_ACCOUNT_ID_SIZE);
	info.pin = pin_value;
	info.console_pin = 0;
	info.holepunch_info = nullptr;
	info.rudp = nullptr;

	RegistWait wait;
	ChiakiRegist regist;
	ChiakiErrorCode err = chiaki_regist_start(&regist, &log, &info, RegistCb, &wait);
	if(err != CHIAKI_ERR_SUCCESS)
		return RegistFailed(QString("start: %1").arg(chiaki_error_string(err)));

	bool finished;
	{
		std::unique_lock<std::mutex> lock(wait.mutex);
		finished = wait.done.wait_for(lock, REGIST_TIMEOUT, [&wait] { return wait.finished; });
	}
	if(!finished)
		chiaki_regist_stop(&regist);
	chiaki_regist_fini(&regist);

	if(!finished)
		return RegistFailed("timeout");
	if(wait.type != CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS)
		return RegistFailed(wait.type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED ? "canceled" : "failed");

	const ChiakiRegisteredHost &h = wait.host;
	// A chave de registro vem com zeros no fim; o Rova guarda só o texto (o "stream" completa os zeros).
	QByteArray regist_key(h.rp_regist_key, static_cast<int>(strnlen(h.rp_regist_key, sizeof(h.rp_regist_key))));
	QByteArray nickname(h.server_nickname, static_cast<int>(strnlen(h.server_nickname, sizeof(h.server_nickname))));
	QJsonObject event{
		{"event", "regist"},
		{"ok", true},
		{"nickname", QString::fromUtf8(nickname)},
		{"mac", QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(h.server_mac), sizeof(h.server_mac)).toHex().toUpper())},
		{"target", static_cast<int>(h.target)},
		{"console", chiaki_target_is_ps5(h.target) ? "PS5" : "PS4"},
		{"registKey", QString::fromLatin1(regist_key)},
		{"morning", QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(h.rp_key), sizeof(h.rp_key)).toBase64())},
	};
	RovaEmit(event);
	return 0;
}
