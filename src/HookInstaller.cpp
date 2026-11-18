// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HookInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QSaveFile>
#include <QStandardPaths>

#include <memory>
#include <utility>

namespace
{
const QString kHookName = QStringLiteral("x3ro-deck-hook");
constexpr int kTimeout = 5;

// JSON that keeps object keys in file order and scalars as written.
// QJsonObject sorts its keys, which would reorder the user's whole file.
struct Node
{
    enum Kind { Scalar, Object, Array };
    Kind kind = Scalar;
    QByteArray raw; // a scalar's text, quotes included for strings
    QList<QPair<QByteArray, Node>> members; // raw key (quoted) → value
    QList<Node> items;

    static Node object() { return Node{Object, {}, {}, {}}; }
    static Node array() { return Node{Array, {}, {}, {}}; }
    static Node string(const QString &s)
    {
        QByteArray json = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);
        return Node{Scalar, json.mid(1, json.size() - 2), {}, {}};
    }
    static Node number(int n) { return Node{Scalar, QByteArray::number(n), {}, {}}; }

    // A string scalar's value; empty for anything else.
    QString text() const
    {
        if (kind != Scalar || !raw.startsWith('"')) {
            return {};
        }
        return QJsonDocument::fromJson("[" + raw + "]").array().at(0).toString();
    }
    Node *member(const QString &key) { return const_cast<Node *>(std::as_const(*this).member(key)); }
    const Node *member(const QString &key) const
    {
        for (const auto &m : members) {
            if (decodeKey(m.first) == key) {
                return &m.second;
            }
        }
        return nullptr;
    }
    void append(const QString &key, Node value) { members.append({Node::string(key).raw, std::move(value)}); }
    void remove(const QString &key)
    {
        members.removeIf([&](const auto &m) { return decodeKey(m.first) == key; });
    }
    static QString decodeKey(const QByteArray &raw) { return QJsonDocument::fromJson("[" + raw + "]").array().at(0).toString(); }
};

class Parser
{
public:
    explicit Parser(const QByteArray &in)
        : m_in(in)
    {
    }

