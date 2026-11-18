#include "SettingsDialog.h"

#include "Config.h"
#include "Dependencies.h"
#include "DockerEndpoint.h"
#include "HookInstaller.h"
#include "Paths.h"
#include "TerminalSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace
{
const QString kNewAgent = QStringLiteral(":new-agent");
const QStringList kPages{QStringLiteral("checkouts"), QStringLiteral("agents"), QStringLiteral("docker"), QStringLiteral("general")};
const QList<QPair<QString, QString>> kKinds{{QStringLiteral("claude"), QStringLiteral("Claude Code")},
                                            {QStringLiteral("codex"), QStringLiteral("Codex")},
                                            {QStringLiteral("custom"), QStringLiteral("Custom command")}};
const QList<QPair<QString, QString>> kGroups{{QStringLiteral("check"), QStringLiteral("Check")},
                                             {QStringLiteral("env"), QStringLiteral("Env")},
                                             {QStringLiteral("data"), QStringLiteral("Data")}};

QString kindHint(const QString &kind)
{
    if (kind == QLatin1String("codex")) {
        return QStringLiteral("Codex reports its state through x3ro-deck-hook once you trust the hook with /hooks.");
    }
    if (kind == QLatin1String("custom")) {
        return QStringLiteral("Any other CLI. It sends no hook events, so X3RO Deck only sees whether it runs.");
    }
    return QStringLiteral("Claude Code reports its state through x3ro-deck-hook in ~/.claude/settings.json.");
}

QLabel *hint(const QString &text, const char *name = "hint")
{
    auto *label = new QLabel(text);
    label->setObjectName(QLatin1String(name));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return label;
}

QLabel *section(const QString &text)
{
    auto *label = new QLabel(text.toUpper());
    label->setObjectName(QStringLiteral("section"));
    return label;
}

QLineEdit *lineEdit(bool mono = false)
{
    auto *edit = new QLineEdit;
    edit->setProperty("mono", mono);
    return edit;
}

// A line edit rather than a QSpinBox, whose editor ignores the stylesheet's padding.
QLineEdit *numberEdit(int max)
{
    auto *edit = lineEdit(true);
    edit->setValidator(new QIntValidator(0, max, edit));
    edit->setFixedWidth(140);
    return edit;
}

// Label and field columns. A grid rather than QFormLayout, which sizes a
// wrapped note by its size hint and leaves a blank line under it.
QGridLayout *form()
{
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    return grid;
}

int nextRow(const QGridLayout *grid)
{
    return grid->count() ? grid->rowCount() : 0;
}

// `top` puts the label beside the first line of a multi-line field.
void addField(QGridLayout *grid, const QString &label, QWidget *field, QLabel *note = nullptr, bool top = false)
{
    const int row = nextRow(grid);
    if (!label.isEmpty()) {
        auto *text = new QLabel(label);
        text->setContentsMargins(0, top ? 8 : 0, 0, 0);
        grid->addWidget(text, row, 0, Qt::AlignRight | (top ? Qt::AlignTop : Qt::AlignVCenter));
    }
    grid->addWidget(field, row, 1);
    if (note) {
        note->setContentsMargins(0, 0, 0, 8);
        grid->addWidget(note, row + 1, 1);
    }
}

void setRowVisible(const QGridLayout *grid, QWidget *field, bool visible)
{
    int row = 0;
    int column = 0;
    int rowSpan = 0;
    int columnSpan = 0;
    grid->getItemPosition(grid->indexOf(field), &row, &column, &rowSpan, &columnSpan);
    for (int c = 0; c < grid->columnCount(); ++c) {
        if (QLayoutItem *item = grid->itemAtPosition(row, c); item && item->widget()) {
            item->widget()->setVisible(visible);
        }
    }
}

QTableWidget *table(const QStringList &headers)
{
    auto *t = new QTableWidget(0, static_cast<int>(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(30);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed
                       | QAbstractItemView::SelectedClicked);
    t->setShowGrid(false);
    return t;
}

QString cellText(const QTableWidget *t, int row, int column)
{
    const QTableWidgetItem *item = t->item(row, column);
    return item ? item->text().trimmed() : QString();
}

// Quick command and link rows keep their original JSON in the first cell, so
// keys without a column survive.
QJsonObject quickRow(const QTableWidget *t, int row)
{
    QJsonObject o = t->item(row, 0)->data(Qt::UserRole).toJsonObject();
    o.insert(QStringLiteral("label"), cellText(t, row, 0));
    o.insert(QStringLiteral("command"), cellText(t, row, 1));
    o.insert(QStringLiteral("group"), static_cast<QComboBox *>(t->cellWidget(row, 2))->currentData().toString());
    return o;
}

void setQuickRow(QTableWidget *t, int row, const QJsonObject &o)
{
    auto *label = new QTableWidgetItem(o.value(QStringLiteral("label")).toString());
    label->setData(Qt::UserRole, o);
    t->setItem(row, 0, label);
    t->setItem(row, 1, new QTableWidgetItem(o.value(QStringLiteral("command")).toString()));
    auto *group = new QComboBox;
    for (const auto &[key, text] : kGroups) {
        group->addItem(text, key);
    }
    group->setCurrentIndex(std::max(0, group->findData(o.value(QStringLiteral("group")).toString())));
    t->setCellWidget(row, 2, group);
}

QJsonObject linkRow(const QTableWidget *t, int row)
{
    QJsonObject o = t->item(row, 0)->data(Qt::UserRole).toJsonObject();
    o.insert(QStringLiteral("label"), cellText(t, row, 0));
    o.insert(QStringLiteral("service"), cellText(t, row, 1));
    o.insert(QStringLiteral("containerPort"), cellText(t, row, 2).toInt());
    return o;
}

void setLinkRow(QTableWidget *t, int row, const QJsonObject &o)
{
    auto *label = new QTableWidgetItem(o.value(QStringLiteral("label")).toString());
    label->setData(Qt::UserRole, o);
    t->setItem(row, 0, label);
    t->setItem(row, 1, new QTableWidgetItem(o.value(QStringLiteral("service")).toString()));
    const int port = o.value(QStringLiteral("containerPort")).toInt();
    t->setItem(row, 2, new QTableWidgetItem(port > 0 ? QString::number(port) : QString()));
}

// "Claude Opus" → "claude-opus", unique among `taken`.
QString slug(const QString &label, const QSet<QString> &taken)
{
    QString base = label.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    base = base.split(QLatin1Char('-'), Qt::SkipEmptyParts).join(QLatin1Char('-'));
    if (base.isEmpty()) {
        base = QStringLiteral("agent");
    }
    QString id = base;
    for (int n = 2; taken.contains(id); ++n) {
        id = QStringLiteral("%1-%2").arg(base).arg(n);
    }
    return id;
}
} // namespace

SettingsDialog::SettingsDialog(const QJsonObject &json, QWidget *parent)
    : DeckDialog(parent)
    , m_root(json)
{
    setWindowTitle(QStringLiteral("Settings — X3RO Deck"));
    resize(1000, 820);

    for (const QJsonValue &v : json.value(QStringLiteral("checkouts")).toArray()) {
        m_checkouts.append(v.toObject());
    }
    for (const QJsonValue &v : json.value(QStringLiteral("agents")).toArray()) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject user = v.toObject();
        QString kind = user.value(QStringLiteral("kind")).toString();
        if (kind != QLatin1String("codex") && kind != QLatin1String("custom")) {
            kind = QStringLiteral("claude");
        }
        Agent a{user.value(QStringLiteral("id")).toString(kind), Config::kindDefaults(kind)};
        for (auto it = user.begin(); it != user.end(); ++it) {
            a.json.insert(it.key(), it.value());
        }
        a.json.insert(QStringLiteral("kind"), kind);
        a.json.remove(QStringLiteral("id"));
        m_agents.append(a);
    }
    if (m_agents.isEmpty()) {
        Agent a{QStringLiteral("claude"), Config::kindDefaults(QStringLiteral("claude"))};
        a.json.insert(QStringLiteral("kind"), QStringLiteral("claude"));
        a.json.insert(QStringLiteral("label"), QStringLiteral("Claude"));
        m_agents.append(a);
    }
    m_defaultAgent = json.value(QStringLiteral("defaultAgent")).toString(m_agents.first().id);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildCheckoutsPage(), QStringLiteral("Checkouts"));
    m_tabs->addTab(buildAgentsPage(), QStringLiteral("Agents"));
    m_tabs->addTab(buildDockerPage(), QStringLiteral("Docker"));
    m_tabs->addTab(buildGeneralPage(), QStringLiteral("General"));

    auto *editJson = new QPushButton(QStringLiteral("Edit config.json…"));
    editJson->setAutoDefault(false);
    editJson->setToolTip(Config::configPath());
    connect(editJson, &QPushButton::clicked, this, [this] {
        if (dirty()
            && QMessageBox::question(this, QStringLiteral("Settings"),
                                     QStringLiteral("Discard your changes and open config.json in a text editor?"),
                                     QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel)
                != QMessageBox::Discard) {
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(Config::configPath()));
        QDialog::reject();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(20, 12, 20, 16);
    footer->addWidget(editJson);
    footer->addStretch();
    footer->addWidget(buttons);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_tabs, 1);
    root->addLayout(footer);

    refreshAgentChoices();
    m_checkoutList->setCurrentRow(m_checkouts.isEmpty() ? -1 : 0);
    loadCheckout(m_checkoutList->currentRow());
    m_agentList->setCurrentRow(0);
    loadAgent(0);
    m_initial = config();
}

