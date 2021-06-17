# MPI ping-pong benchmark with congestion

This repository contains the benchmark described in Section III-B of the paper "A machine-learning approach for communication prediction of large-scale applications" (CLUSTER'15, https://ieeexplore.ieee.org/abstract/document/7307574/). 

To compile:
  mpicc -O3 -o benchmark mpi_pingpong_congestion.c -lm

To run:
  mpirun -np procs ./benchmark nodes messages
  
  - The benchmark receives two arguments: the number of nodes (to report the number of nodes and number of processes per node, procs/nodes) and the number of messages exchanged by each process.