    std::unique_ptr<Node> parse(QString *error)
    {
        auto node = std::make_unique<Node>();
        skip();
        if (!value(*node)) {
            fail(error);
            return nullptr;
        }
        skip();
        if (m_pos != m_in.size()) {
            m_error = QStringLiteral("unexpected text after the end");
            fail(error);
            return nullptr;
        }
        return node;
    }

private:
    void fail(QString *error) const
    {
        if (error) {
            *error = QStringLiteral("Invalid JSON at byte %1: %2").arg(m_pos).arg(m_error);
        }
    }
    char peek() const { return m_pos < m_in.size() ? m_in.at(m_pos) : '\0'; }
    void skip()
    {
        while (m_pos < m_in.size()) {
            const char c = m_in.at(m_pos);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }
    bool expect(char c)
    {
        skip();
        if (peek() != c) {
            m_error = QStringLiteral("expected '%1'").arg(QLatin1Char(c));
            return false;
        }
        ++m_pos;
        return true;
    }
    bool string(QByteArray &out)
    {
        const qsizetype start = m_pos;
        if (peek() != '"') {
            m_error = QStringLiteral("expected a string");
            return false;
        }
        ++m_pos;
        while (m_pos < m_in.size()) {
            const char c = m_in.at(m_pos++);
            if (c == '\\') {
                ++m_pos;
            } else if (c == '"') {
                out = m_in.mid(start, m_pos - start);
                return true;
            }
        }
        m_error = QStringLiteral("unterminated string");
        return false;
    }
    bool value(Node &node)
    {
        skip();
        const char c = peek();
        if (c == '{') {
            node.kind = Node::Object;
            ++m_pos;
            skip();
            if (peek() == '}') {
                ++m_pos;
                return true;
            }
            while (true) {
                skip();
                QByteArray key;
                if (!string(key) || !expect(':')) {
                    return false;
                }
                Node child;
                if (!value(child)) {
                    return false;
                }
                node.members.append({key, std::move(child)});
                skip();
                if (peek() == ',') {
                    ++m_pos;
                    continue;
                }
                return expect('}');
            }
        }
        if (c == '[') {
            node.kind = Node::Array;
            ++m_pos;
            skip();
            if (peek() == ']') {
                ++m_pos;
                return true;
            }
            while (true) {
                Node child;
                if (!value(child)) {
                    return false;
                }
                node.items.append(std::move(child));
                skip();
                if (peek() == ',') {
                    ++m_pos;
                    continue;
                }
                return expect(']');
            }
        }
        node.kind = Node::Scalar;
        if (c == '"') {
            if (!string(node.raw)) {
                return false;
            }
        } else {
            const qsizetype start = m_pos;
            while (m_pos < m_in.size() && !QByteArrayView(",]} \t\r\n").contains(m_in.at(m_pos))) {
                ++m_pos;
            }
            node.raw = m_in.mid(start, m_pos - start);
        }
        // Validates the scalar's own syntax (numbers, literals, escapes).
        QJsonParseError err;
        QJsonDocument::fromJson("[" + node.raw + "]", &err);
        if (node.raw.isEmpty() || err.error != QJsonParseError::NoError) {
            m_error = QStringLiteral("invalid value");
            return false;
        }
        return true;
    }

    const QByteArray &m_in;
    qsizetype m_pos = 0;
    QString m_error;
};

// The layout of JSON.stringify(value, null, 2), which both CLIs write.
void write(const Node &node, QByteArray &out, int depth)
{
    const QByteArray pad((depth + 1) * 2, ' ');
    const QByteArray close(depth * 2, ' ');
    if (node.kind == Node::Scalar) {
        out += node.raw;
    } else if (node.kind == Node::Object) {
        if (node.members.isEmpty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        for (qsizetype i = 0; i < node.members.size(); ++i) {
            out += pad + node.members.at(i).first + ": ";
            write(node.members.at(i).second, out, depth + 1);
            out += i + 1 < node.members.size() ? ",\n" : "\n";
        }
        out += close + "}";
    } else {
        if (node.items.isEmpty()) {
            out += "[]";
            return;
        }
        out += "[\n";
        for (qsizetype i = 0; i < node.items.size(); ++i) {
            out += pad;
            write(node.items.at(i), out, depth + 1);
            out += i + 1 < node.items.size() ? ",\n" : "\n";
        }
        out += close + "]";
    }
}

QByteArray serialize(const Node &node, bool trailingNewline)
{
    QByteArray out;
    write(node, out, 0);
    if (trailingNewline) {
        out += '\n';
    }
    return out;
}

bool endsWithNewline(const QByteArray &json)
{
    return json.isEmpty() || json.endsWith('\n');
}

std::unique_ptr<Node> parseFile(const QByteArray &json, QString *error)
{
    if (json.trimmed().isEmpty()) {
        return std::make_unique<Node>(Node::object());
    }
    std::unique_ptr<Node> root = Parser(json).parse(error);
    if (root && root->kind != Node::Object) {
        if (error) {
            *error = QStringLiteral("The file does not hold a JSON object.");
        }
        return nullptr;
    }
    return root;
}

QString firstWord(const QString &command)
{
    const QString s = command.trimmed();
    if (s.startsWith(QLatin1Char('\'')) || s.startsWith(QLatin1Char('"'))) {
        const qsizetype end = s.indexOf(s.at(0), 1);
        return end < 0 ? s.mid(1) : s.mid(1, end - 1);
    }
    const qsizetype space = s.indexOf(QLatin1Char(' '));
    return space < 0 ? s : s.left(space);
}

// `installed` runs the same binary as `wanted`: the same text, or a bare
// name or other path that resolves to the same file.
bool sameHook(const QString &installed, const QString &wanted)
{
    if (installed == wanted) {
        return true;
    }
    QString exe = firstWord(installed);
    if (!exe.contains(QLatin1Char('/'))) {
        exe = QStandardPaths::findExecutable(exe);
    }
    const QString a = QFileInfo(exe).canonicalFilePath();
    const QString b = QFileInfo(firstWord(wanted)).canonicalFilePath();
    return !a.isEmpty() && a == b && installed.trimmed().indexOf(QLatin1Char(' ')) < 0;
}

// Removes our hooks from every event and drops what that leaves empty,
// except the events in `keep`, which install refills in place.
void removeOurs(Node &root, const QStringList &keep = {})
{
    Node *hooks = root.member(QStringLiteral("hooks"));
    if (!hooks || hooks->kind != Node::Object) {
        return;
    }
    for (auto &event : hooks->members) {
        Node &groups = event.second;
        if (groups.kind != Node::Array) {
            continue;
        }
        bool changed = false;
        for (Node &group : groups.items) {
            Node *list = group.member(QStringLiteral("hooks"));
            if (!list || list->kind != Node::Array) {
                continue;
            }
            const qsizetype before = list->items.size();
            list->items.removeIf([](const Node &h) {
                const Node *cmd = h.member(QStringLiteral("command"));
                return cmd && HookInstaller::isOurs(cmd->text());
            });
            changed = changed || list->items.size() != before;
        }
        if (changed) {
            groups.items.removeIf([](const Node &g) {
                const Node *list = g.member(QStringLiteral("hooks"));
                return list && list->kind == Node::Array && list->items.isEmpty();
            });
        }
    }
    hooks->members.removeIf([&](const auto &m) {
        return m.second.kind == Node::Array && m.second.items.isEmpty() && !keep.contains(Node::decodeKey(m.first));
    });
    if (hooks->members.isEmpty()) {
        root.remove(QStringLiteral("hooks"));
    }
}

QString notificationMatcher()
{
    return QStringLiteral("permission_prompt|idle_prompt|elicitation_dialog");
}

QByteArray readFile(const QString &file, bool *ok, QString *error)
{
    *ok = true;
    QFile f(file);
    if (!f.exists()) {
        return {};
    }
    if (!f.open(QIODevice::ReadOnly)) {
        *ok = false;
        if (error) {
            *error = QStringLiteral("Cannot read %1: %2").arg(file, f.errorString());
        }
        return {};
    }
    return f.readAll();
}

bool writeFile(const QString &file, const QByteArray &before, const QByteArray &after, QString *error)
{
    if (before == after) {
        return true;
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (QFile::exists(file)) {
        const QString bak = file + QStringLiteral(".bak");
        QFile::remove(bak);
        if (!QFile::copy(file, bak)) {
            if (error) {
                *error = QStringLiteral("Cannot write the backup %1.").arg(bak);
            }
            return false;
        }
    }
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(after) != after.size() || !out.commit()) {
        if (error) {
            *error = QStringLiteral("Cannot write %1: %2").arg(file, out.errorString());
        }
        return false;
    }
    return true;
}
} // namespace

QString HookInstaller::settingsFile(Cli cli)
{
    const QString home = QDir::homePath();
    return cli == Cli::Claude ? home + QStringLiteral("/.claude/settings.json") : home + QStringLiteral("/.codex/hooks.json");
}

QString HookInstaller::hookPath()
{
    const QString local = QCoreApplication::applicationDirPath() + QLatin1Char('/') + kHookName;
    if (QFileInfo(local).isExecutable()) {
        return local;
    }
    return QStandardPaths::findExecutable(kHookName);
}

QString HookInstaller::command(const QString &hookPath)
{
    for (const QChar c : hookPath) {
        if (!c.isLetterOrNumber() && !QStringLiteral("/._+-@%=:,").contains(c)) {
            QString quoted = hookPath;
            quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            return QLatin1Char('\'') + quoted + QLatin1Char('\'');
        }
    }
    return hookPath;
}

QStringList HookInstaller::events(Cli cli)
{
    if (cli == Cli::Claude) {
        return {QStringLiteral("SessionStart"), QStringLiteral("UserPromptSubmit"),
                QStringLiteral("PreToolUse"),   QStringLiteral("PermissionRequest"),
                QStringLiteral("PostToolUse"),  QStringLiteral("PostToolUseFailure"),
                QStringLiteral("Notification"), QStringLiteral("Stop"),
                QStringLiteral("SessionEnd")};
    }
    return {QStringLiteral("SessionStart"),      QStringLiteral("UserPromptSubmit"), QStringLiteral("PreToolUse"),
            QStringLiteral("PermissionRequest"), QStringLiteral("PostToolUse"),      QStringLiteral("Stop"),
            QStringLiteral("Interrupt"),         QStringLiteral("SessionEnd")};
}

bool HookInstaller::isOurs(const QString &command)
{
    return QFileInfo(firstWord(command)).fileName() == kHookName;
}

HookInstaller::Status HookInstaller::statusOf(Cli cli, const QByteArray &json, const QString &command)
{
    Status st;
    QString error;
    std::unique_ptr<Node> root = parseFile(json, &error);
    if (!root) {
        st.state = State::Error;
        st.error = error;
        return st;
    }
    Node *hooks = root->member(QStringLiteral("hooks"));
    if (!hooks || hooks->kind != Node::Object) {
        return st;
    }
    bool any = false;
    bool complete = true;
    for (const QString &event : events(cli)) {
        int found = 0;
        if (Node *groups = hooks->member(event); groups && groups->kind == Node::Array) {
            for (Node &group : groups->items) {
                Node *list = group.member(QStringLiteral("hooks"));
                if (!list || list->kind != Node::Array) {
                    continue;
                }
                for (Node &h : list->items) {
                    const Node *cmd = h.member(QStringLiteral("command"));
                    if (cmd && isOurs(cmd->text())) {
                        any = true;
                        st.command = cmd->text();
                        found += sameHook(cmd->text(), command) ? 1 : 2;
                    }
                }
            }
        }
        complete = complete && found == 1;
    }
    st.state = !any ? State::NotInstalled : complete ? State::Installed : State::Outdated;
    return st;
}

QByteArray HookInstaller::installed(Cli cli, const QByteArray &json, const QString &command, QString *error)
{
    std::unique_ptr<Node> root = parseFile(json, error);
    if (!root) {
        return {};
    }
    removeOurs(*root, events(cli));
    Node *hooks = root->member(QStringLiteral("hooks"));
    if (!hooks) {
        root->append(QStringLiteral("hooks"), Node::object());
        hooks = root->member(QStringLiteral("hooks"));
    } else if (hooks->kind != Node::Object) {
        if (error) {
            *error = QStringLiteral("\"hooks\" is not a JSON object.");
        }
        return {};
    }
    for (const QString &event : events(cli)) {
        Node *groups = hooks->member(event);
        if (!groups) {
            hooks->append(event, Node::array());
            groups = hooks->member(event);
        } else if (groups->kind != Node::Array) {
            if (error) {
                *error = QStringLiteral("\"hooks.%1\" is not a JSON array.").arg(event);
            }
            return {};
        }
        Node hook = Node::object();
        hook.append(QStringLiteral("type"), Node::string(QStringLiteral("command")));
        hook.append(QStringLiteral("command"), Node::string(command));
        hook.append(QStringLiteral("timeout"), Node::number(kTimeout));
        Node list = Node::array();
        list.items.append(hook);
        Node group = Node::object();
        if (event == QLatin1String("Notification")) {
            group.append(QStringLiteral("matcher"), Node::string(notificationMatcher()));
        }
        group.append(QStringLiteral("hooks"), list);
        groups->items.append(group);
    }
    return serialize(*root, endsWithNewline(json));
}

QByteArray HookInstaller::removed(const QByteArray &json, QString *error)
{
    std::unique_ptr<Node> root = parseFile(json, error);
    if (!root) {
        return {};
    }
    removeOurs(*root);
    return serialize(*root, endsWithNewline(json));
}

QByteArray HookInstaller::normalized(const QByteArray &json, QString *error)
{
    std::unique_ptr<Node> root = parseFile(json, error);
    return root ? serialize(*root, endsWithNewline(json)) : QByteArray();
}

HookInstaller::Status HookInstaller::status(Cli cli, const QString &file, const QString &command)
{
    bool ok = false;
    Status st;
    const QByteArray json = readFile(file, &ok, &st.error);
    if (!ok) {
        st.state = State::Error;
        return st;
    }
    return statusOf(cli, json, command);
}

bool HookInstaller::install(Cli cli, const QString &file, const QString &command, QString *error)
{
    bool ok = false;
    const QByteArray before = readFile(file, &ok, error);
    if (!ok) {
        return false;
    }
    const QByteArray after = installed(cli, before, command, error);
    return !after.isNull() && writeFile(file, before, after, error);
}

bool HookInstaller::uninstall(const QString &file, QString *error)
{
    bool ok = false;
    const QByteArray before = readFile(file, &ok, error);
    if (!ok) {
        return false;
    }
    if (before.isEmpty()) {
        return true;
    }
    const QByteArray after = removed(before, error);
    return !after.isNull() && writeFile(file, before, after, error);
}
