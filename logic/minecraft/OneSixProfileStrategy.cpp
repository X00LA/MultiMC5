#include "minecraft/OneSixProfileStrategy.h"

#include <QDir>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QResource>

#include "minecraft/VersionBuildError.h"
#include "minecraft/OneSixInstance.h"
#include "wonko/WonkoIndex.h"
#include "wonko/WonkoVersionList.h"
#include "Env.h"
#include <FileSystem.h>


OneSixProfileStrategy::OneSixProfileStrategy(OneSixInstance* instance)
{
	m_instance = instance;
}

void OneSixProfileStrategy::upgradeDeprecatedFiles()
{
	const QString versionJsonPath = FS::PathCombine(m_instance->instanceRoot(), "version.json");
	const QString customJsonPath = FS::PathCombine(m_instance->instanceRoot(), "custom.json");
	const QString mcJson = FS::PathCombine(m_instance->instanceRoot(), "patches" , "net.minecraft.json");

	QString sourceFile;
	QString renameFile;

	// convert old crap.
	if(QFile::exists(customJsonPath))
	{
		sourceFile = customJsonPath;
		renameFile = versionJsonPath;
	}
	else if(QFile::exists(versionJsonPath))
	{
		sourceFile = versionJsonPath;
	}
	if(!sourceFile.isEmpty() && !QFile::exists(mcJson))
	{
		if(!FS::ensureFilePathExists(mcJson))
		{
			qWarning() << "Couldn't create patches folder for" << m_instance->name();
			return;
		}
		if(!renameFile.isEmpty() && QFile::exists(renameFile))
		{
			if(!QFile::rename(renameFile, renameFile + ".old"))
			{
				qWarning() << "Couldn't rename" << renameFile << "to" << renameFile + ".old" << "in" << m_instance->name();
				return;
			}
		}
		auto file = ProfileUtils::parseJsonFile(QFileInfo(sourceFile), false);
		ProfileUtils::removeLwjglFromPatch(file);
		file->fileId = "net.minecraft";
		file->version = file->id;
		file->name = "Minecraft";
		auto data = file->toJson(false).toJson();
		QSaveFile newPatchFile(mcJson);
		if(!newPatchFile.open(QIODevice::WriteOnly))
		{
			newPatchFile.cancelWriting();
			qWarning() << "Couldn't open main patch for writing in" << m_instance->name();
			return;
		}
		newPatchFile.write(data);
		if(!newPatchFile.commit())
		{
			qWarning() << "Couldn't save main patch in" << m_instance->name();
			return;
		}
		if(!QFile::rename(sourceFile, sourceFile + ".old"))
		{
			qWarning() << "Couldn't rename" << sourceFile << "to" << sourceFile + ".old" << "in" << m_instance->name();
			return;
		}
	}
}

void OneSixProfileStrategy::loadPatches()
{
	// load all patches, put into map for ordering, apply in the right order
	ProfileUtils::PatchOrder userOrder;
	ProfileUtils::readOverrideOrders(FS::PathCombine(m_instance->instanceRoot(), "order.json"), userOrder);
	QDir patches(FS::PathCombine(m_instance->instanceRoot(),"patches"));

	// first, load things by sort order.
	for (auto id : userOrder)
	{
		// parse the file
		QString filename = patches.absoluteFilePath(id + ".json");
		QFileInfo finfo(filename);
		if(!finfo.exists())
		{
			qDebug() << "Patch file " << filename << " was deleted by external means...";
			continue;
		}
		qDebug() << "Reading" << filename << "by user order";
		auto file = ProfileUtils::parseJsonFile(finfo, false);
		// sanity check. prevent tampering with files.
		if (file->fileId != id)
		{
			throw VersionBuildError(
				QObject::tr("load id %1 does not match internal id %2").arg(id, file->fileId));
		}
		file->setRemovable(true);
		file->setMovable(true);
		profile->appendPatch(file);
	}
	// now load the rest by internal preference.
	QMap<int, QPair<QString, VersionFilePtr>> files;
	for (auto info : patches.entryInfoList(QStringList() << "*.json", QDir::Files))
	{
		// parse the file
		qDebug() << "Reading" << info.fileName();
		auto file = ProfileUtils::parseJsonFile(info, true);
		// do not load what we already loaded in the first pass
		if (userOrder.contains(file->fileId))
			continue;
		if (files.contains(file->order))
		{
			// FIXME: do not throw?
			throw VersionBuildError(QObject::tr("%1 has the same order as %2")
										.arg(file->fileId, files[file->order].second->fileId));
		}
		file->setRemovable(true);
		file->setMovable(true);
		files.insert(file->order, qMakePair(info.fileName(), file));
	}
	for (auto order : files.keys())
	{
		auto &filePair = files[order];
		profile->appendPatch(filePair.second);
	}
}


void OneSixProfileStrategy::load()
{
	profile->clearPatches();

	upgradeDeprecatedFiles();
	loadPatches();

	profile->finalize();
}

