// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

// Base of the app's own dialogs; data/style.qss styles them by this class name.
class DeckDialog : public QDialog
{
    Q_OBJECT
public:
    using QDialog::QDialog;
};