SettingsDialog::ListButtons SettingsDialog::listButtons(const QString &addText)
{
    ListButtons b{new QHBoxLayout, new QPushButton(addText), new QPushButton(QStringLiteral("Remove")),
                  new QPushButton(QStringLiteral("↑")), new QPushButton(QStringLiteral("↓"))};
    b.up->setToolTip(QStringLiteral("Move up"));
    b.down->setToolTip(QStringLiteral("Move down"));
    for (QPushButton *button : {b.add, b.remove, b.up, b.down}) {
        button->setObjectName(QStringLiteral("small"));
        button->setAutoDefault(false);
    }
    b.layout->setSpacing(6);
    b.layout->addWidget(b.add);
    b.layout->addWidget(b.remove);
    b.layout->addStretch();
    b.layout->addWidget(b.up);
    b.layout->addWidget(b.down);
    return b;
}

// ───────────────────────── checkouts ─────────────────────────

QWidget *SettingsDialog::buildCheckoutsPage()
{
    m_checkoutList = new QListWidget;
    for (const QJsonObject &c : std::as_const(m_checkouts)) {
        m_checkoutList->addItem(c.value(QStringLiteral("name")).toString());
    }
    m_checkoutButtons = listButtons(QStringLiteral("Add…"));
    m_checkoutButtons.add->setToolTip(QStringLiteral("Pick a folder to add as a checkout"));
    auto *left = new QWidget;
    left->setFixedWidth(260);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(m_checkoutList, 1);
    leftLayout->addLayout(m_checkoutButtons.layout);

    m_checkoutName = lineEdit();
    m_checkoutPath = lineEdit(true);
    auto *browse = new QPushButton(QStringLiteral("Browse…"));
    browse->setAutoDefault(false);
    auto *pathRow = new QWidget;
    auto *pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_checkoutPath, 1);
    pathLayout->addWidget(browse);
    m_checkoutPathWarning = hint(QString(), "warning");
    m_checkoutOffset = numberEdit(65535);
    m_checkoutAgent = new QComboBox;
    m_checkoutArgs = lineEdit();
    m_checkoutArgs->setPlaceholderText(QStringLiteral("--name my-checkout"));

    m_checkoutFields = form();
    addField(m_checkoutFields, QStringLiteral("Name"), m_checkoutName, hint(QStringLiteral("Also names the checkout's agent session.")));
    addField(m_checkoutFields, QStringLiteral("Folder"), pathRow, hint(QStringLiteral("Where the agent and the compose commands run.")));
    m_checkoutFields->addWidget(m_checkoutPathWarning, nextRow(m_checkoutFields), 1);
    m_checkoutPathWarning->setContentsMargins(0, 0, 0, 8);
    addField(m_checkoutFields, QStringLiteral("Port offset"), m_checkoutOffset, hint(QStringLiteral("Shown on the checkout's card.")));
    addField(m_checkoutFields, QStringLiteral("Agent"), m_checkoutAgent,
             hint(QStringLiteral("Starts whenever X3RO Deck creates the checkout's agent session: on the first launch, and on "
                                 "every launch after you stopped the agents when quitting. Switching agents in the session "
                                 "panel lasts until then.")));
    addField(m_checkoutFields, QStringLiteral("Custom args"), m_checkoutArgs,
             hint(QStringLiteral("Added to the agent's command line. A flag set here replaces the same flag in the agent's command. "
                                 "Quote values with spaces.")));
    m_checkoutForm = new QWidget;
    auto *formLayout = new QVBoxLayout(m_checkoutForm);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->addLayout(m_checkoutFields);
    formLayout->addStretch();

    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 8);
    layout->setSpacing(24);
    layout->addWidget(left);
    layout->addWidget(m_checkoutForm, 1);

    connect(m_checkoutList, &QListWidget::currentRowChanged, this, &SettingsDialog::loadCheckout);
    connect(m_checkoutButtons.add, &QPushButton::clicked, this, &SettingsDialog::addCheckout);
    connect(m_checkoutButtons.remove, &QPushButton::clicked, this, [this] {
        const int row = m_checkoutList->currentRow();
        if (row >= 0) {
            m_checkouts.removeAt(row);
            delete m_checkoutList->takeItem(row);
            loadCheckout(m_checkoutList->currentRow());
        }
    });
    connect(m_checkoutButtons.up, &QPushButton::clicked, this, [this] { moveCheckout(-1); });
    connect(m_checkoutButtons.down, &QPushButton::clicked, this, [this] { moveCheckout(1); });
    connect(m_checkoutName, &QLineEdit::textEdited, this, [this](const QString &text) {
        editCheckout(QStringLiteral("name"), text.trimmed());
        if (QListWidgetItem *item = m_checkoutList->currentItem()) {
            item->setText(text.trimmed());
        }
    });
    connect(m_checkoutPath, &QLineEdit::textEdited, this, [this](const QString &text) {
        editCheckout(QStringLiteral("path"), text.trimmed());
        checkPath();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(parentWidget(), QStringLiteral("Checkout folder"),
                                                              Paths::expandTilde(m_checkoutPath->text().trimmed()));
        if (!dir.isEmpty()) {
            m_checkoutPath->setText(Paths::tilde(dir));
            editCheckout(QStringLiteral("path"), m_checkoutPath->text());
            checkPath();
        }
    });
    connect(m_checkoutOffset, &QLineEdit::textEdited, this,
            [this](const QString &text) { editCheckout(QStringLiteral("portOffset"), text.toInt()); });
    connect(m_checkoutArgs, &QLineEdit::textEdited, this,
            [this](const QString &text) { editCheckout(QStringLiteral("args"), text.trimmed()); });
    connect(m_checkoutAgent, &QComboBox::activated, this, [this](int index) {
        const QString id = m_checkoutAgent->itemData(index).toString();
        if (id != kNewAgent) {
            editCheckout(QStringLiteral("agent"), id);
            return;
        }
        const int agent = addAgent();
        editCheckout(QStringLiteral("agent"), m_agents.at(agent).id);
        refreshAgentChoices();
        m_tabs->setCurrentIndex(static_cast<int>(kPages.indexOf(QStringLiteral("agents"))));
        m_agentLabel->setFocus();
        m_agentLabel->selectAll();
    });
    return page;
}

