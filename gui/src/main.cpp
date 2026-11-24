
// Use the native entry point instead of SDL's WinMain wrapper.
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#endif
#ifdef main
#undef main
#endif

// ugly workaround because Windows does weird things and ENOTIME
int real_main(int argc, char *argv[]);
int main(int argc, char *argv[]) { return real_main(argc, argv); }

#include <streamsession.h>
#include <settings.h>
#include <host.h>
#include <controllermanager.h>
#include <discoverymanager.h>
#include <qmlmainwindow.h>
#include <rovaengine.h>
#include <QApplication>
#include <QScreen>
#include <QtTypes>

#ifdef CHIAKI_ENABLE_CLI
#include <chiaki-cli.h>
#endif

#include <chiaki/session.h>
#include <chiaki/regist.h>
#include <chiaki/base64.h>

#include <stdio.h>
#include <string.h>

#ifdef CHIAKI_HAVE_WEBENGINE
#include <QtWebEngineQuick>
#endif

#include <QCommandLineParser>
#include <QMap>
#include <QSurfaceFormat>

Q_DECLARE_METATYPE(ChiakiLogLevel)
Q_DECLARE_METATYPE(ChiakiRegistEventType)

#if defined(CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE) && defined(Q_OS_LINUX)
#include <QtPlugin>
Q_IMPORT_PLUGIN(SDInputContextPlugin)
#endif

#ifdef CHIAKI_ENABLE_CLI
struct CLICommand
{
	int (*cmd)(ChiakiLog *log, int argc, char *argv[]);
};

static const QMap<QString, CLICommand> cli_commands = {
	{ "discover", { chiaki_cli_cmd_discover } },
	{ "wakeup", { chiaki_cli_cmd_wakeup } }
};
#endif

int RunStream(QGuiApplication &app, const StreamSessionConnectInfo &connect_info, const QString &screen_at = QString(), const QString &screen_name = QString(), const QString &screen_size = QString());
int RunMain(QGuiApplication &app, Settings *settings, bool exit_app_on_stream_exit);
static bool ApplyStreamQuality(Settings *settings, bool ps5, const QString &resolution, const QString &fps, const QString &bitrate, const QString &codec);

