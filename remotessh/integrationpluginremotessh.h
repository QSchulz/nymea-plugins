/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2020, nymea GmbH
* Contact: contact@nymea.io
*
* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU Lesser General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 3. This project is distributed in the hope that
* it will be useful, but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef INTEGRATIONPLUGINREMOTESSH_H
#define INTEGRATIONPLUGINREMOTESSH_H

#include "plugintimer.h"
#include "integrations/integrationplugin.h"

#include <QProcess>

class IntegrationPluginRemoteSsh : public IntegrationPlugin
{
    Q_OBJECT

    Q_PLUGIN_METADATA(IID "io.nymea.IntegrationPlugin" FILE "integrationpluginremotessh.json")
    Q_INTERFACES(IntegrationPlugin)

public:
    explicit IntegrationPluginRemoteSsh();

    void init() override;
    void setupThing(ThingSetupInfo *info) override;
    void thingRemoved(Thing *thing) override;
    void executeAction(ThingActionInfo *info) override;

private:
    QHash<QProcess *, Thing *> m_reverseSSHProcess;
    QHash<QProcess *, Thing *> m_sshKeyGenProcess;

    QHash<QProcess *, ThingActionInfo*> m_startingProcess;
    QHash<QProcess *, ThingActionInfo*> m_killingProcess;

    PluginTimer *m_pluginTimer = nullptr;

    bool m_aboutToQuit = false;
    QString m_identityFilePath;

    QProcess *startReverseSSHProcess(Thing *thing);

private slots:
    void onPluginTimeout();
    void processReadyRead();
    void processStateChanged(QProcess::ProcessState state);
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
};

#endif // INTEGRATIONPLUGINREMOTESSH_H
