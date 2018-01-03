Summary: Controller and monitoring module for Nemea
Name: nemea-supervisor
Version: 1.5.3
Release: 1
URL: http://www.liberouter.org/nemea
Source: https://www.liberouter.org/repo/SOURCES/%{name}-%{version}-%{release}.tar.gz
Group: Liberouter
License: BSD
Vendor: CESNET, z.s.p.o.
Packager:  <>
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
Requires: libtrap libxml2
Requires(pre): /usr/sbin/useradd, /usr/bin/getent
Requires(postun): /usr/sbin/userdel
BuildRequires: gcc make doxygen pkgconfig libtrap-devel libxml2-devel
Provides: nemea-supervisor

%description

%prep
%setup

%build
./configure --prefix=%{_prefix} --libdir=%{_libdir} --sysconfdir=%{_sysconfdir}/nemea --bindir=%{_bindir}/nemea --datarootdir=%{_datarootdir} --docdir=%{_docdir}/nemea-supervisor --localstatedir=%{_localstatedir} -q --enable-silent-rules --disable-repobuild;
make -j5

%install
make -j5 DESTDIR=$RPM_BUILD_ROOT install

%post
%{_bindir}/nemea/prepare_default_config.sh install

%files
%{_bindir}/nemea/*
%{_docdir}/nemea-supervisor/README.md
%{_docdir}/nemea-supervisor/README.munin
%{_sysconfdir}/init.d/nemea-supervisor
%{_datarootdir}/munin/plugins/nemea_supervisor
%{_datarootdir}/nemea-supervisor/*
/usr/lib/systemd/system
%defattr(755, nemead, nemead)
%config(noreplace) %{_sysconfdir}/nemea/ipfixcol-startup.xml
%defattr(664, nemead, nemead)
%config(noreplace) %{_sysconfdir}/nemea/supervisor_config_template.xml
