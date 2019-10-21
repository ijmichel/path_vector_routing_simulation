./cmake-build-debug/manager_send 3 send 6 "message from 3 to 6";
./cmake-build-debug/manager_send 6 send 3 "message from 6 to 3";

./cmake-build-debug/manager_send 1 cost 2 1;
sleep 5.0e-1;
./cmake-build-debug/manager_send 0 send 3 "message from 0 to 3";
sleep 5.0e-1;

sudo iptables -D OUTPUT -s 10.1.1.0 -d 10.1.1.255 -j ACCEPT;
sudo iptables -D OUTPUT -s 10.1.1.255 -d 10.1.1.0 -j ACCEPT;
sleep 2.0;
./cmake-build-debug/manager_send 0 send 3 "message from 0 to 3";
./cmake-build-debug/manager_send 7 send 0 "message from 7 to 0";
sleep 5.0e-1;

sudo iptables -I OUTPUT -s 10.1.1.0 -d 10.1.1.255 -j ACCEPT;
sudo iptables -I OUTPUT -s 10.1.1.255 -d 10.1.1.0 -j ACCEPT;
sleep 5.0e-1;
./cmake-build-debug/manager_send 0 send 3 "message from 0 to 3";
./cmake-build-debug/manager_send 7 send 0 "message from 7 to 0";
sleep 5.0e-1;

sudo iptables -D OUTPUT -s 10.1.1.4 -d 10.1.1.7 -j ACCEPT;
sudo iptables -D OUTPUT -s 10.1.1.7 -d 10.1.1.4 -j ACCEPT;
sleep 2.0;
./cmake-build-debug/manager_send 0 send 7 "message from 0 to 7";

./cmake-build-debug/manager_send 5 send 5 "message from 5 to 5";