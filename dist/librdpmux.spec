Name:           librdpmux
Version:        0.4.0
Release:        1%{?dist}
Summary:        Library to provide low-level VM guest interaction capabilties outside the hypervisor
License:        MIT
URL:            https://github.com/datto/librdpmux

Source0:        https://github.com/datto/librdpmux/archive/v%{version}/%{name}-%{version}.tar.gz

%if 0%{?rhel} == 7
BuildRequires:  cmake3 >= 3.2
%else
BuildRequires:  cmake >= 3.2
%endif
BuildRequires:  glib2-devel
BuildRequires:  pixman-devel
BuildRequires:  zeromq-devel >= 4.1.0
BuildRequires:  czmq-devel >= 3.0.0

%description
This library provides a defined interface to interact with
virtual machine guests on a very low level. It provides access
to the current framebuffer of the VM, and to programmatically
send and receive keyboard and mouse events.

%package        devel
Summary:        Development files for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description    devel
The %{name}-devel package contains configuration and header
files for developing applications that use %{name}.

%prep
%setup -q

%build
%if 0%{?rhel} == 7
%cmake3 .
%else
%cmake .
%endif

%make_build V=1

%install
%make_install

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%license LICENSE
%{_libdir}/*.so.*

%files devel
%doc doc/html
%{_includedir}/*.h
%{_libdir}/*.so
%{_libdir}/pkgconfig/*.pc

%changelog
* Tue Jul 12 2016 Neal Gompa <ngompa@datto.com> - 0.4.0-1
- Bump to 0.4.0
- Add support for building on RHEL/CentOS 7

* Tue May 17 2016 Sri Ramanujam <sramanujam@datto.com> - 0.3.0-1
- Implement version 2 of the RDPMux registration protocol

* Mon Mar 14 2016 Sri Ramanujam <sramanujam@datto.com> - 0.2.0-1
- Initial packaging
