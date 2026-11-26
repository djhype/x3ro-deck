# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later

Name:           x3ro-deck
Version:        0.1.1
Release:        1%{?dist}
Summary:        Desktop cockpit for coding agents and their Docker Compose stacks
License:        GPL-3.0-or-later
URL:            https://github.com/djhype/x3ro-deck
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.24
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(vterm)
BuildRequires:  pkgconfig(xcb)
%if 0%{?suse_version}
BuildRequires:  qt6-base-devel
BuildRequires:  qt6-declarative-devel
BuildRequires:  hicolor-icon-theme
%else
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
%endif
BuildRequires:  desktop-file-utils
Recommends:     tmux
Recommends:     git
Suggests:       gh

%description
x3ro deck runs Claude Code, Codex or any CLI agent in each of your checkouts,
collects permission prompts and questions in one place, and shows each
checkout's Docker Compose stack, quick commands and pull request.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -G Ninja -DBUILD_TESTING=OFF
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/x3ro-deck.desktop

%files
%license LICENSE TRADEMARKS.md
%doc README.md CHANGELOG.md
%{_bindir}/x3ro-deck
%{_bindir}/x3ro-deck-attach
%{_bindir}/x3ro-deck-hook
%{_bindir}/x3ro-deck-sessiond
%{_datadir}/applications/x3ro-deck.desktop
%{_datadir}/icons/hicolor/*/apps/x3ro-deck.*

%changelog
* Wed Nov 25 2026 x3ro - 0.1.1-1
- Release 0.1.1; see CHANGELOG.md.

* Wed Nov 18 2026 x3ro - 0.1.0-1
- First release.