int real_main(int argc, char *argv[])
{
	qRegisterMetaType<DiscoveryHost>();
	qRegisterMetaType<RegisteredHost>();
	qRegisterMetaType<HostMAC>();
	qRegisterMetaType<ChiakiQuitReason>();
	qRegisterMetaType<ChiakiRegistEventType>();
	qRegisterMetaType<ChiakiLogLevel>();

	// Rova Remote Play: nome e configuração próprios, separados de um chiaki-ng instalado no mesmo PC.
	QGuiApplication::setOrganizationName("Rova");
	QGuiApplication::setApplicationName("RovaRemotePlay");
	QGuiApplication::setApplicationVersion(CHIAKI_VERSION);
	QGuiApplication::setApplicationDisplayName("Rova Remote Play");
#if defined(Q_OS_MACOS)
	// Use a plain CAMetalLayer for Vulkan windows. Qt's own layer presents in step with each
	// live resize, but our swapchain presents from the render thread, so every resize step
	// stalled and resizing the window became very slow.
	qputenv("QT_MTL_NO_TRANSACTION", "1");
	// The Vulkan loader also reads Homebrew's driver folder, so with Homebrew's molten-vk installed
	// the app loaded a second MoltenVK beside its own and listed every GPU twice. Use only the
	// bundled one. Setting any of these variables yourself keeps your choice.
	const QString moltenvk_icd = QFileInfo(QString::fromLocal8Bit(argv[0])).canonicalPath()
		+ "/../Resources/vulkan/icd.d/MoltenVK_icd.json";
	if(QFileInfo::exists(moltenvk_icd) && !qEnvironmentVariableIsSet("VK_DRIVER_FILES")
		&& !qEnvironmentVariableIsSet("VK_ICD_FILENAMES") && !qEnvironmentVariableIsSet("VK_LOADER_DRIVERS_SELECT")
		&& !qEnvironmentVariableIsSet("VK_LOADER_DRIVERS_DISABLE"))
		qputenv("VK_DRIVER_FILES", QFileInfo(moltenvk_icd).canonicalFilePath().toUtf8());
#endif
#if defined(Q_OS_LINUX)
	if(qEnvironmentVariableIsSet("FLATPAK_ID"))
		QGuiApplication::setDesktopFileName(qEnvironmentVariable("FLATPAK_ID"));
	else
#endif
		QGuiApplication::setDesktopFileName("rova-remote-play");

	qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
#if defined(Q_OS_WIN)
	const size_t cSize = strlen(argv[0])+1;
	wchar_t wc[cSize];
	mbstowcs (wc, argv[0], cSize);
	QString import_path = QFileInfo(QString::fromWCharArray(wc)).dir().absolutePath() + "/qml";
	qputenv("QML_IMPORT_PATH", import_path.toUtf8());
	// Microsoft's OpenCL, OpenGL and Vulkan Compatibility Pack adds the Dozen driver (Vulkan on
	// D3D12) beside the GPU's own, and loading it can crash the app even when it isn't used. Where
	// Dozen is the only Vulkan driver, Vulkan then finds no device and chiaki-ng uses OpenGL,
	// which the pack also provides. Setting either variable yourself keeps your choice.
	if(!qEnvironmentVariableIsSet("VK_LOADER_DRIVERS_DISABLE") && !qEnvironmentVariableIsSet("VK_LOADER_DRIVERS_SELECT"))
		qputenv("VK_LOADER_DRIVERS_DISABLE", "*dzn*");
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
	qputenv("ANV_VIDEO_DECODE", "1");
	qputenv("RADV_PERFTEST", "video_decode");
	// Rova: o Mesa 26 trocou RADV_PERFTEST por RADV_EXPERIMENTAL; sem ele a placa AMD recusa o HEVC pelo
	// Vulkan ("Device does not support decoding hevc") e o vídeo cai para outro decodificador.
	if(!qEnvironmentVariableIsSet("RADV_EXPERIMENTAL"))
		qputenv("RADV_EXPERIMENTAL", "video_decode");
#endif
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
	if (qEnvironmentVariableIsSet("SteamDeck"))
		qputenv("QT_IM_MODULE", "sdinput");
#endif

	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "Chiaki lib init failed: %s\n", chiaki_error_string(err));
		return 1;
	}

    SDL_SetHint(SDL_HINT_APP_NAME, "Rova Remote Play");

	if(SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		fprintf(stderr, "SDL Audio init failed: %s\n", SDL_GetError());
		return 1;
	}

	QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#ifdef CHIAKI_HAVE_WEBENGINE
	QtWebEngineQuick::initialize();