void SettingsDialog::loadCheckout(int row)
{
    const bool valid = row >= 0 && row < m_checkouts.size();
    m_checkoutForm->setEnabled(valid);
    m_checkoutButtons.remove->setEnabled(valid);
    m_checkoutButtons.up->setEnabled(valid && row > 0);
    m_checkoutButtons.down->setEnabled(valid && row + 1 < m_checkouts.size());
    const QJsonObject c = valid ? m_checkouts.at(row) : QJsonObject();
    const bool loading = std::exchange(m_loading, true);
    m_checkoutName->setText(c.value(QStringLiteral("name")).toString());
    m_checkoutPath->setText(c.value(QStringLiteral("path")).toString());
    m_checkoutOffset->setText(QString::number(c.value(QStringLiteral("portOffset")).toInt()));
    m_checkoutAgent->setCurrentIndex(std::max(0, m_checkoutAgent->findData(c.value(QStringLiteral("agent")).toString())));
    m_checkoutArgs->setText(c.value(QStringLiteral("args")).toString());
    m_loading = loading;
    checkPath();
}

void SettingsDialog::editCheckout(const QString &key, const QJsonValue &value)
{
    const int row = m_checkoutList->currentRow();
    if (m_loading || row < 0 || row >= m_checkouts.size()) {
        return;
    }
    // No agent key means the default agent.
    if ((key == QLatin1String("agent") || key == QLatin1String("args")) && value.toString().isEmpty()) {
        m_checkouts[row].remove(key);
    } else {
        m_checkouts[row].insert(key, value);
    }
}

void SettingsDialog::addCheckout()
{
    const QString start = m_checkouts.isEmpty()
        ? QDir::homePath()
        : QFileInfo(Paths::expandTilde(m_checkouts.last().value(QStringLiteral("path")).toString())).path();
    // Parented past this dialog, whose stylesheet would otherwise restyle the system file dialog.
    const QString dir = QFileDialog::getExistingDirectory(parentWidget(), QStringLiteral("Add checkout"), start);
    if (dir.isEmpty()) {
        return;
    }
    QSet<QString> names;
    int offset = 0;
    for (const QJsonObject &c : std::as_const(m_checkouts)) {
        names.insert(c.value(QStringLiteral("name")).toString());
        offset = std::max(offset, c.value(QStringLiteral("portOffset")).toInt() + 10000);
    }
    const QString base = QFileInfo(dir).fileName();
    QString name = base;
    for (int n = 2; names.contains(name); ++n) {
        name = QStringLiteral("%1-%2").arg(base).arg(n);
    }
    m_checkouts.append(
        QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("path"), Paths::tilde(dir)}, {QStringLiteral("portOffset"), offset}});
    m_checkoutList->addItem(name);
    m_checkoutList->setCurrentRow(static_cast<int>(m_checkouts.size()) - 1);
    m_checkoutName->setFocus();
    m_checkoutName->selectAll();
}

void SettingsDialog::moveCheckout(int delta)
{
    const int row = m_checkoutList->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || to >= m_checkouts.size()) {
        return;
    }
    m_checkouts.swapItemsAt(row, to);
    {
        const QSignalBlocker block(m_checkoutList);
        m_checkoutList->insertItem(to, m_checkoutList->takeItem(row));
        m_checkoutList->setCurrentRow(to);
    }
    loadCheckout(to);
}

void SettingsDialog::checkPath()
{
    const QString path = m_checkoutPath->text().trimmed();
    const bool missing = !path.isEmpty() && !QFileInfo(Paths::expandTilde(path)).isDir();
    m_checkoutPathWarning->setText(missing ? QStringLiteral("This folder does not exist.") : QString());
    m_checkoutPathWarning->setVisible(missing);
}

// ───────────────────────── agents ─────────────────────────

QWidget *SettingsDialog::hookRow(int cli)
{
    HookRow &row = m_hooks[cli];
    row.status = hint(QString());
    row.status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row.install = new QPushButton(QStringLiteral("Install"));
    row.uninstall = new QPushButton(QStringLiteral("Uninstall"));
    for (QPushButton *b : {row.install, row.uninstall}) {
        b->setObjectName(QStringLiteral("small"));
        b->setAutoDefault(false);
    }
    connect(row.install, &QPushButton::clicked, this, [this, cli] { changeHook(cli, true); });
    connect(row.uninstall, &QPushButton::clicked, this, [this, cli] { changeHook(cli, false); });
    auto *box = new QWidget;
    auto *layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(row.status, 1);
    layout->addWidget(row.install);
    layout->addWidget(row.uninstall);
    return box;
}

void SettingsDialog::refreshHooks()
{
    const QString hook = HookInstaller::hookPath();
    const QString command = HookInstaller::command(hook);
    for (int cli = 0; cli < 2; ++cli) {
        const HookRow &row = m_hooks[cli];
        const QString file = HookInstaller::settingsFile(HookInstaller::Cli(cli));
        const QString shown = Paths::tilde(file);
        const HookInstaller::Status st = HookInstaller::status(HookInstaller::Cli(cli), file, command);
        QString text;
        const char *name = "hint";
        switch (st.state) {
        case HookInstaller::State::Installed:
            text = QStringLiteral("✓ Installed in %1").arg(shown);
            break;
        case HookInstaller::State::Outdated:
            text = QStringLiteral("Installed in %1 with another path or missing events (%2). Reinstall to update it.")
                       .arg(shown, st.command);
            name = "warning";
            break;
        case HookInstaller::State::NotInstalled:
            text = QStringLiteral("Not installed in %1").arg(shown);
            break;
        case HookInstaller::State::Error:
            text = st.error;
            name = "warning";
            break;
        }
        if (hook.isEmpty()) {
            text += QStringLiteral(" x3ro-deck-hook was not found next to X3RO Deck or on PATH.");
            name = "warning";
        }
        row.status->setObjectName(QLatin1String(name));
        row.status->setText(text);
        row.status->style()->unpolish(row.status);
        row.status->style()->polish(row.status);
        row.install->setText(st.state == HookInstaller::State::NotInstalled ? QStringLiteral("Install") : QStringLiteral("Reinstall"));
        row.install->setEnabled(!hook.isEmpty() && st.state != HookInstaller::State::Error);
        row.uninstall->setEnabled(st.state == HookInstaller::State::Installed || st.state == HookInstaller::State::Outdated);
    }
}