bool OneSixProfileStrategy::saveOrder(ProfileUtils::PatchOrder order)
{
	return ProfileUtils::writeOverrideOrders(FS::PathCombine(m_instance->instanceRoot(), "order.json"), order);
}

bool OneSixProfileStrategy::resetOrder()
{
	return QDir(m_instance->instanceRoot()).remove("order.json");
}

bool OneSixProfileStrategy::removePatch(ProfilePatchPtr patch)
{
	bool ok = true;
	// first, remove the patch file. this ensures it's not used anymore
	auto fileName = patch->getPatchFilename();
	if(fileName.size())
	{
		QFile patchFile(fileName);
		if(patchFile.exists() && !patchFile.remove())
		{
			qCritical() << "File" << fileName << "could not be removed because:" << patchFile.errorString();
			return false;
		}
	}


	auto preRemoveJarMod = [&](JarmodPtr jarMod) -> bool
	{
		QString fullpath = FS::PathCombine(m_instance->jarModsDir(), jarMod->name);
		QFileInfo finfo (fullpath);
		if(finfo.exists())
		{
			QFile jarModFile(fullpath);
			if(!jarModFile.remove())
			{
				qCritical() << "File" << fullpath << "could not be removed because:" << jarModFile.errorString();
				return false;
			}
			return true;
		}
		return true;
	};

	for(auto &jarmod: patch->getJarMods())
	{
		ok &= preRemoveJarMod(jarmod);
	}
	return ok;
}

bool OneSixProfileStrategy::customizePatch(ProfilePatchPtr patch)
{
	if(patch->isCustom())
	{
		return false;
	}

	auto filename = FS::PathCombine(m_instance->instanceRoot(), "patches" , patch->getPatchID() + ".json");
	if(!FS::ensureFilePathExists(filename))
	{
		return false;
	}
	try
	{
		QSaveFile jsonFile(filename);
		if(!jsonFile.open(QIODevice::WriteOnly))
		{
			return false;
		}
		auto document = patch->toJson(true);
		jsonFile.write(document.toJson());
		if(!jsonFile.commit())
		{
			return false;
		}
		load();
	}
	catch (VersionIncomplete &error)
	{
		qDebug() << "Version was incomplete:" << error.cause();
	}
	catch (Exception &error)
	{
		qWarning() << "Version could not be loaded:" << error.cause();
	}
	return true;
}

bool OneSixProfileStrategy::revertPatch(ProfilePatchPtr patch)
{
	if(!patch->isCustom())
	{
		// already not custom
		return true;
	}
	auto filename = patch->getPatchFilename();
	if(!QFile::exists(filename))
	{
		// already gone / not custom
		return true;
	}
	// just kill the file and reload
	bool result = QFile::remove(filename);
	try
	{
		load();
	}
	catch (VersionIncomplete &error)
	{
		qDebug() << "Version was incomplete:" << error.cause();
	}
	catch (Exception &error)
	{
		qWarning() << "Version could not be loaded:" << error.cause();
	}
	return result;
}

bool OneSixProfileStrategy::installJarMods(QStringList filepaths)
{
	QString patchDir = FS::PathCombine(m_instance->instanceRoot(), "patches");
	if(!FS::ensureFolderPathExists(patchDir))
	{
		return false;
	}

	if (!FS::ensureFolderPathExists(m_instance->jarModsDir()))
	{
		return false;
	}

	for(auto filepath:filepaths)
	{
		QFileInfo sourceInfo(filepath);
		auto uuid = QUuid::createUuid();
		QString id = uuid.toString().remove('{').remove('}');
		QString target_filename = id + ".jar";
		QString target_id = "org.multimc.jarmod." + id;
		QString target_name = sourceInfo.completeBaseName() + " (jar mod)";
		QString finalPath = FS::PathCombine(m_instance->jarModsDir(), target_filename);

		QFileInfo targetInfo(finalPath);
		if(targetInfo.exists())
		{
			return false;
		}

		if (!QFile::copy(sourceInfo.absoluteFilePath(),QFileInfo(finalPath).absoluteFilePath()))
		{
			return false;
		}

		auto f = std::make_shared<VersionFile>();
		auto jarMod = std::make_shared<Jarmod>();
		jarMod->name = target_filename;
		jarMod->originalName = sourceInfo.completeBaseName();
		f->jarMods.append(jarMod);
		f->name = target_name;
		f->fileId = target_id;
		f->order = profile->getFreeOrderNumber();
		QString patchFileName = FS::PathCombine(patchDir, target_id + ".json");
		f->filename = patchFileName;
		f->setMovable(true);
		f->setRemovable(true);

		QFile file(patchFileName);
		if (!file.open(QFile::WriteOnly))
		{
			qCritical() << "Error opening" << file.fileName()
						<< "for reading:" << file.errorString();
			return false;
		}
		file.write(f->toJson(true).toJson());
		file.close();
		profile->appendPatch(f);
	}
	profile->saveCurrentOrder();
	profile->reapplySafe();
	return true;
}