#endif
#if defined(Q_OS_LINUX)
	// Rova: "version" e "regist" não abrem janela; num Linux sem tela (terminal sem DISPLAY/WAYLAND, CI)
	// rodam igual. Só no Linux: no Windows sempre há tela, e o pacote não leva o módulo offscreen (faltando,
	// o Qt abre uma caixa de erro e espera um clique).
	const bool no_display = !qEnvironmentVariableIsSet("DISPLAY") && !qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
	for(int i = 1; i < argc && no_display && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"); i++)
	{
		if(strcmp(argv[i], "version") == 0 || strcmp(argv[i], "regist") == 0)
			qputenv("QT_QPA_PLATFORM", "offscreen");
	}
#endif
	QApplication app(argc, argv);

#ifdef Q_OS_MACOS
	QGuiApplication::setWindowIcon(QIcon(":/icons/chiaking_macos.svg"));
#else
	QGuiApplication::setWindowIcon(QIcon(":/icons/rova.png"));
#endif

	QCommandLineParser parser;
	// Rova: as opções valem em qualquer posição (o chiaki-ng só lia as que vinham antes do comando).
	parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
	parser.addHelpOption();
	
	QStringList cmds;
	cmds.append("stream");
	cmds.append("list");
	cmds.append("regist");
	cmds.append("version");
#ifdef CHIAKI_ENABLE_CLI
	cmds.append(cli_commands.keys());
#endif

	parser.addPositionalArgument("command", cmds.join(", "));
	parser.addPositionalArgument("nickname", "Needed for stream command to get credentials for connecting. "
			"Use 'list' to get the nickname.");
	parser.addPositionalArgument("host", "Address to connect to (when using the stream command).");

	QCommandLineOption profile_option("profile", "", "profile", "Configuration profile");
	parser.addOption(profile_option);

	QCommandLineOption stream_exit_option("exit-app-on-stream-exit", "Exit the GUI application when the stream session ends.");
	parser.addOption(stream_exit_option);

	QCommandLineOption regist_key_option("registkey", "", "registkey");
	parser.addOption(regist_key_option);

	QCommandLineOption morning_option("morning", "", "morning");
	parser.addOption(morning_option);

	QCommandLineOption fullscreen_option("fullscreen", "Start window in fullscreen mode [maintains aspect ratio, adds black bars to fill unsused parts of screen if applicable] (only for use with stream command).");
	parser.addOption(fullscreen_option);

	QCommandLineOption dualsense_option("dualsense", "Enable DualSense haptics and adaptive triggers (PS5 and DualSense connected via USB only).");
	parser.addOption(dualsense_option);

	QCommandLineOption zoom_option("zoom", "Start window in fullscreen zoomed in to fit screen [maintains aspect ratio, cutting off edges of image to fill screen] (only for use with stream command)");
	parser.addOption(zoom_option);

	QCommandLineOption stretch_option("stretch", "Start window in fullscreen stretched to fit screen [distorts aspect ratio to fill screen] (only for use with stream command).");
	parser.addOption(stretch_option);

	QCommandLineOption passcode_option("passcode", "Automatically send your PlayStation login passcode (only affects users with a login passcode set on their PlayStation console).", "passcode");
	parser.addOption(passcode_option);

	// Rova: registro com o PIN, console da conexão direta e qualidade da transmissão.
	QCommandLineOption pin_option("pin", "PIN of 8 digits from Settings > System > Remote Play > Link Device (regist command).", "pin");
	parser.addOption(pin_option);

	QCommandLineOption account_id_option("account-id", "PSN account id, base64 (regist command).", "account-id");
	parser.addOption(account_id_option);

	QCommandLineOption console_option("console", "ps5 or ps4: the console type for regist, and for stream with --registkey/--morning (default ps5).", "console", "ps5");
	parser.addOption(console_option);

	QCommandLineOption resolution_option("resolution", "Stream resolution: 360, 540, 720 or 1080 (stream command).", "resolution");
	parser.addOption(resolution_option);

	QCommandLineOption fps_option("fps", "Stream frame rate: 30 or 60 (stream command).", "fps");
	parser.addOption(fps_option);

	QCommandLineOption bitrate_option("bitrate", "Stream bitrate in kbit/s (stream command).", "bitrate");
	parser.addOption(bitrate_option);

	QCommandLineOption codec_option("codec", "Stream codec: h264, h265 or hdr (stream command).", "codec");
	parser.addOption(codec_option);

	QCommandLineOption screen_at_option("screen-at", "x,y: open the stream on the screen that contains this point (Rova sends the center of its own screen).", "x,y");
	parser.addOption(screen_at_option);

	QCommandLineOption screen_name_option("screen-name", "Open the stream on the screen with this name or model, when --screen-at matches none.", "name");
	parser.addOption(screen_name_option);

	QCommandLineOption screen_size_option("screen-size", "WxH in real pixels: the screen with this size, when --screen-at and --screen-name match none (screens with different scaling).", "WxH");
	parser.addOption(screen_size_option);

	QCommandLineOption on_exit_option("on-exit", "sleep or nothing: what happens to the console when the stream is closed (stream command, default sleep, never asks).", "on-exit", "sleep");
	parser.addOption(on_exit_option);

	QCommandLineOption opengl_option("opengl", "Switch the renderer to OpenGL from now on, for systems where Vulkan shows a blank window or crashes (switch back under Settings > Video > Renderer Backend).");
	parser.addOption(opengl_option);

	parser.process(app);
	QStringList args = parser.positionalArguments();

	const QString console_type = parser.value(console_option).toLower();
	if(console_type != "ps5" && console_type != "ps4")
	{
		fprintf(stderr, "--console must be ps5 or ps4\n");
		return 1;
	}
	if(args.length() > 0 && args[0] == "version")
	{
		RovaEmit({{"event", "version"}, {"version", CHIAKI_VERSION}, {"protocol", ROVA_ENGINE_PROTOCOL}});
		return 0;
	}
	if(args.length() > 0 && args[0] == "regist")
	{
		if(args.length() < 2)
			parser.showHelp(1);
		return RovaRegist(args[1], console_type == "ps5", parser.value(pin_option), parser.value(account_id_option));
	}

	Settings settings(parser.isSet(profile_option) ? parser.value(profile_option) : QString());
	bool exit_app_on_stream_exit = parser.isSet(stream_exit_option);
	if(parser.isSet(profile_option))
		settings.SetCurrentProfile(parser.value(profile_option));
	Settings alt_settings(parser.isSet(profile_option) ? "" : settings.GetCurrentProfile());
	if(!settings.GetCurrentProfile().isEmpty())
		QGuiApplication::setApplicationDisplayName(QString("chiaki-ng:%1").arg(settings.GetCurrentProfile()));
	bool use_alt_settings = false;
	if(!parser.isSet(profile_option))
		use_alt_settings = true;

	if(parser.isSet(opengl_option))
	{
		Settings *active_settings = use_alt_settings ? &alt_settings : &settings;
		active_settings->SetRenderBackend(RenderBackend::OpenGL);
		active_settings->SetVulkanStartupPending(false);
		active_settings->Sync();
	}

	if(args.length() == 0)
		return RunMain(app, use_alt_settings ? &alt_settings : &settings, exit_app_on_stream_exit);

	if(args[0] == "list")
	{
		for(const auto &host : settings.GetRegisteredHosts())
			printf("Host: %s \n", host.GetServerNickname().toLocal8Bit().constData());
		return 0;
	}
	if(args[0] == "stream")
	{
		if(args.length() < 2)
			parser.showHelp(1);

		//QString host = args[sizeof(args) -1]; //the ip is always the last param for stream
		QString host = args[args.size()-1];
		QByteArray morning;
		QByteArray regist_key;
		QString initial_login_passcode;
		ChiakiTarget target = CHIAKI_TARGET_PS4_10;

		if(parser.value(regist_key_option).isEmpty() && parser.value(morning_option).isEmpty())
		{
			if(args.length() < 3)
				parser.showHelp(1);

			bool found = false;
			for(const auto &temphost : settings.GetRegisteredHosts())
			{
				if(temphost.GetServerNickname() == args[1])
				{
					found = true;
					morning = temphost.GetRPKey();
					regist_key = temphost.GetRPRegistKey();
					target = temphost.GetTarget();
					break;
				}
			}
			if(!found)
			{
				printf("No configuration found for '%s'\n", args[1].toLocal8Bit().constData());
				return 1;
			}
		}
		else
		{
			target = console_type == "ps5" ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
			regist_key = parser.value(regist_key_option).toUtf8();
			if(regist_key.length() > sizeof(ChiakiConnectInfo::regist_key))
			{
				printf("Given regist key is too long (expected size <=%llu, got %" PRIdQSIZETYPE")\n",
					(unsigned long long)sizeof(ChiakiConnectInfo::regist_key),
					regist_key.length());
				return 1;
			}
			regist_key += QByteArray(sizeof(ChiakiConnectInfo::regist_key) - regist_key.length(), 0);
			morning = QByteArray::fromBase64(parser.value(morning_option).toUtf8());
			if(morning.length() != sizeof(ChiakiConnectInfo::morning))
			{
				printf("Given morning has invalid size (expected %llu, got %" PRIdQSIZETYPE")\n",
					(unsigned long long)sizeof(ChiakiConnectInfo::morning),
					morning.length());
				printf("Given morning has invalid size (expected %llu)", (unsigned long long)sizeof(ChiakiConnectInfo::morning));
				return 1;
			}
		}
		if ((parser.isSet(stretch_option) && (parser.isSet(zoom_option) || parser.isSet(fullscreen_option))) || (parser.isSet(zoom_option) && parser.isSet(fullscreen_option)))
		{
			printf("Must choose between fullscreen, zoom or stretch option.");
			return 1;
		}
		if(parser.value(passcode_option).isEmpty())
		{
			//Set to empty if it wasn't given by user.
			initial_login_passcode = QString("");
		}
		else
		{
			initial_login_passcode = parser.value(passcode_option);
			if(initial_login_passcode.length() != 4)
			{
				printf("Login passcode must be 4 digits. You entered %" PRIdQSIZETYPE "digits)\n", initial_login_passcode.length());
				return 1;
			}
		}
		
		const QString on_exit = parser.value(on_exit_option).toLower();
		if(on_exit != "sleep" && on_exit != "nothing")
		{
			fprintf(stderr, "--on-exit must be sleep or nothing\n");
			return 1;
		}
		{
			// Rova: nunca a pergunta "pôr em repouso?" ao fechar; vale também para o X da janela.
			Settings *active = use_alt_settings ? &alt_settings : &settings;
			active->SetDisconnectAction(on_exit == "sleep" ? DisconnectAction::AlwaysSleep : DisconnectAction::AlwaysNothing);
			active->Sync();
		}
		if(!ApplyStreamQuality(use_alt_settings ? &alt_settings : &settings, chiaki_target_is_ps5(target),
				parser.value(resolution_option), parser.value(fps_option), parser.value(bitrate_option), parser.value(codec_option)))
			return 1;

		StreamSessionConnectInfo connect_info(
				use_alt_settings ? &alt_settings : &settings,
				target,
				std::move(host),
				QString(),
				std::move(regist_key),
				std::move(morning),
				std::move(initial_login_passcode),
				QString(),
				false,
				parser.isSet(fullscreen_option),
				parser.isSet(zoom_option),
				parser.isSet(stretch_option));

		return RunStream(app, connect_info, parser.value(screen_at_option), parser.value(screen_name_option), parser.value(screen_size_option));
	}
#ifdef CHIAKI_ENABLE_CLI
	else if(cli_commands.contains(args[0]))
	{
		ChiakiLog log;
		// TODO: add verbose arg
		chiaki_log_init(&log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, chiaki_log_cb_print, nullptr);

		const auto &cmd = cli_commands[args[0]];
		int sub_argc = args.count();
		QVector<QByteArray> sub_argv_b(sub_argc);
		QVector<char *> sub_argv(sub_argc);
		for(size_t i=0; i<sub_argc; i++)
		{
			sub_argv_b[i] = args[i].toLocal8Bit();
			sub_argv[i] = sub_argv_b[i].data();
		}
		return cmd.cmd(&log, sub_argc, sub_argv.data());
	}
#endif
	else
	{
		parser.showHelp(1);
	}
}