void SettingsDialog::changeHook(int cli, bool install)
{
    const QString file = HookInstaller::settingsFile(HookInstaller::Cli(cli));
    QString error;
    const bool ok = install
        ? HookInstaller::install(HookInstaller::Cli(cli), file, HookInstaller::command(HookInstaller::hookPath()), &error)
        : HookInstaller::uninstall(file, &error);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Hooks"), error);
    }
    refreshHooks();
}

QWidget *SettingsDialog::buildAgentsPage()
{
    m_defaultAgentBox = new QComboBox;
    m_defaultAgentBox->setMinimumWidth(240);
    auto *top = form();
    addField(top, QStringLiteral("Default agent"), m_defaultAgentBox, hint(QStringLiteral("Runs in checkouts whose agent is Default.")));
    addField(top, QStringLiteral("Claude Code hook"), hookRow(int(HookInstaller::Cli::Claude)));
    addField(top, QStringLiteral("Codex hook"), hookRow(int(HookInstaller::Cli::Codex)),
             hint(QStringLiteral("Codex runs a new hook only after you trust it: type /hooks in Codex once after installing.")));
    refreshHooks();

    m_agentList = new QListWidget;
    for (int i = 0; i < m_agents.size(); ++i) {
        m_agentList->addItem(agentLabel(i));
    }
    m_agentButtons = listButtons(QStringLiteral("Add"));
    auto *left = new QWidget;
    left->setFixedWidth(260);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(m_agentList, 1);
    leftLayout->addLayout(m_agentButtons.layout);

    m_agentLabel = lineEdit();
    m_agentKind = new QComboBox;
    for (const auto &[key, text] : kKinds) {
        m_agentKind->addItem(text, key);
    }
    m_agentKindHint = hint(QString());
    m_agentCommand = lineEdit(true);
    m_agentResume = lineEdit(true);

    m_agentEnv = table({QStringLiteral("Variable"), QStringLiteral("Value")});
    m_agentEnv->setObjectName(QStringLiteral("env"));
    m_agentEnv->horizontalHeader()->resizeSection(0, 260);
    m_agentEnv->setFixedHeight(150);
    const ListButtons envButtons = listButtons(QStringLiteral("Add variable"));
    envButtons.up->hide();
    envButtons.down->hide();
    auto *envBox = new QWidget;
    auto *envLayout = new QVBoxLayout(envBox);
    envLayout->setContentsMargins(0, 0, 0, 0);
    envLayout->setSpacing(6);
    envLayout->addWidget(m_agentEnv);
    envLayout->addLayout(envButtons.layout);

    m_agentPrompts = new QPlainTextEdit;
    m_agentPrompts->setProperty("mono", true);
    m_agentPrompts->setTabChangesFocus(true);
    m_agentPrompts->setFixedHeight(84);
    m_agentPrPrompt = lineEdit(true);
    m_agentProcess = lineEdit(true);

    m_keyAllow = lineEdit(true);
    m_keyAlways = lineEdit(true);
    m_keyDeny = lineEdit(true);
    m_agentKeys = new QWidget;
    auto *keysLayout = new QHBoxLayout(m_agentKeys);
    keysLayout->setContentsMargins(0, 0, 0, 0);
    keysLayout->setSpacing(8);
    for (const auto &[text, edit] : {std::pair{"Allow", m_keyAllow}, std::pair{"Always", m_keyAlways}, std::pair{"Deny", m_keyDeny}}) {
        edit->setFixedWidth(96);
        keysLayout->addWidget(new QLabel(QLatin1String(text)));
        keysLayout->addWidget(edit);
    }
    keysLayout->addStretch();
    m_agentKeysHint = hint(QStringLiteral("tmux key names that the Allow, Always and Deny buttons send."));
    m_agentDelay = numberEdit(5000);
    auto *delayRow = new QWidget;
    auto *delayLayout = new QHBoxLayout(delayRow);
    delayLayout->setContentsMargins(0, 0, 0, 0);
    delayLayout->setSpacing(8);
    delayLayout->addWidget(m_agentDelay);
    delayLayout->addWidget(new QLabel(QStringLiteral("ms")));
    delayLayout->addStretch();

    m_agentFields = form();
    addField(m_agentFields, QStringLiteral("Name"), m_agentLabel);
    addField(m_agentFields, QStringLiteral("Runs"), m_agentKind, m_agentKindHint);
    addField(m_agentFields, QStringLiteral("Command"), m_agentCommand,
             hint(QStringLiteral("Typed into the shell of the checkout's agent session.")));
    addField(m_agentFields, QStringLiteral("Resume arguments"), m_agentResume,
             hint(QStringLiteral("Added to the command by Continue; empty hides that button.")));
    addField(m_agentFields, QStringLiteral("Environment"), envBox,
             hint(QStringLiteral(
                 "Set on the agent's session when it starts, never typed. $VAR and ${VAR} expand from X3RO Deck's own environment.")),
             true);
    addField(m_agentFields, QStringLiteral("Prompt buttons"), m_agentPrompts,
             hint(QStringLiteral("One prompt per line. Each becomes a button that sends it to the agent.")), true);
    addField(m_agentFields, QStringLiteral("PR prompt"), m_agentPrPrompt,
             hint(QStringLiteral("Sent by the PR button while the branch has no PR; empty hides the button.")));
    m_agentFields->addWidget(section(QStringLiteral("Advanced")), nextRow(m_agentFields), 0, 1, 2);
    addField(
        m_agentFields, QStringLiteral("Process"), m_agentProcess,
        hint(QStringLiteral("The session's foreground process while the agent runs, which tells a running agent from an idle shell.")));
    addField(m_agentFields, QStringLiteral("Permission keys"), m_agentKeys, m_agentKeysHint);
    addField(m_agentFields, QStringLiteral("Submit delay"), delayRow,
             hint(QStringLiteral("Pause between typing a prompt and pressing Enter, for CLIs that read fast input as a paste.")));

    m_agentForm = new QWidget;
    m_agentForm->setObjectName(QStringLiteral("scrollContent"));
    auto *formLayout = new QVBoxLayout(m_agentForm);
    formLayout->setContentsMargins(0, 0, 12, 0);
    formLayout->addLayout(m_agentFields);
    formLayout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_agentForm);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *body = new QHBoxLayout;
    body->setSpacing(24);
    body->addWidget(left);
    body->addWidget(scroll, 1);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 8);
    layout->setSpacing(10);
    layout->addLayout(top);
    layout->addLayout(body, 1);

    connect(m_defaultAgentBox, &QComboBox::activated, this, [this](int index) {
        m_defaultAgent = m_defaultAgentBox->itemData(index).toString();
        refreshAgentChoices();
    });
    connect(m_agentList, &QListWidget::currentRowChanged, this, &SettingsDialog::loadAgent);
    connect(m_agentButtons.add, &QPushButton::clicked, this, [this] {
        addAgent();
        m_agentLabel->setFocus();
        m_agentLabel->selectAll();
    });
    connect(m_agentButtons.remove, &QPushButton::clicked, this, &SettingsDialog::removeAgent);
    connect(m_agentButtons.up, &QPushButton::clicked, this, [this] { moveAgent(-1); });
    connect(m_agentButtons.down, &QPushButton::clicked, this, [this] { moveAgent(1); });
    connect(m_agentLabel, &QLineEdit::textEdited, this, [this](const QString &text) {
        editAgent(QStringLiteral("label"), text.trimmed());
        if (QListWidgetItem *item = m_agentList->currentItem()) {
            item->setText(agentLabel(m_agentList->currentRow()));
        }
        refreshAgentChoices();
    });
    connect(m_agentKind, &QComboBox::activated, this, [this](int index) { setAgentKind(m_agentKind->itemData(index).toString()); });
    const QList<QPair<QLineEdit *, QString>> texts{{m_agentCommand, QStringLiteral("command")},
                                                   {m_agentResume, QStringLiteral("continueArgs")},
                                                   {m_agentPrPrompt, QStringLiteral("prPrompt")},
                                                   {m_agentProcess, QStringLiteral("process")}};
    for (const auto &[edit, key] : texts) {
        connect(edit, &QLineEdit::textEdited, this, [this, key](const QString &text) { editAgent(key, text.trimmed()); });
    }
    for (QLineEdit *edit : {m_keyAllow, m_keyAlways, m_keyDeny}) {
        connect(edit, &QLineEdit::textEdited, this, [this] {
            editAgent(QStringLiteral("permissionKeys"),
                      QJsonObject{{QStringLiteral("allow"), m_keyAllow->text().trimmed()},
                                  {QStringLiteral("always"), m_keyAlways->text().trimmed()},
                                  {QStringLiteral("deny"), m_keyDeny->text().trimmed()}});
        });
    }
    connect(m_agentPrompts, &QPlainTextEdit::textChanged, this, [this] {
        QJsonArray prompts;
        for (const QString &line : m_agentPrompts->toPlainText().split(QLatin1Char('\n'))) {
            if (!line.trimmed().isEmpty()) {
                prompts.append(line.trimmed());
            }
        }
        editAgent(QStringLiteral("prompts"), prompts);
    });
    connect(m_agentDelay, &QLineEdit::textEdited, this,
            [this](const QString &text) { editAgent(QStringLiteral("submitDelayMs"), text.toInt()); });

    auto writeEnv = [this] {
        QJsonObject env;
        for (int r = 0; r < m_agentEnv->rowCount(); ++r) {
            if (const QString name = cellText(m_agentEnv, r, 0); !name.isEmpty()) {
                env.insert(name, m_agentEnv->item(r, 1) ? m_agentEnv->item(r, 1)->text() : QString());
            }
        }
        editAgent(QStringLiteral("env"), env);
    };
    connect(m_agentEnv, &QTableWidget::itemChanged, this, writeEnv);
    connect(envButtons.add, &QPushButton::clicked, this, [this] {
        const int row = m_agentEnv->rowCount();
        m_agentEnv->insertRow(row);
        m_agentEnv->setItem(row, 0, new QTableWidgetItem);
        m_agentEnv->setItem(row, 1, new QTableWidgetItem);
        m_agentEnv->setCurrentCell(row, 0);
        m_agentEnv->editItem(m_agentEnv->item(row, 0));
    });
    connect(envButtons.remove, &QPushButton::clicked, this, [this, writeEnv] {
        if (m_agentEnv->currentRow() >= 0) {
            m_agentEnv->removeRow(m_agentEnv->currentRow());
            writeEnv();
        }
    });
    return page;
}

