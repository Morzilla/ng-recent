# SPDX-License-Identifier: CC0-1.0
%global commit      7638e8f03cd3283f40b9587dacfa4259c40d85f3
%global shortcommit %(c=%{commit}; echo ${c:0:7})
%global snapdate    202611262238

Name:           ng-recent
Version:        0.1^%{snapdate}git%{shortcommit}
Release:        1%{?dist}
Summary:        What happened lately to files and folders, for KDE file views
License:        LGPL-2.1-or-later AND CC0-1.0
URL:            https://github.com/Morzilla/ng-recent
Source0:        %{url}/archive/%{commit}/ng-recent-%{shortcommit}.tar.gz

BuildRequires:  cmake
BuildRequires:  extra-cmake-modules
BuildRequires:  gcc-c++
BuildRequires:  kf6-rpm-macros
BuildRequires:  qt6-qtbase-devel
BuildRequires:  cmake(KF6KIO)

%description
KDE's "Recent Files" only knows the files an application opened. ng-recent
lists what happened lately to the files and folders of the user: created,
added, modified, downloaded, made by a quick action of Dolphin NG, opened.
It is a KIO worker (ngrecent:/) that needs nothing running. Dolphin NG and
Dialog NG show it in place of "Recent Files" when it is installed.

%prep
%autosetup -n ng-recent-%{commit}

%build
%cmake_kf6
%cmake_build

%install
%cmake_install

%files
%license LICENSES/LGPL-2.1-or-later.txt LICENSES/CC0-1.0.txt
%doc README.md
%{_bindir}/ng-recent-list
%{_kf6_plugindir}/kio/ngrecent.so

%changelog
* Fri Nov 27 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 0.1^202611262238git7638e8f-1
- First package
