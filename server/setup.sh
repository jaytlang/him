#!/bin/sh
# run me to install dependencies and
# set up users on the server before
# installing our proper package

if [ `whoami` != "root" ]; then
        echo "this script must be run as root. try sudo $0"
        exit 1
fi

# set up firewall
iptables -P INPUT ACCEPT
iptables -P OUTPUT ACCEPT
iptables -P FORWARD ACCEPT
iptables -F

ufw --force reset
ufw default deny outgoing
ufw default deny incoming
ufw allow ssh
ufw allow 6969
ufw allow out http
ufw allow out https
ufw allow out domain
ufw allow out ntp
ufw enable

# disable ipv6
target=`cat << EOF
net.ipv6.conf.all.disable_ipv6=1
net.ipv6.conf.default.disable_ipv6=1
net.ipv6.conf.lo.disable_ipv6=1
EOF`

grep "$target" /etc/sysctl.conf >/dev/null || echo "$target" >> /etc/sysctl.conf
sysctl -p

# install required packages
apt update && apt upgrade
apt install -y libevent-dev libseccomp-dev clang build-essential

# make empty directory
mkdir -p /var/empty

# make unprivileged user
adduser --system --shell /sbin/nologin --no-create-home --disabled-password \
	--disabled-login --gecos "Him Daemon User" him || true