QString SettingsDialog::agentLabel(int row) const
{
    const Agent &a = m_agents.at(row);
    return a.json.value(QStringLiteral("label")).toString(a.id);
}

void SettingsDialog::loadAgent(int row)
{
    const bool valid = row >= 0 && row < m_agents.size();
    m_agentForm->setEnabled(valid);
    m_agentButtons.remove->setEnabled(valid && m_agents.size() > 1);
    m_agentButtons.up->setEnabled(valid && row > 0);
    m_agentButtons.down->setEnabled(valid && row + 1 < m_agents.size());
    if (!valid) {
        return;
    }
    const QJsonObject &o = m_agents.at(row).json;
    const QString kind = o.value(QStringLiteral("kind")).toString();
    const bool custom = kind == QLatin1String("custom");
    const bool loading = std::exchange(m_loading, true);
    m_agentLabel->setText(agentLabel(row));
    m_agentKind->setCurrentIndex(m_agentKind->findData(kind));
    m_agentKindHint->setText(kindHint(kind));
    m_agentCommand->setText(o.value(QStringLiteral("command")).toString());
    m_agentCommand->setPlaceholderText(custom ? QStringLiteral("aider --model sonnet") : QString());
    m_agentResume->setText(o.value(QStringLiteral("continueArgs")).toString());
    m_agentEnv->setRowCount(0);
    const QJsonObject env = o.value(QStringLiteral("env")).toObject();
    for (auto it = env.begin(); it != env.end(); ++it) {
        const int r = m_agentEnv->rowCount();
        m_agentEnv->insertRow(r);
        m_agentEnv->setItem(r, 0, new QTableWidgetItem(it.key()));
        m_agentEnv->setItem(r, 1, new QTableWidgetItem(it.value().toString()));
    }
    QStringList prompts;
    for (const QJsonValue &v : o.value(QStringLiteral("prompts")).toArray()) {
        prompts.append(v.toString());
    }
    m_agentPrompts->setPlainText(prompts.join(QLatin1Char('\n')));
    m_agentPrPrompt->setText(o.value(QStringLiteral("prPrompt")).toString());
    m_agentProcess->setText(o.value(QStringLiteral("process")).toString());
    m_agentProcess->setPlaceholderText(custom ? QStringLiteral("The command's first word") : QString());
    const QJsonObject keys = o.value(QStringLiteral("permissionKeys")).toObject();
    m_keyAllow->setText(keys.value(QStringLiteral("allow")).toString());
    m_keyAlways->setText(keys.value(QStringLiteral("always")).toString());
    m_keyDeny->setText(keys.value(QStringLiteral("deny")).toString());
    m_agentDelay->setText(QString::number(o.value(QStringLiteral("submitDelayMs")).toInt()));
    // A custom command never asks for permission through hooks.
    setRowVisible(m_agentFields, m_agentKeys, !custom);
    setRowVisible(m_agentFields, m_agentKeysHint, !custom);
    m_loading = loading;
}

void SettingsDialog::editAgent(const QString &key, const QJsonValue &value)
{
    const int row = m_agentList->currentRow();
    if (!m_loading && row >= 0 && row < m_agents.size()) {
        m_agents[row].json.insert(key, value);
    }
}

void SettingsDialog::setAgentKind(const QString &kind)
{
    const int row = m_agentList->currentRow();
    if (row < 0) {
        return;
    }
    QJsonObject &o = m_agents[row].json;
    const QJsonObject from = Config::kindDefaults(o.value(QStringLiteral("kind")).toString());
    const QJsonObject to = Config::kindDefaults(kind);
    QStringList keys = from.keys() + to.keys();
    keys.removeDuplicates();
    // Fields still at the old kind's defaults take the new kind's.
    for (const QString &key : std::as_const(keys)) {
        if (o.value(key) != from.value(key)) {
            continue;
        }
        if (to.contains(key)) {
            o.insert(key, to.value(key));
        } else {
            o.remove(key);
        }
    }
    o.insert(QStringLiteral("kind"), kind);
    loadAgent(row);
}

