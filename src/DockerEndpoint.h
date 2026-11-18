#pragma once

#include <QProcessEnvironment>
#include <QString>

#include <functional>

// Where the Docker Engine API listens. Resolution order:
//   1. the `dockerHost` setting
//   2. $DOCKER_HOST
//   3. the active context ($DOCKER_CONTEXT, else currentContext in
//      $DOCKER_CONFIG/config.json) from contexts/meta/*/meta.json
//   4. the first socket that exists: rootless Docker, Podman, Colima, OrbStack
//   5. /var/run/docker.sock
// unix:// hosts (or a bare absolute path) and, for Docker Desktop on Windows,
// npipe:// are supported (the default there is npipe:////./pipe/docker_engine);
// tcp:// and ssh:// are out of scope.
struct DockerEndpoint
{
    QString socketPath; // empty when the chosen host is unsupported
    QString source; // what picked it, for Settings: "setting", "DOCKER_HOST", "context …", "Podman", …
    QString error; // set when the chosen host cannot be used

    QString display() const;
};

struct DockerEnvironment
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString home;
    std::function<bool(const QString &)> exists;

    static DockerEnvironment system();
};

DockerEndpoint resolveDockerEndpoint(const QString &setting, const DockerEnvironment &where = DockerEnvironment::system());
