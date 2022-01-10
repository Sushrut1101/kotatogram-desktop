/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/changelogs.h"

#include "kotato/kotato_lang.h"
#include "kotato/kotato_version.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "data/data_session.h"
#include "base/qt_adapters.h"
#include "mainwindow.h"
#include "apiwrap.h"

namespace Core {
namespace {

std::map<int, const char*> BetaLogs() {
	return {
	{
		3002006,
		"- Try out the new audio player with playlist shuffle and repeat.\n"

		"- Give a custom name to your desktop session "
		"to distinguish it in the sessions list.\n"
	},
	{
		3002007,
		"- Active sessions list redesign.\n"

		"- Fix disappearing emoji selector button.\n"

		"- Fix a crash in archived stickers loading.\n"
		
		"- Fix a crash in calls to old Telegram versions.\n"
	},
	{
		3003001,
		"- Switch between contacts list sorting modes.\n"

		"- Sort contacts list by last seen time by default.\n"

		"- Fix disappearing Send As Channel button after message editing.\n"

		"- Fix file upload cancelling.\n"

		"- Fix crash in video capture on macOS.\n"

		"- Fix labels in the About box.\n"

		"- Use Qt 6.2.2 for macOS and Linux builds.\n"

		"- Allow installing x64 Windows version on Windows ARM.\n"
	},
	{
		3003002,
		"- Select text when typing and choose 'Formatting > Spoiler' in the "
		"context menu to hide some or all of the contents of a message.\n"

		"- Click on the spoiler in chat to reveal its hidden text.\n"

		"- Spoiler formatting hides text in chat, "
		"as well as in the chat list and notifications.\n"
	}
	};
};

} // namespace

Changelogs::Changelogs(not_null<Main::Session*> session, int oldVersion, int oldKotatoVersion)
: _session(session)
, _oldVersion(oldVersion)
, _oldKotatoVersion(oldKotatoVersion) {

	LOG(("Previous Kotatogram version: %1").arg(_oldKotatoVersion));

	_session->data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return !folder;
	}) | rpl::start_with_next([=] {
		addKotatoLogs();
	}, _chatsSubscription);
}

std::unique_ptr<Changelogs> Changelogs::Create(
		not_null<Main::Session*> session) {
	auto &local = Core::App().domain().local();
	const auto oldVersion = local.oldVersion();
	const auto oldKotatoVersion = Local::oldKotatoVersion();
	local.clearOldVersion();
	return (oldVersion != 0
		&& oldKotatoVersion < AppKotatoVersion)
		? std::make_unique<Changelogs>(session, oldVersion, oldKotatoVersion)
		: nullptr;
}

void Changelogs::addKotatoLogs() {
	_chatsSubscription.destroy();
	
	if (_addedSomeLocal) {
		return;
	}
	auto baseLang = Lang::GetInstance().baseId();
	auto currentLang = Lang::Id();
	QString channelLink;

	for (const auto language : { "ru", "uk", "be" }) {
		if (baseLang.startsWith(QLatin1String(language)) || currentLang == QString(language)) {
			channelLink = "https://t.me/kotatogram_ru";
			break;
		}
	}

	if (channelLink.isEmpty()) {
		channelLink = "https://t.me/kotatogram";
	}

	const auto text = ktr("ktg_new_version",
		{ "version", QString::fromLatin1(AppKotatoVersionStr) },
		{ "td_version", QString::fromLatin1(AppVersionStr) },
		{ "link", channelLink });
	addLocalLog(text.trimmed());
}

void Changelogs::requestCloudLogs() {
	_chatsSubscription.destroy();

	const auto callback = [this](const MTPUpdates &result) {
		_session->api().applyUpdates(result);

		auto resultEmpty = true;
		switch (result.type()) {
		case mtpc_updateShortMessage:
		case mtpc_updateShortChatMessage:
		case mtpc_updateShort:
			resultEmpty = false;
			break;
		case mtpc_updatesCombined:
			resultEmpty = result.c_updatesCombined().vupdates().v.isEmpty();
			break;
		case mtpc_updates:
			resultEmpty = result.c_updates().vupdates().v.isEmpty();
			break;
		case mtpc_updatesTooLong:
		case mtpc_updateShortSentMessage:
			LOG(("API Error: Bad updates type in app changelog."));
			break;
		}
		if (resultEmpty) {
			addLocalLogs();
		}
	};
	_session->api().requestChangelog(
		FormatVersionPrecise(_oldVersion),
		crl::guard(this, callback));
}

void Changelogs::addLocalLogs() {
	if (AppKotatoBetaVersion || cAlphaVersion()) {
		addBetaLogs();
	}
	if (!_addedSomeLocal) {
		const auto text = tr::lng_new_version_wrap(
			tr::now,
			lt_version,
			QString::fromLatin1(AppVersionStr),
			lt_changes,
			tr::lng_new_version_minor(tr::now),
			lt_link,
			Core::App().changelogLink());
		addLocalLog(text.trimmed());
	}
}

void Changelogs::addLocalLog(const QString &text) {
	auto textWithEntities = TextWithEntities{ text };
	TextUtilities::ParseEntities(textWithEntities, TextParseLinks);
	_session->data().serviceNotification(textWithEntities);
	_addedSomeLocal = true;
};

void Changelogs::addBetaLogs() {
	for (const auto &[version, changes] : BetaLogs()) {
		addBetaLog(version, changes);
	}
}

void Changelogs::addBetaLog(int changeVersion, const char *changes) {
	if (_oldVersion >= changeVersion) {
		return;
	}
	const auto text = [&] {
		static const auto simple = u"\n- "_q;
		static const auto separator = QString::fromUtf8("\n\xE2\x80\xA2 ");
		auto result = QString::fromUtf8(changes).trimmed();
		if (result.startsWith(base::StringViewMid(simple, 1))) {
			result = separator.mid(1) + result.mid(simple.size() - 1);
		}
		return result.replace(simple, separator);
	}();
	const auto version = FormatVersionDisplay(changeVersion);
	const auto log = qsl("New in version %1 beta:\n\n").arg(version) + text;
	addLocalLog(log);
}

QString FormatVersionDisplay(int version) {
	return QString::number(version / 1000000)
		+ '.' + QString::number((version % 1000000) / 1000)
		+ ((version % 1000)
			? ('.' + QString::number(version % 1000))
			: QString());
}

QString FormatVersionPrecise(int version) {
	return QString::number(version / 1000000)
		+ '.' + QString::number((version % 1000000) / 1000)
		+ '.' + QString::number(version % 1000);
}

} // namespace Core