int SettingsDialog::addAgent()
{
    Agent a{QStringLiteral(":new%1").arg(++m_newAgents), Config::kindDefaults(QStringLiteral("custom"))};
    a.json.insert(QStringLiteral("kind"), QStringLiteral("custom"));
    a.json.insert(QStringLiteral("label"), QStringLiteral("Custom"));
    m_agents.append(a);
    const int row = static_cast<int>(m_agents.size()) - 1;
    m_agentList->addItem(agentLabel(row));
    refreshAgentChoices();
    m_agentList->setCurrentRow(row);
    return row;
}

void SettingsDialog::removeAgent()
{
    const int row = m_agentList->currentRow();
    if (row < 0 || m_agents.size() <= 1) {
        return;
    }
    const QString id = m_agents.takeAt(row).id;
    for (QJsonObject &c : m_checkouts) {
        if (c.value(QStringLiteral("agent")).toString() == id) {
            c.remove(QStringLiteral("agent"));
        }
    }
    if (m_defaultAgent == id) {
        m_defaultAgent = m_agents.first().id;
    }
    delete m_agentList->takeItem(row);
    refreshAgentChoices();
    loadAgent(m_agentList->currentRow());
}

void SettingsDialog::moveAgent(int delta)
{
    const int row = m_agentList->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || to >= m_agents.size()) {
        return;
    }
    m_agents.swapItemsAt(row, to);
    {
        const QSignalBlocker block(m_agentList);
        m_agentList->insertItem(to, m_agentList->takeItem(row));
        m_agentList->setCurrentRow(to);
    }
    loadAgent(to);
    refreshAgentChoices();
}

void SettingsDialog::refreshAgentChoices()
{
    const bool loading = std::exchange(m_loading, true);
    QString defaultLabel;
    m_defaultAgentBox->clear();
    for (int i = 0; i < m_agents.size(); ++i) {
        m_defaultAgentBox->addItem(agentLabel(i), m_agents.at(i).id);
        if (m_agents.at(i).id == m_defaultAgent) {
            defaultLabel = agentLabel(i);
        }
    }
    m_defaultAgentBox->setCurrentIndex(std::max(0, m_defaultAgentBox->findData(m_defaultAgent)));

    m_checkoutAgent->clear();
    m_checkoutAgent->addItem(QStringLiteral("Default (%1)").arg(defaultLabel.isEmpty() ? agentLabel(0) : defaultLabel), QString());
    for (int i = 0; i < m_agents.size(); ++i) {
        m_checkoutAgent->addItem(agentLabel(i), m_agents.at(i).id);
    }
    m_checkoutAgent->insertSeparator(m_checkoutAgent->count());
    m_checkoutAgent->addItem(QStringLiteral("Custom command…"), kNewAgent);
    const int row = m_checkoutList->currentRow();
    const QString current
        = row >= 0 && row < m_checkouts.size() ? m_checkouts.at(row).value(QStringLiteral("agent")).toString() : QString();
    m_checkoutAgent->setCurrentIndex(std::max(0, m_checkoutAgent->findData(current)));
    m_loading = loading;
}

// ───────────────────────── docker ─────────────────────────

QWidget *SettingsDialog::buildDockerPage()
{
    m_dockerHost = lineEdit(true);
    m_dockerHost->setPlaceholderText(QStringLiteral("automatic"));
    m_dockerHost->setText(m_root.value(QStringLiteral("dockerHost")).toString());
    m_dockerEndpoint = hint(QString());
    m_dockerEndpoint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto showEndpoint = [this] {
        m_dockerEndpoint->setText(QStringLiteral("In use: ") + resolveDockerEndpoint(m_dockerHost->text()).display());
    };
    connect(m_dockerHost, &QLineEdit::textChanged, this, showEndpoint);
    showEndpoint();
    m_composeBinary = new QComboBox;
    m_composeBinary->setEditable(true);
    m_composeBinary->addItems({QStringLiteral("docker compose"), QStringLiteral("docker-compose"), QStringLiteral("podman compose")});
    m_composeBinary->setCurrentText(m_root.value(QStringLiteral("composeBinary")).toString(QStringLiteral("docker compose")));
    auto *engine = form();
    addField(engine, QStringLiteral("Docker host"), m_dockerHost, m_dockerEndpoint);
    addField(engine, QStringLiteral("Compose binary"), m_composeBinary,
             hint(QStringLiteral("What {compose} in the commands below and in quick commands becomes.")));

    m_composeUp = lineEdit(true);
    m_composeDown = lineEdit(true);
    m_composeRestart = lineEdit(true);
    m_composeUp->setText(m_root.value(QStringLiteral("composeUp")).toString());
    m_composeDown->setText(m_root.value(QStringLiteral("composeDown")).toString());
    m_composeRestart->setText(m_root.value(QStringLiteral("composeRestart")).toString());
    auto *compose = form();
    addField(compose, QStringLiteral("Up"), m_composeUp);
    addField(compose, QStringLiteral("Down"), m_composeDown);
    addField(compose, QStringLiteral("Restart"), m_composeRestart,
             hint(QStringLiteral("Each runs in a shell tab in the checkout's folder.")));

    m_quick = table({QStringLiteral("Label"), QStringLiteral("Command"), QStringLiteral("Group")});
    m_quick->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_quick->horizontalHeader()->setStretchLastSection(false);
    m_quick->horizontalHeader()->resizeSection(0, 180);
    m_quick->horizontalHeader()->resizeSection(2, 120);
    const QJsonArray quick = m_root.value(QStringLiteral("quickCommands")).toArray();
    for (const QJsonValue &v : quick) {
        m_quick->insertRow(m_quick->rowCount());
        setQuickRow(m_quick, m_quick->rowCount() - 1, v.toObject());
    }

    m_links = table({QStringLiteral("Label"), QStringLiteral("Service"), QStringLiteral("Container port")});
    m_links->horizontalHeader()->resizeSection(0, 180);
    m_links->horizontalHeader()->resizeSection(1, 240);
    const QJsonArray links = m_root.value(QStringLiteral("links")).toArray();
    for (const QJsonValue &v : links) {
        m_links->insertRow(m_links->rowCount());
        setLinkRow(m_links, m_links->rowCount() - 1, v.toObject());
    }

    // A table with add, remove and reorder buttons under it.
    auto editable = [](QTableWidget *t, auto read, auto write) {
        const ListButtons b = listButtons(QStringLiteral("Add"));
        QObject::connect(b.add, &QPushButton::clicked, t, [t, write] {
            const int row = t->rowCount();
            t->insertRow(row);
            write(t, row, QJsonObject());
            t->setCurrentCell(row, 0);
            t->editItem(t->item(row, 0));
        });
        QObject::connect(b.remove, &QPushButton::clicked, t, [t] {
            if (t->currentRow() >= 0) {
                t->removeRow(t->currentRow());
            }
        });
        auto move = [t, read, write](int delta) {
            const int row = t->currentRow();
            const int to = row + delta;
            if (row < 0 || to < 0 || to >= t->rowCount()) {
                return;
            }
            const QJsonObject a = read(t, row);
            write(t, row, read(t, to));
            write(t, to, a);
            t->setCurrentCell(to, std::max(0, t->currentColumn()));
        };
        QObject::connect(b.up, &QPushButton::clicked, t, [move] { move(-1); });
        QObject::connect(b.down, &QPushButton::clicked, t, [move] { move(1); });
        auto *box = new QWidget;
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        layout->addWidget(t, 1);
        layout->addLayout(b.layout);
        return box;
    };

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 8);
    layout->setSpacing(8);
    layout->addWidget(section(QStringLiteral("Engine")));
    layout->addWidget(hint(QStringLiteral("Empty finds it: DOCKER_HOST, the active docker context, then rootless Docker, Podman, "
                                          "Colima, OrbStack and /var/run/docker.sock. Only unix:// sockets work.")));
    layout->addLayout(engine);
    layout->addSpacing(6);
    layout->addWidget(section(QStringLiteral("Docker Compose")));
    layout->addLayout(compose);
    layout->addSpacing(6);
    layout->addWidget(section(QStringLiteral("Quick commands")));
    layout->addWidget(hint(QStringLiteral("{compose} becomes the compose binary, {project} the compose project and "
                                          "{port:<service>:<containerPort>} the port Docker published for it. The group picks the "
                                          "button colour.")));
    layout->addWidget(editable(m_quick, quickRow, setQuickRow), 3);
    layout->addSpacing(6);
    layout->addWidget(section(QStringLiteral("Links")));
    layout->addWidget(hint(QStringLiteral("Each opens http://localhost on the port Docker published for the service's container port.")));
    layout->addWidget(editable(m_links, linkRow, setLinkRow), 2);
    return page;
}