/**
 * Rova: grava a qualidade pedida na linha de comando (vazio = mantém a configuração) para o tipo de
 * console, na rede local e fora dela. Devolve false com o valor inválido no stderr.
 */
static bool ApplyStreamQuality(Settings *settings, bool ps5, const QString &resolution, const QString &fps, const QString &bitrate, const QString &codec)
{
	if(!resolution.isEmpty())
	{
		static const QMap<QString, ChiakiVideoResolutionPreset> presets = {
			{"360", CHIAKI_VIDEO_RESOLUTION_PRESET_360p}, {"540", CHIAKI_VIDEO_RESOLUTION_PRESET_540p},
			{"720", CHIAKI_VIDEO_RESOLUTION_PRESET_720p}, {"1080", CHIAKI_VIDEO_RESOLUTION_PRESET_1080p}};
		if(!presets.contains(resolution))
		{
			fprintf(stderr, "--resolution must be 360, 540, 720 or 1080\n");
			return false;
		}
		if(ps5) { settings->SetResolutionLocalPS5(presets[resolution]); settings->SetResolutionRemotePS5(presets[resolution]); }
		else { settings->SetResolutionLocalPS4(presets[resolution]); settings->SetResolutionRemotePS4(presets[resolution]); }
	}
	if(!fps.isEmpty())
	{
		if(fps != "30" && fps != "60")
		{
			fprintf(stderr, "--fps must be 30 or 60\n");
			return false;
		}
		auto preset = fps == "60" ? CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30;
		if(ps5) { settings->SetFPSLocalPS5(preset); settings->SetFPSRemotePS5(preset); }
		else { settings->SetFPSLocalPS4(preset); settings->SetFPSRemotePS4(preset); }
	}
	if(!bitrate.isEmpty())
	{
		bool ok = false;
		unsigned int value = bitrate.toUInt(&ok);
		if(!ok || value < 2000 || value > 100000)
		{
			fprintf(stderr, "--bitrate must be between 2000 and 100000 kbit/s\n");
			return false;
		}
		if(ps5) { settings->SetBitrateLocalPS5(value); settings->SetBitrateRemotePS5(value); }
		else { settings->SetBitrateLocalPS4(value); settings->SetBitrateRemotePS4(value); }
	}
	if(!codec.isEmpty())
	{
		static const QMap<QString, ChiakiCodec> codecs = {{"h264", CHIAKI_CODEC_H264}, {"h265", CHIAKI_CODEC_H265}, {"hdr", CHIAKI_CODEC_H265_HDR}};
		if(!codecs.contains(codec) || (!ps5 && codec != "h264"))
		{
			fprintf(stderr, "--codec must be h264, h265 or hdr (PS4: h264 only)\n");
			return false;
		}
		if(ps5) { settings->SetCodecLocalPS5(codecs[codec]); settings->SetCodecRemotePS5(codecs[codec]); }
		else settings->SetCodecPS4(codecs[codec]);
	}
	settings->Sync();
	return true;
}

