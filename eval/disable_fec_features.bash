#!/bin/bash
echo "Disabling eth0 features"

sudo ethtool -K eth0 rx off && \
sudo ethtool -K eth0 tx off && \
sudo ethtool -K eth0 tso off && \
sudo ethtool -K eth0 sg off && \
sudo ethtool -K eth0 gso off && \
sudo ethtool -K eth0 gro off && \
sudo ethtool -K eth0 rxvlan off || exit 1

echo "Done"
exit 0