// ───────────────────────── general ─────────────────────────

QWidget *SettingsDialog::buildGeneralPage()
{
    m_autostart = new QCheckBox(QStringLiteral("Start the agent when a checkout's agent session is created"));
    m_autostart->setChecked(m_root.value(QStringLiteral("autostartAgents")).toBool(true));
    m_notifyDone = new QCheckBox(QStringLiteral("Notify when an agent finishes and waits for you"));
    m_notifyDone->setChecked(m_root.value(QStringLiteral("notifyOnDone")).toBool(true));

    // Probing the sources reads a few small files; the dialog keeps its own
    // registry and never watches them.
    m_terminals = new TerminalSettings(this);
    m_terminalSource = new QComboBox;
    m_terminalSource->setMinimumWidth(280);
    const TerminalSettingsSource *detected = m_terminals->desktopDefault();
    m_terminalSource->addItem(detected ? QStringLiteral("Automatic (%1)").arg(detected->displayName())
                                       : QStringLiteral("Automatic (no supported terminal found)"),
                              QString());
    const QString source = m_root.value(QStringLiteral("terminalSettings")).toString().trimmed();
    for (const TerminalSettingsSource *s : m_terminals->sources()) {
        const bool found = s->detect();
        if (found || s->id() == source) {
            m_terminalSource->addItem(found ? s->displayName() : QStringLiteral("%1 (not found)").arg(s->displayName()), s->id());
        }
    }
    m_terminalSource->addItem(QStringLiteral("None (X3RO Deck's colors and your login shell)"), TerminalSettings::none());
    int sourceIndex = m_terminalSource->findData(source);
    if (sourceIndex < 0) {
        m_terminalSource->addItem(QStringLiteral("%1 (unknown)").arg(source), source);
        sourceIndex = m_terminalSource->count() - 1;
    }
    m_terminalSource->setCurrentIndex(sourceIndex);
    m_terminalProfile = new QComboBox;
    m_terminalProfile->setMinimumWidth(280);
    fillTerminalProfiles(m_root.value(QStringLiteral("terminalProfile")).toString().trimmed());
    connect(m_terminalSource, &QComboBox::currentIndexChanged, this, [this] { fillTerminalProfiles(QString()); });

    auto *terminals = form();
    addField(terminals, QStringLiteral("Import settings from"), m_terminalSource,
             hint(QStringLiteral("The terminals take the colors, font, cursor, scrollback and shell from this terminal's "
                                 "profile, and follow changes to it.")));
    addField(terminals, QStringLiteral("Profile"), m_terminalProfile);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 8);
    layout->setSpacing(8);
    layout->addWidget(section(QStringLiteral("Agents")));
    layout->addWidget(m_autostart);
    layout->addWidget(hint(QStringLiteral("When off, a new session waits at a shell prompt until you press Start.")));
    layout->addSpacing(6);
    layout->addWidget(m_notifyDone);
    layout->addWidget(hint(QStringLiteral("Permission prompts, questions and container problems always notify.")));
    layout->addSpacing(14);
    layout->addWidget(section(QStringLiteral("Terminals")));
    layout->addLayout(terminals);
    layout->addSpacing(14);
    layout->addWidget(section(QStringLiteral("Dependencies")));
    m_dependencies = form();
    layout->addLayout(m_dependencies);
    layout->addStretch();
    auto *about = new QPushButton(QStringLiteral("About x3ro deck…"));
    connect(about, &QPushButton::clicked, this, &SettingsDialog::aboutRequested);
    auto *aboutRow = new QHBoxLayout;
    aboutRow->addWidget(about);
    aboutRow->addStretch();
    layout->addLayout(aboutRow);
    return page;
}

void SettingsDialog::setDependencies(const QList<Dependency> &list, bool ready)
{
    while (QLayoutItem *item = m_dependencies->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const Dependency &d : list) {
        QLabel *status = nullptr;
        if (!d.path.isEmpty()) {
            status = hint(QStringLiteral("✓ ") + d.path);
            status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        } else if (!ready) {
            status = hint(QStringLiteral("Checking…"));
        } else {
            status = hint(QStringLiteral("Not found. ") + d.hint, "warning");
            status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        }
        status->setToolTip(d.purpose);
        const int row = nextRow(m_dependencies);
        auto *name = new QLabel(d.name);
        name->setToolTip(d.purpose);
        m_dependencies->addWidget(name, row, 0, Qt::AlignLeft | Qt::AlignTop);
        m_dependencies->addWidget(status, row, 1);
    }
}

void SettingsDialog::fillTerminalProfiles(const QString &selected)
{
    m_terminalProfile->clear();
    const QString id = m_terminalSource->currentData().toString();
    const TerminalSettingsSource *s = id.isEmpty() ? m_terminals->desktopDefault() : m_terminals->source(id);
    if (!s || !s->detect()) {
        m_terminalProfile->addItem(QStringLiteral("—"), QString());
        m_terminalProfile->setEnabled(false);
        return;
    }
    m_terminalProfile->setEnabled(true);
    const QList<TerminalSettingsSource::ProfileRef> profiles = s->profiles();
    const QString def = s->defaultProfile();
    QString defName = def;
    for (const auto &p : profiles) {
        if (p.id == def) {
            defName = p.name;
        }
    }
    m_terminalProfile->addItem(QStringLiteral("Default (%1)").arg(defName), QString());
    for (const auto &p : profiles) {
        m_terminalProfile->addItem(p.name, p.id);
    }
    int index = m_terminalProfile->findData(selected);
    if (index < 0) {
        m_terminalProfile->addItem(QStringLiteral("%1 (not found)").arg(selected), selected);
        index = m_terminalProfile->count() - 1;
    }
    m_terminalProfile->setCurrentIndex(index);
}

