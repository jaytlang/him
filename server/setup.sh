#!/bin/sh
# run me to install dependencies and
# set up users on the server before
# installing our proper package

if [ `whoami` != "root" ]; then
        echo "this script must be run as root. try sudo $0"
        exit 1
fi

# kill off the firewall, we have
# infrastructure-level restrictions
# in oracle cloud
iptables -P INPUT ACCEPT
iptables -P OUTPUT ACCEPT
iptables -P FORWARD ACCEPT
iptables -F
ufw --force reset

# disable ipv6
target=`cat << EOF
net.ipv6.conf.all.disable_ipv6=1
net.ipv6.conf.default.disable_ipv6=1
net.ipv6.conf.lo.disable_ipv6=1
EOF`

grep "$target" /etc/sysctl.conf >/dev/null || echo "$target" >> /etc/sysctl.conf
sysctl -p

# install required packages
apt update && apt upgrade -y
apt install -y libevent-dev libseccomp-dev clang build-essential

# make empty directory
mkdir -p /var/empty

# make unprivileged user
adduser --system --shell /sbin/nologin --no-create-home --disabled-password \
	--disabled-login --gecos "Him Daemon User" him || true
