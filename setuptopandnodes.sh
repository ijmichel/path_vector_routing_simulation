killall -9 main;
ps;
echo starting to make topo
perl make_topology.pl example_topology/testtopo.txt;
echo Done Make topology
./cmake-build-debug/main 0 example_topology/test2initcosts0 example_topology/test2log0 &
./cmake-build-debug/main 1 example_topology/test2initcosts1 example_topology/test2log1 &
./cmake-build-debug/main 2 example_topology/test2initcosts2 example_topology/test2log2 &
./cmake-build-debug/main 3 example_topology/test2initcosts3 example_topology/test2log3 &
./cmake-build-debug/main 4 example_topology/test2initcosts4 example_topology/test2log4 &
./cmake-build-debug/main 5 example_topology/test2initcosts5 example_topology/test2log5 &
./cmake-build-debug/main 6 example_topology/test2initcosts6 example_topology/test2log6 &
./cmake-build-debug/main 7 example_topology/test2initcosts7 example_topology/test2log7 &
./cmake-build-debug/main 255 example_topology/test2initcosts255 example_topology/test2log255