// ───────────────────────── result ─────────────────────────

QJsonObject SettingsDialog::config() const
{
    QJsonObject root = m_root;

    // New agents get ids from their labels; references to them follow.
    QSet<QString> taken;
    for (const Agent &a : m_agents) {
        if (!a.id.startsWith(QLatin1Char(':'))) {
            taken.insert(a.id);
        }
    }
    QHash<QString, QString> ids;
    QJsonArray agents;
    for (const Agent &a : m_agents) {
        QString id = a.id;
        if (id.startsWith(QLatin1Char(':'))) {
            id = slug(a.json.value(QStringLiteral("label")).toString(), taken);
            taken.insert(id);
            ids.insert(a.id, id);
        }
        // Keys at their kind's defaults stay out of the file, so the profile
        // keeps following those defaults.
        QJsonObject o = a.json;
        const QJsonObject defaults = Config::kindDefaults(o.value(QStringLiteral("kind")).toString());
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            if (o.value(it.key()) == it.value()) {
                o.remove(it.key());
            }
        }
        if (o.value(QStringLiteral("env")).toObject().isEmpty()) {
            o.remove(QStringLiteral("env"));
        }
        o.insert(QStringLiteral("id"), id);
        agents.append(o);
    }
    root.insert(QStringLiteral("agents"), agents);
    root.insert(QStringLiteral("defaultAgent"), ids.value(m_defaultAgent, m_defaultAgent));

    QJsonArray checkouts;
    for (const QJsonObject &checkout : m_checkouts) {
        QJsonObject c = checkout;
        if (c.contains(QStringLiteral("agent"))) {
            const QString agent = c.value(QStringLiteral("agent")).toString();
            c.insert(QStringLiteral("agent"), ids.value(agent, agent));
        }
        checkouts.append(c);
    }
    root.insert(QStringLiteral("checkouts"), checkouts);

    root.insert(QStringLiteral("dockerHost"), m_dockerHost->text().trimmed());
    root.insert(QStringLiteral("composeBinary"), m_composeBinary->currentText().trimmed());
    root.insert(QStringLiteral("composeUp"), m_composeUp->text().trimmed());
    root.insert(QStringLiteral("composeDown"), m_composeDown->text().trimmed());
    root.insert(QStringLiteral("composeRestart"), m_composeRestart->text().trimmed());
    QJsonArray quick;
    for (int r = 0; r < m_quick->rowCount(); ++r) {
        const QJsonObject o = quickRow(m_quick, r);
        if (!o.value(QStringLiteral("label")).toString().isEmpty() || !o.value(QStringLiteral("command")).toString().isEmpty()) {
            quick.append(o);
        }
    }
    root.insert(QStringLiteral("quickCommands"), quick);
    QJsonArray links;
    for (int r = 0; r < m_links->rowCount(); ++r) {
        const QJsonObject o = linkRow(m_links, r);
        if (!o.value(QStringLiteral("label")).toString().isEmpty() || !o.value(QStringLiteral("service")).toString().isEmpty()
            || o.value(QStringLiteral("containerPort")).toInt() > 0) {
            links.append(o);
        }
    }
    root.insert(QStringLiteral("links"), links);

    root.insert(QStringLiteral("autostartAgents"), m_autostart->isChecked());
    root.insert(QStringLiteral("notifyOnDone"), m_notifyDone->isChecked());
    root.insert(QStringLiteral("terminalSettings"), m_terminalSource->currentData().toString());
    root.insert(QStringLiteral("terminalProfile"), m_terminalProfile->currentData().toString());
    return root;
}

void SettingsDialog::showPage(const QString &page)
{
    if (const qsizetype index = kPages.indexOf(page); index >= 0) {
        m_tabs->setCurrentIndex(static_cast<int>(index));
    }
}

bool SettingsDialog::validate()
{
    auto fail = [this](const QString &page, const QString &message) {
        showPage(page);
        QMessageBox::warning(this, QStringLiteral("Settings"), message);
        return false;
    };

    QSet<QString> names;
    for (int i = 0; i < m_checkouts.size(); ++i) {
        const QString name = m_checkouts.at(i).value(QStringLiteral("name")).toString();
        QString problem;
        if (name.isEmpty()) {
            problem = QStringLiteral("Every checkout needs a name.");
        } else if (names.contains(name)) {
            problem = QStringLiteral("Two checkouts are named %1.").arg(name);
        } else if (m_checkouts.at(i).value(QStringLiteral("path")).toString().isEmpty()) {
            problem = QStringLiteral("%1 needs a folder.").arg(name);
        }
        if (!problem.isEmpty()) {
            m_checkoutList->setCurrentRow(i);
            return fail(QStringLiteral("checkouts"), problem);
        }
        names.insert(name);
    }

    static const QRegularExpression envName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (int i = 0; i < m_agents.size(); ++i) {
        const QJsonObject &o = m_agents.at(i).json;
        QString problem;
        if (agentLabel(i).isEmpty()) {
            problem = QStringLiteral("Every agent needs a name.");
        } else if (o.value(QStringLiteral("command")).toString().isEmpty()) {
            problem = QStringLiteral("%1 needs a command.").arg(agentLabel(i));
        }
        const QStringList vars = o.value(QStringLiteral("env")).toObject().keys();
        for (const QString &var : vars) {
            if (problem.isEmpty() && !envName.match(var).hasMatch()) {
                problem = QStringLiteral("%1 sets %2, which is not a valid environment variable name.").arg(agentLabel(i), var);
            }
        }
        if (!problem.isEmpty()) {
            m_agentList->setCurrentRow(i);
            return fail(QStringLiteral("agents"), problem);
        }
    }

    for (int r = 0; r < m_quick->rowCount(); ++r) {
        const QJsonObject o = quickRow(m_quick, r);
        if (o.value(QStringLiteral("label")).toString().isEmpty() != o.value(QStringLiteral("command")).toString().isEmpty()) {
            m_quick->setCurrentCell(r, 0);
            return fail(QStringLiteral("docker"), QStringLiteral("Every quick command needs a label and a command."));
        }
    }
    for (int r = 0; r < m_links->rowCount(); ++r) {
        const QJsonObject o = linkRow(m_links, r);
        const int filled = int(!o.value(QStringLiteral("label")).toString().isEmpty())
            + int(!o.value(QStringLiteral("service")).toString().isEmpty()) + int(o.value(QStringLiteral("containerPort")).toInt() > 0);
        if (filled > 0 && filled < 3) {
            m_links->setCurrentCell(r, 0);
            return fail(QStringLiteral("docker"), QStringLiteral("Every link needs a label, a service and a container port."));
        }
    }
    return true;
}

void SettingsDialog::accept()
{
    if (validate()) {
        QDialog::accept();
    }
}

void SettingsDialog::reject()
{
    if (dirty()
        && QMessageBox::question(this, QStringLiteral("Settings"), QStringLiteral("Discard your changes?"),
                                 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel)
            != QMessageBox::Discard) {
        return;
    }
    QDialog::reject();
}