int RunMain(QGuiApplication &app, Settings *settings, bool exit_app_on_stream_exit)
{
	QmlMainWindow main_window(settings, exit_app_on_stream_exit);
	main_window.show();
	return app.exec();
}

/**
 * Rova: a tela onde a transmissão abre. Com dois monitores, a janela ia para a tela principal do sistema
 * (às vezes a TV), não para onde o Rova está. `at`: "x,y" de um ponto da tela do Rova; `name`: nome ou
 * modelo do monitor, se o ponto não cair em nenhuma tela (escalas diferentes); `size`: "LxA" em pixels reais,
 * se só uma tela tem esse tamanho. nullptr: a do sistema (nunca trava por não achar).
 */
static QScreen *RovaTargetScreen(const QString &at, const QString &name, const QString &size)
{
	const QStringList xy = at.split(',');
	if(xy.size() == 2)
	{
		bool okx = false, oky = false;
		const QPoint point(xy[0].trimmed().toInt(&okx), xy[1].trimmed().toInt(&oky));
		if(okx && oky)
			if(QScreen *screen = QGuiApplication::screenAt(point))
				return screen;
	}
	const QString wanted = name.trimmed();
	if(!wanted.isEmpty())
	{
		for(QScreen *screen : QGuiApplication::screens())
		{
			if(screen->name().compare(wanted, Qt::CaseInsensitive) == 0)
				return screen;
			if(!screen->model().isEmpty() && wanted.contains(screen->model(), Qt::CaseInsensitive))
				return screen;
		}
	}
	const QStringList wh = size.toLower().split('x');
	if(wh.size() == 2)
	{
		const QSize wanted_size(wh[0].trimmed().toInt(), wh[1].trimmed().toInt());
		QScreen *found = nullptr;
		int matches = 0;
		for(QScreen *screen : QGuiApplication::screens())
		{
			const QSize real = (QSizeF(screen->geometry().size()) * screen->devicePixelRatio()).toSize();
			if(real == wanted_size || screen->geometry().size() == wanted_size)
			{
				found = screen;
				matches++;
			}
		}
		if(matches == 1)
			return found;
	}
	return nullptr;
}

int RunStream(QGuiApplication &app, const StreamSessionConnectInfo &connect_info, const QString &screen_at, const QString &screen_name, const QString &screen_size)
{
	if(QScreen *screen = RovaTargetScreen(screen_at, screen_name, screen_size))
	{
		fprintf(stderr, "Rova: transmissão na tela %s (%s)\n", qPrintable(screen->name()), qPrintable(screen->model()));
		RovaSetStreamScreen(screen);
	}
	QmlMainWindow main_window(connect_info);
	// Rova: com tela cheia pedida, abre já em tela cheia. O show() comum, chamado depois, desfazia a tela
	// cheia no X11 (janela com bordas e barra de título).
	if(connect_info.fullscreen || connect_info.zoom || connect_info.stretch)
		main_window.showFullScreen();
	else
		main_window.show();
	return app.exec();
}
