/* Copyright 2013-2017 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "BaseInstaller.h"

#include <QString>
#include <memory>

#include "multimc_logic_export.h"

class VersionFile;
class ForgeInstallTask;
struct ForgeVersion;

class MULTIMC_LOGIC_EXPORT ForgeInstaller : public BaseInstaller
{
	friend class ForgeInstallTask;
public:
	ForgeInstaller();
	virtual ~ForgeInstaller(){}
	virtual Task *createInstallTask(OneSixInstance *instance, BaseVersionPtr version, QObject *parent) override;
	virtual QString id() const override { return "net.minecraftforge"; }

protected:
	void prepare(const QString &filename, const QString &universalUrl);
	bool add(OneSixInstance *to) override;
	bool addLegacy(OneSixInstance *to);

private:
	// the parsed version json, read from the installer
	std::shared_ptr<VersionFile> m_forge_json;
	// the actual forge version
	std::shared_ptr<ForgeVersion> m_forge_version;
	QString internalPath;
	QString finalPath;
	QString m_forgeVersionString;
	QString m_universal_url;
};
