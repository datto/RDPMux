%global origname RDPMux

%global _dbus_conf_dir %{_sysconfdir}/dbus-1/system.d

Name:           rdpmux
Version:        0.6.4
Release:        1%{?dist}
Summary:        RDP server multiplexer designed to work with virtual machines
License:        ASL 2.0
URL:            https://github.com/datto/RDPMux

Source0:        https://github.com/datto/%{origname}/archive/v%{version}/%{origname}-%{version}.tar.gz

%if 0%{?rhel} == 7
BuildRequires:  cmake3 >= 3.2
%else
BuildRequires:  cmake >= 3.2
%endif
BuildRequires:  freerdp-devel >= 2.0
BuildRequires:  glib2-devel
BuildRequires:  glibmm24-devel
BuildRequires:  msgpack-devel
BuildRequires:  boost-devel
BuildRequires:  pixman-devel
BuildRequires:  libsigc++20-devel
BuildRequires:  zeromq-devel >= 4.1.0
BuildRequires:  czmq-devel >= 3.0.0

Requires(post):   systemd
Requires(preun):  systemd
Requires(postun): systemd

%description
%{origname} provides multiplexed RDP servers for virtual machines.

It communicates with VMs via lib%{name}, which implements the
communication protocol and exposes an API for hypervisors to hook into.

%package -n        lib%{name}
License:           MIT
Summary:           Library for implementing hypervisor-side functionality

%description -n    lib%{name}
This library provides a defined interface to interact with
virtual machine guests on a very low level. It provides access
to the current framebuffer of the VM, and to programmatically
send and receive keyboard and mouse events.

%package -n        lib%{name}-devel
License:           MIT
Summary:           Development files for lib%{name}-devel
Requires:          lib%{name}%{?_isa} = %{version}-%{release}

%description -n    lib%{name}-devel
The lib%{name}-devel package contains configuration and header
files for developing applications that use lib%{name}.


%prep
%setup -qn %{origname}-%{version}

%build
%if 0%{?rhel} == 7
%cmake3 -DCMAKE_BUILD_TYPE=RelWithDebInfo .
%else
%cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .
%endif

%make_build V=1

%install
%make_install
mkdir -p %{buildroot}%{_unitdir}
install -pm 0644 dist/rdpmux.service %{buildroot}%{_unitdir}
mkdir -p %{buildroot}%{_dbus_conf_dir}
install -pm 0644 dist/org.RDPMux.RDPMux.conf %{buildroot}%{_dbus_conf_dir}

# Setting up files for ghost file list directive
# See: http://www.rpm.org/max-rpm-snapshot/s1-rpm-inside-files-list-directives.html
mkdir -p %{buildroot}%{_sysconfdir}/rdpmux/shadow
touch %{buildroot}%{_sysconfdir}/rdpmux/shadow/server.key
touch %{buildroot}%{_sysconfdir}/rdpmux/shadow/server.crt

%post
%systemd_post rdpmux.service

%preun
%systemd_preun rdpmux.service

%postun
%systemd_postun_with_restart rdpmux.service

%post -n lib%{name} -p /sbin/ldconfig
%postun -n lib%{name} -p /sbin/ldconfig

%files
%{_bindir}/rdpmux
%{_unitdir}/rdpmux.service
%{_dbus_conf_dir}/org.RDPMux.RDPMux.conf
%license LICENSE
%ghost %{_sysconfdir}/rdpmux
%ghost %{_sysconfdir}/rdpmux/shadow
%ghost %{_sysconfdir}/rdpmux/shadow/server.key
%ghost %{_sysconfdir}/rdpmux/shadow/server.crt

%files -n lib%{name}
%license LICENSE.LIB
%{_libdir}/*.so.*

%files -n lib%{name}-devel
%{_includedir}/*.h
%{_libdir}/*.so
%{_libdir}/pkgconfig/*.pc

%changelog
* Wed Jan 10 2018 Sri Ramanujam <sramanujam@datto.com> - 0.6.4-1
- Bump to 0.6.4

* Tue Dec 5 2017 Sri Ramanujam <sramanujam@datto.com> - 0.6.3-1
- Bump to 0.6.3

* Fri Dec 1 2017 Sri Ramanujam <sramanujam@datto.com> - 0.6.2-1
- Bump to 0.6.2

* Tue Nov 28 2017 Sri Ramanujam <sramanujam@datto.com> - 0.6.1-1
- Bump to 0.6.1

* Wed Aug 23 2017 Sri Ramanujam <sramanujam@datto.com> - 0.6.0-1
- Bump to 0.6.0

* Tue Jun 27 2017 Sri Ramanujam <sramanujam@datto.com> - 0.5.0-1
- Bump to 0.5.0

* Tue Jul 12 2016 Neal Gompa <ngompa@datto.com> - 0.4.0-1
- Bump to 0.4.0

* Thu May 26 2016 Neal Gompa <ngompa@datto.com> - 0.2.4-1
- Bump to 0.2.4

* Tue May 24 2016 Neal Gompa <ngompa@datto.com> - 0.2.2-1
- Bump to 0.2.2

* Tue May 24 2016 Anthony Gargiulo <agargiulo@datto.com> - 0.2.1-1
- Added DBus conf and systemd service files
- Changed file and directory permissions to 777

* Tue May 17 2016 Sri Ramanujam <sramanujam@datto.com> - 0.2.0-1
- New DBus registration protocol

* Fri Apr 29 2016 Sri Ramanujam <sramanujam@datto.com> - 0.1.2-1
- Initial packaging
