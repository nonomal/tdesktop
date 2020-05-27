/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_cloud_file.h"

#include "data/data_file_origin.h"
#include "storage/cache/storage_cache_database.h"
#include "storage/file_download.h"

namespace Data {

void UpdateCloudFile(
		CloudFile &file,
		const ImageWithLocation &data,
		Storage::Cache::Database &cache,
		uint8 cacheTag,
		Fn<void(FileOrigin)> restartLoader,
		Fn<void(QImage)> usePreloaded) {
	if (!data.location.valid()) {
		return;
	}

	const auto update = !file.location.valid()
		|| (data.location.file().cacheKey()
			&& (!file.location.file().cacheKey()
				|| (file.location.width() < data.location.width())
				|| (file.location.height() < data.location.height())));
	if (!update) {
		return;
	}
	auto cacheBytes = !data.bytes.isEmpty()
		? data.bytes
		: file.location.file().data.is<InMemoryLocation>()
		? file.location.file().data.get_unchecked<InMemoryLocation>().bytes
		: QByteArray();
	if (!cacheBytes.isEmpty()) {
		if (const auto cacheKey = data.location.file().cacheKey()) {
			cache.putIfEmpty(
				cacheKey,
				Storage::Cache::Database::TaggedValue(
					std::move(cacheBytes),
					cacheTag));
		}
	}
	file.location = data.location;
	file.byteSize = data.bytesCount;
	if (!data.preloaded.isNull()) {
		file.loader = nullptr;
		if (usePreloaded) {
			usePreloaded(data.preloaded);
		}
	} else if (file.loader) {
		const auto origin = base::take(file.loader)->fileOrigin();
		restartLoader(origin);
	}
}

void LoadCloudFile(
		CloudFile &file,
		FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag,
		Fn<bool()> finalCheck,
		Fn<void(CloudFile&)> done,
		Fn<void(bool)> fail,
		Fn<void()> progress) {
	if (file.loader) {
		if (fromCloud == LoadFromCloudOrLocal) {
			file.loader->permitLoadFromCloud();
		}
		return;
	} else if ((file.flags & CloudFile::Flag::Failed)
		|| !file.location.valid()
		|| (finalCheck && !finalCheck())) {
		return;
	}
	file.flags &= ~CloudFile::Flag::Cancelled;
	file.loader = CreateFileLoader(
		file.location.file(),
		origin,
		QString(),
		file.byteSize,
		UnknownFileLocation,
		LoadToCacheAsWell,
		fromCloud,
		autoLoading,
		cacheTag);

	const auto finish = [done](CloudFile &file) {
		if (!file.loader || file.loader->cancelled()) {
			file.flags |= CloudFile::Flag::Cancelled;
		} else {
			done(file);
		}
		// NB! file.loader may be in ~FileLoader() already.
		if (const auto loader = base::take(file.loader)) {
			if (file.flags & CloudFile::Flag::Cancelled) {
				loader->cancel();
			}
		}
	};

	file.loader->updates(
	) | rpl::start_with_next_error_done([=] {
		if (const auto onstack = progress) {
			onstack();
		}
	}, [=, &file](bool started) {
		finish(file);
		file.flags |= CloudFile::Flag::Failed;
		if (const auto onstack = fail) {
			onstack(started);
		}
	}, [=, &file] {
		finish(file);
	}, file.loader->lifetime());

	file.loader->start();
}

void LoadCloudFile(
		CloudFile &file,
		FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag,
		Fn<bool()> finalCheck,
		Fn<void(QImage)> done,
		Fn<void(bool)> fail,
		Fn<void()> progress) {
	const auto callback = [=](CloudFile &file) {
		if (auto read = file.loader->imageData(); read.isNull()) {
			file.flags |= CloudFile::Flag::Failed;
			if (const auto onstack = fail) {
				onstack(true);
			}
		} else if (const auto onstack = done) {
			onstack(std::move(read));
		}
	};
	LoadCloudFile(
		file,
		origin,
		fromCloud,
		autoLoading,
		cacheTag,
		finalCheck,
		callback,
		std::move(fail),
		std::move(progress));
}

void LoadCloudFile(
		CloudFile &file,
		FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag,
		Fn<bool()> finalCheck,
		Fn<void(QByteArray)> done,
		Fn<void(bool)> fail,
		Fn<void()> progress) {
	const auto callback = [=](CloudFile &file) {
		if (auto bytes = file.loader->bytes(); bytes.isEmpty()) {
			file.flags |= Data::CloudFile::Flag::Failed;
			if (const auto onstack = fail) {
				onstack(true);
			}
		} else if (const auto onstack = done) {
			onstack(std::move(bytes));
		}
	};
	LoadCloudFile(
		file,
		origin,
		fromCloud,
		autoLoading,
		cacheTag,
		finalCheck,
		callback,
		std::move(fail),
		std::move(progress));
}

} // namespace Data